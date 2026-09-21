#pragma once

#include <stddef.h>
#include <stdint.h>

/* Battery voltage history (FR-33).
 *
 * WHY A HISTORY AND NOT JUST THE CURRENT READING: FR-33 asks for "battery voltage history —
 * enough to diagnose a field failure remotely". On the deployed device the refresh interval is
 * 15 minutes and each wake is seconds long, so the only way to tell a healthy pack from one
 * that is slowly failing is the TREND across wakes — a single number at the moment someone
 * happens to query the API says almost nothing.
 *
 * It also feeds power_classify()'s trend term, which exists to disambiguate a full charged cell
 * from mains and is currently handed a hardcoded 0.0 because one boot cannot compute a trend.
 *
 * WHERE IT LIVES: the ring itself must survive deep sleep, so the device keeps it in
 * RTC_DATA_ATTR (the only memory that does). That is why the storage is a plain fixed array here
 * and not a linked structure — RTC memory is not initialised on wake, so the layout must be
 * trivially valid for arbitrary bytes, and a `count`/`head` pair that a stale write could push
 * out of range is exactly the kind of thing that corrupts it. Every reader therefore clamps. */

#define VBAT_HISTORY_MAX 24

typedef struct {
    int32_t  millivolts[VBAT_HISTORY_MAX];  /* oldest first once filled; -1 = unused slot */
    uint16_t count;                          /* valid samples, 0..VBAT_HISTORY_MAX */
    uint16_t head;                           /* next slot to write */
    int32_t  magic;                          /* distinguishes retained data from cold RTC RAM */
} vbat_history_t;

/* Stamped into `magic` so uninitialised RTC memory (which is arbitrary on a cold boot) is not
 * mistaken for a real history. Chosen to be a value random RAM is very unlikely to hold. */
#define VBAT_HISTORY_MAGIC 0x56424154  /* "VBAT" */

/* Reset to empty and stamp the magic. Call on a cold boot only. */
void vbat_history_init(vbat_history_t *h);

/* Whether this struct holds a usable history: magic matches AND the indices are in range. The
 * range check is what makes arbitrary retained bytes safe to read — a torn or stale write can
 * leave a plausible magic with a corrupt count, and a count past the array would otherwise walk
 * off the end. */
int vbat_history_valid(const vbat_history_t *h);

/* Append a sample in millivolts. A negative value is ignored (an ADC read that failed) rather
 * than recorded as a bogus sample that would drag the trend. Full history overwrites the oldest.
 * A struct that fails vbat_history_valid() is re-initialised first, so a corrupt ring heals
 * instead of being written into. */
void vbat_history_push(vbat_history_t *h, int32_t millivolts);

/* Number of samples, clamped to the array size even if `count` is corrupt. */
int vbat_history_count(const vbat_history_t *h);

/* The i-th sample counting from the OLDEST (0) to the newest. Returns -1 for an out-of-range
 * index. This order is what a display or a log wants; the ring's internal order is not. */
int32_t vbat_history_at(const vbat_history_t *h, int i);

/* Trend in volts per minute, or 0.0 when there is not enough history to compute one.
 *
 * `elapsed_minutes` is the wall-clock span the samples cover, supplied by the caller because the
 * ring stores volts only — the device has no RTC clock across deep sleep, so the span comes from
 * the configured wake interval times the number of samples, not from timestamps.
 *
 * Compares the oldest and newest samples: for a slow Li-ion discharge that is the right
 * simplification, because a least-squares fit over 24 noisy samples buys nothing the endpoints do
 * not already show, and the value is only ever compared against zero. Returns 0.0 (not a huge
 * number) when fewer than two samples exist, so a cold boot cannot report a phantom trend. */
double vbat_history_trend(const vbat_history_t *h, double elapsed_minutes);
