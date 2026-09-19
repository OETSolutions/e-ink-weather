#pragma once
#include <stdint.h>
#include "canvas.h"

/* Composite renderer (FR-1, FR-2, FR-3, FR-4b, NFR-9).
 *
 * The device is deliberately layout-INDEPENDENT. It never decides where anything goes and
 * never draws a label: the web app pre-renders a full-screen 1 bpp "static layer" (all the
 * chrome, labels, units, dividers) and pushes it down, plus a list of value boxes. The
 * firmware only stamps the current readings into those boxes. That is what lets a layout
 * change ship from the web app without a firmware update (FR-1). */

/* A box on the static layer that holds one dynamic string.
 *
 * THERE IS NO `text` FIELD ON PURPOSE: the dynamic string comes from `values[i]`, and any
 * static label is baked into the static layer by the web app. Adding a label here would
 * drag layout knowledge back into the firmware.
 *
 *   x, y     top-left of the box, in panel pixels
 *   w, h     box size; the value is clipped to this box, so a too-long reading cannot
 *            overwrite the surrounding static art
 *   align_h  'L', 'C' or 'R' — horizontal alignment within the box
 *   align_v  'T', 'M' or 'B' — vertical alignment within the box
 *   font_id  FONT_BODY or FONT_VALUE
 */
typedef struct {
    int  x, y, w, h;
    char align_h;
    char align_v;
    int  font_id;
} value_field_t;

/* Draw `static_layer` (EPD_FB_BYTES, 1 bpp, 1 = white) into `c`, then stamp `values[i]`
 * into `fields[i]` for i in [0, n_fields).
 *
 * Any unrecognised align char falls back to 'L'/'T' rather than being rejected: a config
 * from a newer web app must still render *something* legible instead of a blank screen.
 *
 * Returns 0 on success, negative if `c`, `static_layer`, `fields` or `values` is NULL when
 * the matching count is non-zero. */
int render_compose(canvas_t *c, const uint8_t *static_layer,
                   const value_field_t *fields, const char *const *values,
                   int n_fields);
