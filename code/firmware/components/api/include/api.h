#pragma once

#include "esp_err.h"

/* The device's own HTTP API (IF-4). Every endpoint is reachable on the LAN with no
 * authentication, which is a deliberate consequence of FR-31: the web app is served from
 * this device and configures it, so the two must share an origin and a session model. The
 * device is intended for a trusted home network, and the endpoints it exposes can only
 * change what the display shows — never reach further into the network.
 *
 * Endpoints:
 *   GET  /api/status    JSON device state (FR-33)
 *   GET  /api/config    the stored config document
 *   PUT  /api/config    validate, then persist (a bad body is refused, never stored)
 *   POST /api/bitmap    chunked static-bitmap upload, atomic promote (IF-2a)
 *   POST /api/refresh   force a refresh now
 *   POST /api/ota       firmware update over HTTPS with rollback (FR-32) */

/* Start the HTTP server. Returns ESP_OK once it is listening. */
esp_err_t api_start(void);

/* Force the next refresh to be a FULL one, regardless of the partial budget.
 *
 * This is what POST /api/refresh sets and what the boot path's "panel is showing something
 * stale" case needs: a partial refresh cannot clear ghosting, so a user asking for a
 * refresh after changing the layout wants a full. Read-and-clear: the flag is consumed by
 * whoever performs the next refresh. */
void api_request_full_refresh(void);

/* Register the task to wake when a refresh is requested, so POST /api/refresh actually
 * causes one.
 *
 * The flag alone is not enough: on USB power the device stays awake and the boot path
 * performs exactly ONE refresh tick, so a request arriving afterwards would set a flag that
 * nothing ever reads. The endpoint would answer 202 "refresh scheduled" and the panel would
 * never change — including after a bitmap upload, which is the main thing the web app does.
 *
 * `task` is a TaskHandle_t (passed as void* to keep FreeRTOS out of this header). The task
 * is woken with a task notification; a NULL handle just clears the registration. */
void api_set_refresh_task(void *task);

/* Take the pending full-refresh request. Returns 1 if one was pending (and clears it), 0
 * otherwise. Safe to call from a different task than api_request_full_refresh(). */
int api_take_full_refresh(void);

/* Ask for a SPECIFIC page to be drawn on the next refresh, overriding the rotation schedule
 * for that one frame (FR-15). Returns -1 from api_take_page() when no override is pending.
 *
 * WHY THIS EXISTS: the device picks its page from uptime modulo the pages' refreshSeconds, so
 * an edit to any page other than the one currently scheduled waits for its own rotation slot —
 * fifteen minutes with the shipped intervals. The layout editor therefore could not make the
 * page it is editing appear on the glass, and a value box the user just moved or added to a
 * non-scheduled page looked like it had been ignored. A one-shot override lets the editor say
 * "show me the page I am working on" and see it within a second.
 *
 * It also implies a FULL refresh: a different page means a different background, which a
 * partial cannot diff against.
 *
 * THE REQUEST IS NOT CONSUMED BY api_take_page(); it stays until the frame that honours it is
 * actually pushed (api_clear_page()). A tick that fails to get its framebuffer draws nothing, and
 * clearing the request there would drop the user's "show this page" with the request having had no
 * effect — they would then wait a rotation for the page they explicitly asked for. */
void api_request_page(int page);
int api_take_page(void);
/* Clear a page request once a frame carrying `page` has been rendered. Takes the page that was
 * honoured so a NEWER request arriving mid-render is not discarded — see the definition. */
void api_clear_page(int page);

/* Record an error for /api/status's `errors` array, newest first (FR-33). Bounded ring, so
 * a device stuck in an error loop cannot grow this without limit. */
void api_note_error(const char *msg);

/* Record that a refresh completed, and whether it was a full one.
 *
 * These counters are reported by /api/status and are what makes the daily-full-refresh
 * requirement (FR-10) observable from off-device: a device whose `fulls_total` has stopped
 * climbing while `partials_since_full` climbs past the limit is ghosting its panel. The
 * refresh path (Task 12) calls this; the API only reads it. */
void api_record_refresh(int was_full);

/* Record which page the last refresh rendered (FR-15). Kept separate from
 * api_record_refresh() because a refresh can be full or partial while the page is
 * independent of that, and /api/status reports them as separate facts. */
void api_record_page(int page);

/* Record the values the last refresh resolved, for GET /api/values (FR-27).
 *
 * The EDITOR's preview needs the real fetched data, and this device is the only thing that has
 * it: it runs each widget through value_format_widget(), the same path that puts the string on
 * the glass. The refresh path hands the result here; the HTTP handler only serialises it.
 *
 * `ids` and `texts` are parallel arrays of `count` entries, for page number `page`. The call is a
 * no-op for a count of 0, a count beyond the fixed cap, or a page index the store does not hold. */
void api_record_values(const char (*ids)[24], const char (*texts)[40],
                       const int *has_value, int count, int page, int page_count);

/* Make sure the per-page value store can hold `page_count` pages, allocating it on first use.
 *
 * WHY THE CALLER SAYS HOW MANY: the store is sized to the config's page count rather than a fixed
 * maximum, because a fixed array of LAYOUT_MAX_PAGES(8) x 24 entries is ~13 KB and .bss on this
 * part is DRAM the heap never gets — measured before, a static of that order dropped the largest
 * free block below what the static layer needs and nothing could be drawn. Called from the refresh
 * tick AFTER the fetch buffers are freed, which is when the heap has room; a failure is non-fatal
 * (the endpoint answers for whatever pages it holds). */
