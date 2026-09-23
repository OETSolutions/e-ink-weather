#pragma once
#include <stddef.h>
#include "datasrc.h"
#include "widgets.h"

/* Turning ONE widget into the exact string that appears on the glass (FR-2, FR-3, FR-23).
 *
 * WHY THIS IS ITS OWN MODULE, host-tested: this is where a wrong binding or a stray "0.0"
 * becomes something the user reads off a wall, and it is the only part of the render path that
 * is pure logic. It is also the counterpart of the web app's data/format.ts — the preview and
 * the device must agree, or the preview is a confident lie.
 *
 * THE WIDGET DOES NOT FETCH. It is handed the already-fetched documents and picks what it
 * needs. That keeps the network on the caller's side (so one forecast fetch serves every
 * forecast widget) and makes every branch here testable with no radio. */

/* What a widget needs fetched, so the caller can fetch it ONCE for the whole page rather than
 * once per widget. Built by scanning the parsed widgets. */
typedef struct {
    int need_owm_current;   /* any BIND_OWM_CURRENT widget */
    int need_owm_daily;     /* any BIND_OWM_DAILY widget (needs the 5-day forecast document) */
    int need_owm_alert;     /* any BIND_OWM_ALERT widget */
    int need_ha;            /* any BIND_HA widget */
    int max_day_index;      /* the highest forecast day any widget asks for */
} value_needs_t;

/* Scan `w[0..n)` and report what must be fetched. Always returns 0. */
int value_scan_needs(const layout_widget_t *w, int n, value_needs_t *out);

/* The documents a page's widgets read from. Any of them may be NULL, which makes the widgets
 * that needed it render their fallback rather than a wrong number. */
typedef struct {
    const char *owm_current;    /* 2.5/weather or One Call 3.0 */
    const char *owm_daily;      /* 2.5/forecast or One Call 3.0 */
    const char *ha_line;        /* the '|'-separated template response, or NULL */
    /* FR-7: can the configured OWM product carry official alerts AT ALL?
     *
     * This is NOT the same question as "does this document contain an alert". On One Call 3.0 an
     * empty alert bar means quiet weather; on the free 2.5 products it means the product has no
     * alerts to give, and FR-7 requires the firmware to SAY so rather than show a blank bar the
     * user reads as "no severe weather". Without this flag those two states are indistinguishable
     * at the point of drawing, which is exactly the silent degradation FR-7 forbids. */
    int         owm_alerts_supported;
} value_sources_t;

/* The HA entities a widget set needs, in the order they were first seen — so the caller can
 * build ONE template request for the page. Writes up to `cap` entity ids into `out`
 * (each at least 48 bytes) and returns how many were written; `*needed` reports how many the
 * page actually wanted, so a caller that ran out of room can tell truncation from success. */
int value_collect_ha_entities(const layout_widget_t *w, int n,
                              char (*out)[48], int cap, int *needed);

/* Format ONE resolved value into `buf` (NUL-terminated, never longer than `cap`).
 *
 * Returns 1 if a real reading was drawn, 0 if the fallback was used. The distinction is
 * returned rather than inferred by comparing strings because the fallback is user-configurable
 * — the alert bar's fallback is the empty string — so a caller cannot tell them apart.
 *
 * `ids`/`n_ids` are the PAGE's HA entity list, in template order, from
 * value_collect_ha_entities(). They must be passed in rather than derived here: the template
 * response is one '|'-separated line for the whole page, so the second HA widget on a page
 * reads the SECOND token. A widget-local list would make every HA widget read token 0 and show
 * the same entity's value twice.
 *
 * A NON-FINITE OR UNAVAILABLE READING NEVER PRINTS A NUMBER. `0.0` is plausible weather and
 * "NaN" is not a reading; both would be read as fact on a wall.
 */
int value_format_widget(const layout_widget_t *w, const value_sources_t *src,
                        char (*ids)[48], int n_ids, long now_unix,
                        datasrc_value_t *out_value, char *buf, size_t cap);

/* As value_format_widget(), plus `*out_rendered_number` set to 1 ONLY when `buf` is the plain
 * rendering of `out_value->value` (prefix + the formatted number + suffix).
 *
 * WHY THIS EXISTS. GET /api/values reports the raw reading so the editor can re-format a value
 * after the user edits its decimals or affixes — otherwise the box keeps showing the string the
 * device formatted with the OLD format until a save and a repaint (reported as "the editor doesn't
 * show the updated values until you first save and refresh"). But only this function knows whether
 * the drawn text really is that number: a firing alert replaces it with a level word, a text reading
 * carries a condition or a sensor state, an icon binding carries the OWM code, and a failed fetch
 * shows the widget's fallback. Re-formatting the reading in any of those cases would draw a number
 * where the panel has a word, so the firmware reports which branch it took instead of letting the
 * editor infer it from the string — an inference that cannot be made, since a fallback of "72.5" is
 * indistinguishable from a reading of 72.5. */
int value_format_widget_rendered(const layout_widget_t *w, const value_sources_t *src,
                                 char (*ids)[48], int n_ids, long now_unix,
                                 datasrc_value_t *out_value, int *out_rendered_number,
                                 char *buf, size_t cap);

/* The alert level a widget's rules raise for `value`. Returns ALERT_NONE when the widget has no
 * rules or the value is unavailable — a missing reading must never raise an alarm, which is the
 * rule alerts_eval_all() already enforces and this simply forwards. */
alert_level_t value_widget_alert_level(const layout_widget_t *w, const datasrc_value_t *v);
