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

/* Read `len` bytes of the static layer starting at `offset` into `dst`.
 * Returns 0 on success, non-zero on failure. */
typedef int (*render_read_fn)(void *ctx, size_t offset, uint8_t *dst, size_t len);

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

/* Same, but the static layer is PULLED through `read` in chunks instead of being a
 * contiguous buffer.
 *
 * WHY THIS EXISTS: the static layer is 78,200 bytes and the device has 320 KB of RAM with
 * no PSRAM (NFR-2). It also lives in a flash partition, so copying it into RAM only to copy
 * it again into the frame buffer costs 78 KB — a quarter of the heap — for nothing. This
 * reads it in `RENDER_CHUNK` windows straight into the frame buffer, so the peak cost is
 * one window regardless of panel size. The host tests use render_compose(), which delegates
 * here with a memcpy reader, so both paths share one implementation of the compositing. */
#define RENDER_CHUNK 1024

int render_compose_stream(canvas_t *c, render_read_fn read, void *ctx,
                          const value_field_t *fields, const char *const *values,
                          int n_fields);

/* Compose ONE HORIZONTAL BAND of the frame: rows [y0, y0 + n_rows) only.
 *
 * WHY A PARTIAL REFRESH NEEDS THIS: `epd_write_frame_partial(prev, next)` derives each pixel's
 * transition from BOTH frames, so both must exist at once — and two 78,200-byte frames do not fit
 * this part's DRAM (measured: the only region large enough for one is 113,840 bytes, and the pair
 * needs 156,400). Composing a band at a time means the previous and the next frame can each be
 * produced a few dozen rows at a time, so a partial needs two SMALL buffers instead of two whole
 * framebuffers. Without this, FR-11's partial path cannot run at all: measured on the bench,
 * `s_fb_next` failed to allocate on every attempt and `/api/status` reported partials_since_full: 0
 * against fulls_total: 6, so every refresh was a full panel flash.
 *
 * `read` supplies the STATIC layer — the same full-panel reader the whole-frame path uses, so the
 * caller needs one copy of the static layer rather than one per band. Field coordinates stay
 * panel-absolute; `c` decides which rows it holds (see canvas_init_band).
 *
 * Returns 0 on success, negative on bad arguments or a failed static-layer read. */
int render_compose_band(canvas_t *c, render_read_fn read, void *ctx,
                        const value_field_t *fields, const char *const *values,
                        int n_fields);
