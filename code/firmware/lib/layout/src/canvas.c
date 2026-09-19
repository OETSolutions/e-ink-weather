#include "canvas.h"
#include <string.h>

/* Bit convention (HW-6): 1 = white, 0 = black; MSB-first within each byte;
 * row-major. This matches the vendor demo's 1 bpp 78,200-byte arrays. */

void canvas_init(canvas_t *c, uint8_t *buf)
{
    c->px = buf;
    c->len = EPD_FB_BYTES;
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
    size_t idx = (size_t)y * EPD_PITCH + (size_t)(x >> 3);
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
    size_t idx = (size_t)y * EPD_PITCH + (size_t)(x >> 3);
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
