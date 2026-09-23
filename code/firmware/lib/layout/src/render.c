#include "render.h"
#include "fonts.h"
#include "weather_icons.h"
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
 * allowed to scribble across the layout.
 *
 * AN UPSCALED FACE DRAWS EACH SOURCE PIXEL AS A k x k BLOCK. `bits`, `w` and `h` describe the
 * BASE face's bitmap (every metric from fonts.c is in base pixels — see font_scale() in
 * fonts.h), and every set source bit is painted across the whole k x k block. This is exact for
 * a 1 bpp glyph — there is no grey to interpolate — and it is the same technique
 * draw_line_scaled() in provscreen.c already uses. The block is clipped per DESTINATION pixel,
 * so a scaled glyph that overflows its box is truncated cleanly rather than writing outside it. */
static void blit_glyph_clipped(canvas_t *c, int gx, int gy,
                               const uint8_t *bits, int w, int h, int k,
                               const value_field_t *f)
{
    if (!bits) return;              /* blank glyph (space): nothing to draw */
    if (k < 1) k = 1;

    const int pitch = (w + 7) / 8;
    const int x0 = f->x, x1 = f->x + f->w;
    const int y0 = f->y, y1 = f->y + f->h;

    for (int sy = 0; sy < h; sy++) {
        const uint8_t *row = bits + (size_t)sy * pitch;
        for (int sx = 0; sx < w; sx++) {
            if (!((row[sx >> 3] >> (7 - (sx & 7))) & 1)) continue;   /* white: leave it */
            const int bx = gx + sx * k;
            const int by = gy + sy * k;
            for (int dy = 0; dy < k; dy++) {
                const int py = by + dy;
                if (py < y0 || py >= y1) continue;
                for (int dx = 0; dx < k; dx++) {
                    const int px = bx + dx;
                    if (px < x0 || px >= x1) continue;
                    canvas_set_px(c, px, py, 1);
                }
            }
        }
    }
}

/* Draw a weather icon into its box, painting only ink over the static layer.
 *
 * THE ICON IS SCALED TO FIT ITS BOX rather than drawn at a fixed size: the box is what the user
 * drew, and an icon larger than its box would spill over the surrounding chrome while one much
 * smaller would look stranded. The aspect ratio is preserved by using the smaller of the two box
 * dimensions, and the result is centred per the box's own alignment.
 *
 * ONLY NEAREST-NEIGHBOUR SAMPLING IS USED, because the source is 1 bpp. There is no grey to
 * interpolate: scaling a 1-bit icon with any smoother filter would either blur it into grey
 * (which the panel cannot show) or, at the downscale ratios involved, drop the thin strokes that
 * make a snowflake a snowflake. */
static void draw_icon(canvas_t *c, const value_field_t *f, const char *code)
{
    const int idx = weather_icon_index(code);
    if (idx < 0 || idx >= WEATHER_ICON_COUNT) return;
    const weather_icon_t *ic = &weather_icons[idx];
    if (!ic->bits || ic->w <= 0 || ic->h <= 0) return;

    const int box = (f->w < f->h) ? f->w : f->h;
    if (box <= 0) return;
    const int side = box;                 /* square icons: the set is drawn square */
    const int off_x = f->x + offset_h(f->align_h, f->w, side);
    const int off_y = f->y + offset_v(f->align_v, f->h, side);

    const int src_pitch = (ic->w + 7) / 8;
    for (int dy = 0; dy < side; dy++) {
        const int sy = (int)((long)dy * ic->h / side);
        if (sy < 0 || sy >= ic->h) continue;
        const int py = off_y + dy;
        if (py < f->y || py >= f->y + f->h) continue;   /* clip to the box */
        for (int dx = 0; dx < side; dx++) {
            const int sx = (int)((long)dx * ic->w / side);
            if (sx < 0 || sx >= ic->w) continue;
            if (!((ic->bits[(size_t)sy * src_pitch + (sx >> 3)] >> (7 - (sx & 7))) & 1)) continue;
            const int px = off_x + dx;
            if (px < f->x || px >= f->x + f->w) continue;
            canvas_set_px(c, px, py, 1);
        }
    }
}

