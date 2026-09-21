#pragma once
#include "datasrc.h"

/* OpenWeatherMap parsing (IF-3, FR-6).
 *
 * TWO PRODUCT SHAPES ARE SUPPORTED, and this is not an edge case — it is the normal
 * case. Verified live 2026-09-18 with a real key:
 *
 *   data/3.0/onecall   -> 401 "requires a separate subscription to the One Call by Call
 *                         plan"
 *   data/2.5/weather   -> 200
 *   data/2.5/forecast  -> 200
 *
 * So a key without that subscription — the default state of a new account — only ever
 * sees 2.5 data. The two schemas differ in three ways, and every one of them yields a
 * plausible wrong answer if you assume One Call's shape:
 *
 *   1. Current temperature is at the TOP LEVEL ("main"."temp") in 2.5/weather, not under
 *      "current". 2.5/weather has NO "list" key at all.
 *   2. There is no "daily" array. The forecast is "list" — 40 blocks at 3-hour steps —
 *      with temp_min/temp_max FLAT under "main", not nested under "temp". 2.5/forecast
 *      has NO top-level "main" at all, so a current-temperature read against it must
 *      fail rather than return list[0].main.temp (which is a 3-hour forecast value).
 *   3. There is no "alerts" key, so the OWM alert mechanism (FR-14 mechanism 2) is
 *      unavailable on the free tier. Absence means "no alerts", a legitimate 0 — NOT an
 *      error. */
datasrc_value_t owm_parse_current_temp(const char *json, long now_unix);

/* Daily forecast min/max for a 0-based day index.
 * One Call 3.0: "daily"[i]."temp"."min"/"max" — the real daily values.
 * Free 2.5:     "list" grouped into LOCAL calendar days via "city"."timezone", reducing
 *               temp_min/temp_max across each day. This APPROXIMATES the daily extreme,
 *               because a 3-hour block's temp_min/temp_max only bounds those 3 hours. On
 *               live data 37 of 40 blocks report temp_min == temp_max, so most of the
 *               reduction comes from the few blocks that do carry a spread. Good enough
 *               for a forecast bar; never present it as exact. */
datasrc_value_t owm_parse_daily_min(const char *json, int day_index, long now_unix);
datasrc_value_t owm_parse_daily_max(const char *json, int day_index, long now_unix);

/* 1 if the response carries at least one government weather alert (One Call 3.0 only).
 * The free 2.5 products have no "alerts" key; absence is a legitimate 0, not an error. */
int owm_has_alerts(const char *json);

/* ---------------------------------------------------------------- which product (FR-6) -- */

/* The three settings the config's `owmProduct` can carry.
 *
 * AUTO IS THE DEFAULT AND THE COMMON CASE: a key without the "One Call by Call" subscription
 * gets a 401 from 3.0, so the free 2.5 products are the normal path rather than a fallback.
 * AUTO therefore probes 3.0 and falls back on that documented not-subscribed response, which is
 * what FR-6 asks for. The explicit settings exist so a user who KNOWS which product their key
 * carries is not paying a failed request on every refresh to find out. */
typedef enum {
    OWM_PRODUCT_AUTO = 0,   /* probe One Call 3.0, fall back to the free 2.5 pair */
    OWM_PRODUCT_ONECALL3,   /* One Call 3.0 only */
    OWM_PRODUCT_LEGACY      /* the free 2.5 current + forecast pair only */
} owm_product_t;

/* Parse the config's `owmProduct` string ("auto" | "onecall3" | "legacy"). Returns
 * OWM_PRODUCT_AUTO for NULL, "", or anything unrecognised — an unknown value must fall back to
 * the probing behaviour, never to a fixed product the user did not ask for. Same rule as
 * power_mode_from_string(), and for the same reason. */
owm_product_t owm_product_from_string(const char *s);

