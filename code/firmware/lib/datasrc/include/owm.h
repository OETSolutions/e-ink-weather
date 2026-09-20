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
