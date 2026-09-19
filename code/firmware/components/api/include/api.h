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

/* The partial budget in force, from the stored config. 0 means every refresh is full. */
int api_partial_limit(void);

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
