#include "render.h"
#include "fonts.h"
#include <string.h>

/* Horizontal offset of the text within its box, for 'L'/'C'/'R'. */
static int offset_h(char a, int box_w, int text_w)
{
    if (a == 'C') return (box_w - text_w) / 2;
    if (a == 'R') return box_w - text_w;
    return 0;                       /* 'L' and anything unrecognised */
}

/* Vertical offset of the line box within its box, for 'T'/'M'/'B'. */
static int offset_v(char a, int box_h, int line_h)
{
    if (a == 'M') return (box_h - line_h) / 2;
    if (a == 'B') return box_h - line_h;
    return 0;                       /* 'T' and anything unrecognised */
}

/* Stamp one glyph, clipped to the field box, painting only INK.
 *
 * White pixels are deliberately skipped rather than written: the static layer underneath
 * carries the chrome, and a solid-white glyph box would punch a rectangle out of it. This
 * is the `transparent_white = 1` behaviour render.h promises.
 *
 * Clipping to the box (not just to the panel) is what stops an over-long reading from
 * overwriting the surrounding static art — a field that overflows is truncated, not
 * allowed to scribble across the layout. */
static void blit_glyph_clipped(canvas_t *c, int gx, int gy,
                               const uint8_t *bits, int w, int h,
                               const value_field_t *f)
{
    if (!bits) return;              /* blank glyph (space): nothing to draw */
    int pitch = (w + 7) / 8;
    int x0 = f->x, x1 = f->x + f->w;
    int y0 = f->y, y1 = f->y + f->h;

    for (int sy = 0; sy < h; sy++) {
        int dy = gy + sy;
        if (dy < y0 || dy >= y1) continue;
        const uint8_t *row = bits + (size_t)sy * pitch;
        for (int sx = 0; sx < w; sx++) {
            if (!((row[sx >> 3] >> (7 - (sx & 7))) & 1)) continue;   /* white: leave it */
            int dx = gx + sx;
            if (dx < x0 || dx >= x1) continue;
            canvas_set_px(c, dx, dy, 1);
        }
    }
}

static void draw_field(canvas_t *c, const value_field_t *f, const char *s)
{
    if (!s || !*s) return;          /* nothing to show: leave the static layer intact */

    font_id_t font = (font_id_t)f->font_id;
    int text_w = 0, line_h = 0;
    if (font_measure(font, s, &text_w, &line_h) != 0) {
        return;                     /* a character with no glyph: draw nothing, do not guess */
    }

    /* The pen origin is the top-left of the line box. Glyph ink is then placed relative to
     * the baseline, which is where the per-glyph bearings come in — placing glyphs by their
     * ink box alone would float a '.' at the top of the line instead of on the baseline. */
    int pen_x = f->x + offset_h(f->align_h, f->w, text_w);
    int pen_y = f->y + offset_v(f->align_v, f->h, line_h);
    int baseline = pen_y + font_ascent(font);

    for (const char *p = s; *p; p++) {
        const uint8_t *bits = NULL;
        int w = 0, h = 0, bx = 0, by = 0;
        if (font_glyph(font, *p, &bits, &w, &h) != 0) break;
        if (font_bearing(font, *p, &bx, &by) != 0) break;
        blit_glyph_clipped(c, pen_x + bx, baseline + by, bits, w, h, f);
        pen_x += font_advance(font, *p);
    }
}

int render_compose_stream(canvas_t *c, render_read_fn read, void *ctx,
                          const value_field_t *fields, const char *const *values,
                          int n_fields)
{
    if (!c || !c->px || !read) return -1;
    if (n_fields < 0) return -1;
    if (n_fields > 0 && (!fields || !values)) return -1;

    /* The static layer IS the base image: the web app already drew all the chrome, labels
     * and units at full panel resolution (FR-1). Copy it in fixed windows so the caller
     * never needs the whole 78,200 bytes resident. */
    uint8_t window[RENDER_CHUNK];
    for (size_t off = 0; off < EPD_FB_BYTES; off += sizeof(window)) {
        size_t n = sizeof(window);
        if (off + n > EPD_FB_BYTES) n = EPD_FB_BYTES - off;
        if (read(ctx, off, window, n) != 0) return -1;
        memcpy(c->px + off, window, n);
    }

    for (int i = 0; i < n_fields; i++) {
        draw_field(c, &fields[i], values[i]);
    }
    return 0;
}

/* Adapter so the in-memory path shares one implementation with the streaming path. */
static int mem_reader(void *ctx, size_t offset, uint8_t *dst, size_t len)
{
    memcpy(dst, (const uint8_t *)ctx + offset, len);
    return 0;
}

int render_compose(canvas_t *c, const uint8_t *static_layer,
                   const value_field_t *fields, const char *const *values,
                   int n_fields)
{
    if (!static_layer) return -1;
    return render_compose_stream(c, mem_reader, (void *)(uintptr_t)static_layer,
                                 fields, values, n_fields);
}
