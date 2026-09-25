#pragma once

/* The OWM daily call counter (spec §3.4), as pure logic.
 *
 * WHY THIS IS A LIBRARY AND NOT CODE IN THE HTTP SERVER: the spec requires the firmware to
 * "count and cap daily calls", and the failure it guards against — a refresh loop quietly
 * burning the provider's quota — is not reproducible on the bench without waiting a day. The
 * interesting cases are all calendar arithmetic (which day a call belongs to, what happens
 * across the boundary, what a missing timestamp means), and those are exactly the cases a host
 * test can pin down and the device cannot.
 *
 * The day is the CALENDAR day, because the provider's quota resets on a calendar boundary. A
 * rolling 24 h window would let a device that refreshes across midnight accumulate calls from
 * two calendar days and cap itself early — refusing to update on a perfectly normal day. */

typedef struct {
    long day;        /* which calendar day the count belongs to, or 0 if never set */
    int  calls;      /* calls recorded during `day` */
    int  have_day;   /* 0 until a usable timestamp has been seen */
} owm_counter_t;

void owm_counter_init(owm_counter_t *c);

/* 1 if another call is allowed under `cap`, 0 if the cap is reached for the current day. */
int owm_counter_should_call(const owm_counter_t *c, int cap);

/* Credit one call. `now_unix` is the timestamp carried in the response itself, so a call can
 * only be attributed to a day AFTER it returns; a non-positive value is ignored rather than
 * blamed on epoch day 0.
 *
 * The day index only ever moves FORWARD: a timestamp for a LATER day starts a fresh quota, and a
 * same-or-earlier one is counted into the current day without resetting. That monotonicity is
 * load-bearing on the free tier, where a tick makes two OWM calls that disagree on the date —
 * 2.5/weather reports the observation, 2.5/forecast reports its next 3-hourly slot, which is
 * 00:00 tomorrow for the last three hours of each UTC day. Resetting on any difference let those
 * two calls cancel each other's count every tick, pinning the count at 1 and killing the cap.
 * See owm_counter.c for the measurement.
 *
 * `cap` is passed in and the count is CLAMPED to it, so the counter cannot exceed the cap even
 * if a caller records without asking first. owm_counter_should_call() is still what must gate
 * the request — clamping here is defence in depth, not the mechanism. */
void owm_counter_note_call(owm_counter_t *c, long now_unix, int cap);

/* Calls recorded for the current day. Meaningless until owm_counter_known() is 1. */
int owm_counter_calls(const owm_counter_t *c);

/* 1 once a timestamp has been seen. "No calls yet" and "cannot count" must be reported
 * differently, or a status page hides the runaway it exists to expose. */
int owm_counter_known(const owm_counter_t *c);
