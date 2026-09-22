#include "canvas.h"
#include <string.h>

/* Bit convention (HW-6): 1 = white, 0 = black; MSB-first within each byte;
 * row-major. This matches the vendor demo's 1 bpp 78,200-byte arrays. */

void canvas_init(canvas_t *c, uint8_t *buf)
{
    c->px = buf;
    c->len = EPD_FB_BYTES;
    c->y_off = 0;
    c->h = EPD_HEIGHT;
}

void canvas_init_band(canvas_t *c, uint8_t *buf, int y0, int n_rows)
{
    c->px = buf;
    /* `len` is the STORED size, not the panel size: callers that walk the buffer (canvas_fill)
     * must not write past the rows this band actually holds. */
    c->len = (n_rows > 0) ? (size_t)n_rows * EPD_PITCH : 0;
    c->y_off = y0;
    c->h = (n_rows > 0) ? n_rows : 0;
}

void canvas_fill(canvas_t *c, int black)
{
    memset(c->px, black ? 0x00 : 0xFF, c->len);
}

void canvas_set_px(canvas_t *c, int x, int y, int black)
{
    if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) {
        return;
    }
    /* Rows outside this canvas are simply not in the buffer. A band renderer draws a whole page
     * of widgets into one band at a time, so most of those calls legitimately miss — the y test
     * that used to be "is this on the panel" is now "is this row in THIS band". */
    const int row = y - c->y_off;
    if (row < 0 || row >= c->h) {
        return;
    }
    size_t idx = (size_t)row * EPD_PITCH + (size_t)(x >> 3);
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    if (black) {
        c->px[idx] &= (uint8_t)~mask;
    } else {
        c->px[idx] |= mask;
    }
}

int canvas_get_px(const canvas_t *c, int x, int y)
{
    if (x < 0 || y < 0 || x >= EPD_WIDTH || y >= EPD_HEIGHT) {
        return 0;
    }
    const int row = y - c->y_off;
    if (row < 0 || row >= c->h) {
        return 0;
    }
    size_t idx = (size_t)row * EPD_PITCH + (size_t)(x >> 3);
    uint8_t mask = (uint8_t)(0x80u >> (x & 7));
    return (c->px[idx] & mask) ? 0 : 1;   /* bit clear => black => 1 */
}

static void blit(canvas_t *c, int x, int y,
                 const uint8_t *src, int src_w, int src_h, int transp)
{
    int pitch = (src_w + 7) / 8;
    for (int sy = 0; sy < src_h; sy++) {
        for (int sx = 0; sx < src_w; sx++) {
            uint8_t b = src[(size_t)sy * pitch + (size_t)(sx >> 3)];
            int black = (b & (0x80u >> (sx & 7))) ? 0 : 1;
            if (transp && !black) {
                continue;
            }
            canvas_set_px(c, x + sx, y + sy, black);
        }
    }
}

void canvas_blit_1bpp(canvas_t *c, int x, int y,
                      const uint8_t *src, int src_w, int src_h)
{
    blit(c, x, y, src, src_w, src_h, 0);
}

void canvas_blit_1bpp_masked(canvas_t *c, int x, int y,
                             const uint8_t *src, int src_w, int src_h,
                             int transparent_white)
{
    blit(c, x, y, src, src_w, src_h, transparent_white);
}