static void draw_field(canvas_t *c, const value_field_t *f, const char *s)
{
    if (!s || !*s) return;          /* nothing to show: leave the static layer intact */

    /* An icon box draws a PICTURE from the value, not the string itself: the value is the OWM
     * icon code ("04n"), and a code rendered as text would put "04n" on the glass. */
    if (f->kind == VALUE_KIND_ICON) {
        draw_icon(c, f, s);
        return;
    }

    font_id_t font = (font_id_t)f->font_id;
    int text_w = 0, line_h = 0;
    if (font_measure(font, s, &text_w, &line_h) != 0) {
        return;                     /* a character with no glyph: draw nothing, do not guess */
    }

    /* EVERY metric from fonts.c is in BASE pixels; the face's drawn size is base * k. Scaling
     * them here, in the one place the layout is computed, is what makes an upscaled face behave
     * exactly like a rasterised one — the pen, the line box and the baseline are all in drawn
     * pixels, and the blitter expands each source pixel to match. */
    const int k = font_scale(font);
    text_w *= k;
    line_h *= k;

    /* The pen origin is the top-left of the line box. Glyph ink is then placed relative to
     * the baseline, which is where the per-glyph bearings come in — placing glyphs by their
     * ink box alone would float a '.' at the top of the line instead of on the baseline. */
    int pen_x = f->x + offset_h(f->align_h, f->w, text_w);
    int pen_y = f->y + offset_v(f->align_v, f->h, line_h);
    int baseline = pen_y + font_ascent(font) * k;

    /* Walk by UTF-8 CHARACTER and use the CODEPOINT API.
     *
     * Stepping one byte at a time and passing the lead byte to the char-based API could never
     * find a multi-byte glyph: the char API is one byte, so it would read the NEXT byte in
     * memory looking for a continuation and fail. The degree sign (U+00B0) is two bytes, and
     * "68.4°F" would have drawn as "68.4" with the unit silently missing. */
    const char *p = s;
    while (*p) {
        const unsigned cp = font_utf8_next(&p);

        const uint8_t *bits = NULL;
        int w = 0, h = 0, bx = 0, by = 0;
        if (font_glyph_cp(font, cp, &bits, &w, &h) != 0) break;
        if (font_bearing_cp(font, cp, &bx, &by) != 0) break;
        blit_glyph_clipped(c, pen_x + bx * k, baseline + by * k, bits, w, h, k, f);
        pen_x += font_advance_cp(font, cp) * k;
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

int render_compose_band(canvas_t *c, render_read_fn read, void *ctx,
                        const value_field_t *fields, const char *const *values,
                        int n_fields)
{
    if (!c || !c->px || !read) return -1;
    if (c->h <= 0) return -1;                       /* canvas_init_band with nothing to hold */
    if (n_fields < 0) return -1;
    if (n_fields > 0 && (!fields || !values)) return -1;

    /* Copy the static layer ONE BAND at a time, straight from the caller's reader. Reading the
     * whole 78,200-byte layer and slicing it here would defeat the point: the caller would need a
     * full-frame buffer to read into, which is the memory this function exists to avoid. */
    uint8_t window[RENDER_CHUNK];
    const size_t row_bytes = (size_t)c->h * EPD_PITCH;
    for (size_t off = 0; off < row_bytes; off += sizeof(window)) {
        size_t n = sizeof(window);
        if (off + n > row_bytes) n = row_bytes - off;
        /* The reader serves the WHOLE panel, so the band's first row is at panel offset
         * y_off * EPD_PITCH. A reader that assumed offset 0 would copy the top of the picture into
         * every band, which is exactly the kind of mistake that looks like a renderer bug. */
        if (read(ctx, (size_t)c->y_off * EPD_PITCH + off, window, n) != 0) return -1;
        memcpy(c->px + off, window, n);
    }

    /* Every field is drawn, whatever its y. canvas_set_px() ignores rows outside the band, so a
     * band that contains none of a field's box simply drops it — no clipping logic is duplicated
     * here, and a field straddling a band boundary is drawn correctly by BOTH bands. */
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