/* Does this product carry official government alerts? TRUE only for One Call 3.0.
 *
 * WHY THIS IS A FUNCTION AND NOT AN INLINE COMPARISON: FR-7 requires the alert feature to
 * degrade EXPLICITLY when the product cannot provide alerts. The distinction the renderer needs
 * is not "are there alerts in this document" but "could this document ever carry one" — those
 * are the same answer on One Call and opposite answers on the free tier, where an empty alert
 * bar means "quiet weather" on one product and "this product has no alerts at all" on the other.
 *
 * Call it with the RESOLVED product (owm_product_resolve), never with AUTO: AUTO has not probed
 * and so cannot answer, and this function reports 0 for it rather than guessing. */
int owm_product_has_alerts(owm_product_t p);

/* The product the firmware will ACTUALLY fetch with, given the config setting and the probe.
 *
 * WHY THIS EXISTS: the config toggle is only meaningful if it changes which endpoint is called.
 * It is not enough to label the fetch — on 'onecall3' the firmware must call 3.0/onecall and on
 * 'legacy' it must call the 2.5 pair, or the toggle is a lie in the UI and the alert bar's
 * FR-7 message describes a product the device is not actually using.
 *
 * `onecall_available` is the probe's tri-state answer: 1 available, 0 definitively not
 * subscribed (a 401/403), -1 not yet known (no key stored, or the probe could not reach the
 * API). AUTO resolves to One Call only on a definite 1; on -1 it uses the free pair, because
 * something must be fetched and the free pair is the one that works for an unsubscribed key. */
owm_product_t owm_product_resolve(owm_product_t configured, int onecall_available);

/* One named field of the CURRENT conditions, for BIND_OWM_CURRENT widgets.
 *
 * WHY A SINGLE ENTRY POINT rather than one function per field: the two product shapes differ in
 * WHERE each field lives (One Call nests everything under "current"; 2.5/weather puts
 * temperature and humidity under "main" and wind under "wind"), and splitting them across
 * functions would put that shape knowledge in N places and let one of them be wrong. Here the
 * shape is decided once.
 *
 * `field` is a `owm_field_t` (lib/layout/include/widgets.h). For a TEXT field (condition) the
 * result carries is_numeric = 0 and the words in `text`; a caller that prints `value` for a
 * condition would print 0.000, which is why is_numeric exists. */
datasrc_value_t owm_parse_current_field(const char *json, int field, long now_unix);

/* ------------------------------------------------------- forecast request sizing (FR-6) -- */

/* The whole 5-day horizon the free 2.5/forecast product carries: 8 three-hour blocks per day. */
#define OWM_FORECAST_MAX_BLOCKS 40

/* The buffer a `cnt`-block forecast response needs, with headroom.
 *
 * WHY THIS IS A FUNCTION AND NOT A CONSTANT AT THE CALL SITE: the firmware builds the request
 * from the layout's highest day index and receives it into a fixed buffer, and those two numbers
 * MUST agree. They did not — the request was allowed up to 40 blocks while the buffer held
 * 12,288 bytes, and a 40-block response measures 16,575 bytes. net_http refuses to hand a parser
 * a clipped document and reports ESP_ERR_NO_MEM, so every request past 3 days failed outright and
 * every forecast widget on such a page silently rendered "--", while the web app kept offering
 * Day 4 and Day 5 in its picker. Deriving both sides from one rule is what makes that
 * unrepeatable, and it is host-testable because it is pure arithmetic.
 *
 * MODELLED FROM MEASUREMENT (live API, 2026-09-20): 3,512 B at cnt=8, 6,819 at 16, 10,069 at 24,
 * 13,314 at 32, 16,575 at 40 — a marginal cost of ~406 bytes per block over a fixed envelope. The
 * per-block figure is rounded UP to 448 and the envelope to 1,024, because a longer place name or
 * a wetter week lengthens the document and the cost of guessing low is a silently blank panel. */
#define OWM_FORECAST_BYTES_PER_BLOCK 448
#define OWM_FORECAST_BASE_BYTES      1024

/* Returns the bytes to allocate for a request of `blocks` 3-hour blocks, or 0 for an invalid
 * count. Always at least OWM_FORECAST_BASE_BYTES. `unsigned` rather than `size_t` because this
 * header deliberately pulls in nothing but the value contract (IF-3). */
unsigned owm_forecast_buf_bytes(int blocks);
