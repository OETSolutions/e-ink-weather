#pragma once

#include <stddef.h>
#include <stdint.h>

#define EPD_WIDTH    920
#define EPD_HEIGHT   680
#define EPD_PITCH    (EPD_WIDTH / 8)          /* 115 bytes per row */
#define EPD_FB_BYTES ((size_t)EPD_PITCH * EPD_HEIGHT)   /* 78,200 (HW-6) */

typedef struct {
    uint8_t *px;
    size_t   len;
    /* WHICH PART OF THE PANEL THIS CANVAS COVERS. `y_off` is the first panel row the buffer
     * holds and `h` is how many rows it holds, so (x, y) is stored at row (y - y_off).
     *
     * WHY A SUBSET IS WORTH THE TWO EXTRA FIELDS: a partial refresh needs the PREVIOUS and the
     * NEXT frame at the same time, which on this part is impossible — two 78,200-byte frames do
     * not fit the one DRAM region large enough for either (measured: 113,840 B region vs 156,400
     * for the pair). Rendering into ROW BANDS instead means both frames can be produced a
     * hundred-odd rows at a time, so a partial needs two small band buffers rather than two whole
     * framebuffers. For a normal full-panel canvas these are 0 and EPD_HEIGHT, so every existing
     * caller is unaffected. */
    int      y_off;
    int      h;
} canvas_t;

void canvas_init(canvas_t *c, uint8_t *buf);

/* The same, for a buffer holding only rows [y0, y0 + n_rows) of the panel.
 *
 * (x, y) coordinates stay PANEL-ABSOLUTE, so a caller drawing a widget at panel row 500 needs no
 * arithmetic of its own — the translation to band-local storage happens in canvas_set_px(). That
 * is what lets the band renderer reuse the ordinary draw path unchanged. `buf` must hold
 * n_rows * EPD_PITCH bytes. */
void canvas_init_band(canvas_t *c, uint8_t *buf, int y0, int n_rows);
void canvas_fill(canvas_t *c, int black);
void canvas_set_px(canvas_t *c, int x, int y, int black);
int  canvas_get_px(const canvas_t *c, int x, int y);
void canvas_blit_1bpp(canvas_t *c, int x, int y,
                      const uint8_t *src, int src_w, int src_h);
void canvas_blit_1bpp_masked(canvas_t *c, int x, int y,
                             const uint8_t *src, int src_w, int src_h,
                             int transparent_white);
