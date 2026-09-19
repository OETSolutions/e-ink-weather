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
} canvas_t;

void canvas_init(canvas_t *c, uint8_t *buf);
void canvas_fill(canvas_t *c, int black);
void canvas_set_px(canvas_t *c, int x, int y, int black);
int  canvas_get_px(const canvas_t *c, int x, int y);
void canvas_blit_1bpp(canvas_t *c, int x, int y,
                      const uint8_t *src, int src_w, int src_h);
void canvas_blit_1bpp_masked(canvas_t *c, int x, int y,
                             const uint8_t *src, int src_w, int src_h,
                             int transparent_white);
