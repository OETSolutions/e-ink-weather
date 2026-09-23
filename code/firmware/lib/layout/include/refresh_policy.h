#pragma once

/* Which kind of refresh this update needs (FR-10, FR-11, FR-12).
 *
 * WHY THIS IS A PURE FUNCTION AND NOT AN `if` AT THE CALL SITE: the daily full refresh is a
 * PANEL DATASHEET requirement — the GDEH0576T81 accumulates ghosting if it is only ever
 * partially refreshed, and the datasheet's remedy is a full refresh at least every 24 h.
 * Making that a tested function means it cannot be forgotten by a caller, and the rule is
 * checkable without hardware. */

typedef enum { REFRESH_FULL, REFRESH_PARTIAL } refresh_kind_t;

/* Decide whether this update is a full refresh (slow, flashes, clears ghosting) or a
 * partial (fast, low flicker, does not clear ghosting).
 *
 * A full refresh is forced when ANY of these holds:
 *   - `nothing_on_glass` — no trustworthy frame is on the panel yet. A partial refresh is a
 *     diff against the frame currently displayed, so with nothing there it would diff
 *     against nothing and fill the screen with noise. True at boot, and true again after a
 *     static-layer change (see below);
 *   - the partial budget is spent (`partial_limit <= 0`, or
 *     `partials_since_full >= partial_limit`);
 *   - 24 h have passed since the last full — the datasheet rule, and it deliberately beats
 *     the partial budget: a device that refreshes every 5 minutes would otherwise never
 *     reach the limit and would ghost.
 *
 * `nothing_on_glass` IS SEPARATE FROM `partials_since_full`, and the two must not be
 * conflated. A counter of 0 has two distinct meanings — "never drawn anything" and "a full
 * refresh just happened, the glass is clean" — and only the first needs a full refresh. An
 * earlier version of this function took only the counter and treated 0 as "nothing drawn",
 * which made every refresh a full one: the counter is reset to 0 BY a full refresh, so the
 * condition was permanently true and the partial path was dead code. The panel would have
 * flashed on every update and the battery budget (NFR-4) would have been missed.
 *
 * `hours_since_full` is whole hours; 24 means the boundary is crossed. */
refresh_kind_t refresh_decide(int nothing_on_glass, int partials_since_full,
                              int partial_limit, int hours_since_full);

/* Whether the BOOT path should redraw the last-good image before the network runs (FR-29, FR-12).
 *
 * WHY THIS IS A QUESTION AT ALL. FR-29 wants an image on the glass immediately rather than a blank
 * panel during the boot fetch. On a COLD boot the firmware cannot know what a power cut left there,
 * so it must draw — that is the case this exists for, and it stays. But after a TIMER WAKE it can
 * know, and then drawing is actively harmful:
 *
 *   - the panel is BISTABLE, so the frame the device slept with is still on the glass — the image
 *     is already as good as the boot draw could make it;
 *   - the boot draw paints the STATIC LAYER ALONE, because the values that were on it did not
 *     survive the sleep. So it REPLACES a frame that had readings with one that has none, which the
 *     next tick then has to put back — the user watches the numbers vanish and return;
 *   - it is a FULL refresh (the bare layer is not diffable against anything the firmware still
 *     holds), so it FLASHES. On battery every wake then costs two panel updates instead of one,
 *     and the panel's refresh budget is the thing FR-10…FR-13 exist to conserve.
 *
 * So a timer wake skips the boot draw and lets the single fetch-and-render update be the only one.
 * The failure path is not made worse: if the fetch then fails, the glass keeps the previous frame
 * — which is the last good image FR-29 asks for, and strictly better than the values-less layer the
 * boot draw would have left.
 *
 * `woke_from_timer` is the caller's reading of esp_sleep_get_wakeup_cause(): true only when this
 * boot is the tail of a deliberate deep sleep, i.e. the one case where the firmware knows it slept
 * the panel with a frame on it. Any other cause — cold power-on, reset, panic — draws. */
int boot_needs_last_good_draw(int woke_from_timer);