void api_values_reserve(int page_count);

/* Give the per-page value store's memory back.
 *
 * WHY A SAVE NEEDS THIS: the store is a preview cache the editor reads, and it costs ~1.6 KB per
 * page resident for the life of the process. A config PUT has to build a cJSON tree costing several
 * times the document's size out of the same heap, and at idle this part has only ~41 KB free — so a
 * document around 8 KB sat exactly on the boundary and was refused while a 7 KB one stored. Dropping
 * the cache for the duration of the parse buys back the room for a page or two of headroom. It is
 * rebuilt by the next refresh tick (api_values_reserve), leaving only the preview briefly stale,
 * which cannot affect the glass. */
void api_values_forget(void);

/* The partial budget in force, from the stored config. 0 means every refresh is full. */
int api_partial_limit(void);

/* The configured update interval in seconds, from the stored config (FR-9's "configurable
 * interval"), defaulting to 900 when it is missing or below the 30 s floor.
 *
 * The battery path gets this from the boot path's own parse and hands it to the deep-sleep
 * timer. The always-on mains path does NOT sleep, so the serve loop calls this itself to
 * refresh on the same interval — without it a plugged-in device only ever refreshed when the
 * API asked, and a weather display on mains never showed a new reading. */
int api_update_seconds(void);

/* How many partial refreshes have happened since the last full one. The refresh path feeds
 * this to refresh_decide() — passing a constant instead would silently disable the partial
 * budget, so the count has to come from the same place api_record_refresh() writes it. */
int api_partials_since_full(void);

/* Forget every recorded refresh and error. Called once at boot so the counters describe
 * THIS power cycle rather than accumulating across deep sleeps — a device that wakes hourly
 * would otherwise report a `free_heap_min` from days ago. */
void api_reset_cycle_counters(void);

/* Which bitmap slot is currently live, or -1 if neither slot holds a valid image. */
int api_live_bitmap_slot(void);

/* Report the battery voltage measured at boot, and the power source inferred from it.
 *
 * FR-33 requires battery voltage in /api/status, but the sense line is on ADC2, which is
 * UNUSABLE while WiFi is active (HW-3) — and /api/status only exists when WiFi is up. So the
 * reading is taken once, early, with the radio off, and cached here. The alternative (read
 * ADC2 from the HTTP handler) returns a plausible wrong number, which is worse than a stale
 * right one: a device on mains would be reported as a half-flat battery. */
void api_note_vbat(double volts, int source);

/* Battery voltage history (FR-33).
 *
 * Must be called ONCE per boot, before api_note_vbat(). On a cold boot the retained RTC bytes
 * fail validation and the history starts empty; on a deep-sleep wake it is kept, which is what
 * makes a trend observable at all on a device that samples once per wake. */
void api_vbat_history_boot(void);

/* How many samples the retained history holds. 0 means "none yet" — the device has not woken
 * with a valid reading since it was last powered up or reflashed. */
int api_vbat_history_count(void);

/* Trend across the retained history in volts per minute, given the wall-clock span those samples
 * cover. Returns 0.0 when there is too little history: zero is the safe "not falling" answer for
 * power_classify(), so an unknown must not come back as a large negative. */
double api_vbat_trend(double span_minutes);

/* ---- OWM daily call count and cap (spec §3.4) -------------------------------------------
 *
 * The spec requires the firmware to count and cap daily OpenWeatherMap calls and surface the
 * count in /api/status "so a bug cannot silently burn the quota". The free tier allows 1,000
 * calls/day and a 10-15 minute refresh uses ~96-144, so the cap is a TRIPWIRE for the
 * abnormal case (a refresh loop), not a normal operating limit.
 *
 * The day is the CALENDAR day, because the provider's quota resets on a calendar boundary —
 * a rolling 24 h window would cap a device early after it refreshed across midnight. The
 * timestamp comes from the `dt` field already present in each OWM response, so no wall clock
 * has to be maintained across deep sleep (there is none: esp_timer_get_time() is documented
 * as "time since boot" and resets on every wake). */

/* 1 if another call is allowed under the daily cap, 0 if the cap is reached. Call before
 * making the request — this is what makes the cap actually prevent a call. Returns 1 when no
 * timestamp has been seen yet: failing open is deliberate, because refusing to fetch on an
 * unknown day would brick the display, which is worse than a momentarily uncounted call. */
int api_owm_should_call(void);

/* Record the outcome of a fetch attempt. `now_unix` is the response's own timestamp (from
 * datasrc_value_t.observed_at); `did_call` is 1 if a request was actually sent.
 *
 * WHY A SUCCESSFUL FETCH IS COUNTED EVEN WHEN THE TIMESTAMP IS KNOWN: the timestamp arrives
 * WITH the response, so the call cannot be attributed to a day until after it has been made.
 * The count is therefore reconciled on the next attempt — if that one lands on a new calendar
 * day, the stale day's count is discarded wholesale and the new day starts fresh. */
void api_owm_note_call(long now_unix, int did_call);
