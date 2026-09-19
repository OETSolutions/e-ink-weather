#pragma once

typedef enum { REFRESH_FULL, REFRESH_PARTIAL } refresh_kind_t;

/* Decide whether this update is a full refresh (slow, clears ghosting) or a
 * partial (fast, low flicker). A full refresh is forced when:
 *   - nothing has been drawn since the last full (partials_since_full == 0),
 *   - the partial budget is spent, or
 *   - 24 h have passed since the last full, per the panel datasheet (FR-10).
 *
 * COUNTER PROTOCOL — the caller must follow this exactly, because getting it wrong is
 * silent and expensive:
 *   - After a full refresh, reset the counter to 0 and then INCREMENT BEFORE the next
 *     call. `partials_since_full == 0` is reserved for "this device has never completed a
 *     refresh" (a cold boot). If the caller forgets to increment, the counter stays 0 and
 *     every wake decides FULL — the device still works and still avoids ghosting, but it
 *     is slow and flickers on every update with no error anywhere.
 *   - On a cold boot with no stored history, pass 0 to force the first refresh to be full.
 *
 * `hours_since_full` must be the CEILING of elapsed hours, not a truncation. Truncating
 * lets the panel exceed the datasheet's 24 h limit: with a 13-hour update interval the
 * checks land at 13 h (partial) and 26 h (full), so 26 hours pass between full refreshes.
 * FR-10 is a panel-lifetime requirement, so the caller must round up. */
refresh_kind_t refresh_decide(int partials_since_full, int partial_limit,
                              int hours_since_full);
