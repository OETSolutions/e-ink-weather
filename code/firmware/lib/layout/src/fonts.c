#include "fonts.h"
#include "atlas_body.h"
#include "atlas_value.h"
#include <stddef.h>

/* The two generated atlases have identical layouts but different type names, so the glue
 * below is written twice rather than macro-templated: there are exactly two faces, and an
 * X-macro would be harder to read than the duplication it removes. */

typedef struct {
    uint32_t off;
    int      w, h, advance, bx, by;
} glyph_metrics_t;

typedef struct {
    const void  *glyphs;    /* array of the face's own glyph struct */
    const uint8_t *bits;
    int  first_char;
    int  glyph_count;
    int  px;
    int  ascent;
    int  descent;
    int  line_height;
    /* Non-ASCII glyphs, indexed from the END of the ASCII table (see the generated header).
     * NULL when the face has none. */
    const void  *extra;
    int  extra_count;
    /* Look one up by codepoint. Returns 0 and fills `out` when found. */
    int (*find_extra)(const void *extra, int count, unsigned codepoint, glyph_metrics_t *out);
    /* Copy the generated struct into the common shape. Two faces, two one-liners — this
     * is the only place that needs to know the two struct types differ. */
    void (*read)(const void *glyphs, int idx, glyph_metrics_t *out);
} face_t;

static void read_body(const void *g, int idx, glyph_metrics_t *o)
{
    const font_body_glyph_t *s = &((const font_body_glyph_t *)g)[idx];
    *o = (glyph_metrics_t){ s->off, s->w, s->h, s->advance, s->bx, s->by };
}

static void read_value(const void *g, int idx, glyph_metrics_t *o)
{
    const font_value_glyph_t *s = &((const font_value_glyph_t *)g)[idx];
    *o = (glyph_metrics_t){ s->off, s->w, s->h, s->advance, s->bx, s->by };
}

static int find_extra_body(const void *e, int count, unsigned cp, glyph_metrics_t *o)
{
    const font_body_extra_t *t = (const font_body_extra_t *)e;
    for (int i = 0; i < count; i++) {
        if (t[i].codepoint == cp) {
            *o = (glyph_metrics_t){ t[i].off, t[i].w, t[i].h, t[i].advance, t[i].bx, t[i].by };
            return 0;
        }
    }
    return -1;
}

static int find_extra_value(const void *e, int count, unsigned cp, glyph_metrics_t *o)
{
    const font_value_extra_t *t = (const font_value_extra_t *)e;
    for (int i = 0; i < count; i++) {
        if (t[i].codepoint == cp) {
            *o = (glyph_metrics_t){ t[i].off, t[i].w, t[i].h, t[i].advance, t[i].bx, t[i].by };
            return 0;
        }
    }
    return -1;
}

/* Ascent/descent/line-height come from the generated atlas, which takes them from the
 * face's own metrics (Pillow getmetrics()), NOT from the tallest glyph's ink box. Using
 * ink height instead would make a line's measured height depend on which characters happen
 * to be in it, so a temperature line would shift vertically as digits changed between
 * refreshes. Sourcing them from the atlas rather than hardcoding here means re-running the
 * generator at a different --px cannot leave these stale. */
static const face_t FACES[FONT_COUNT] = {
    [FONT_BODY] = {
        .glyphs = FONT_BODY_GLYPHS, .bits = FONT_BODY_BITS,
        .first_char = FONT_BODY_FIRST_CHAR, .glyph_count = FONT_BODY_GLYPH_COUNT,
        .px = FONT_BODY_PX, .ascent = FONT_BODY_ASCENT, .descent = FONT_BODY_DESCENT,
        .line_height = FONT_BODY_LINE_HEIGHT, .read = read_body,
        .extra = FONT_BODY_EXTRA, .extra_count = FONT_BODY_EXTRA_COUNT,
        .find_extra = find_extra_body,
    },
    [FONT_VALUE] = {
        .glyphs = FONT_VALUE_GLYPHS, .bits = FONT_VALUE_BITS,
        .first_char = FONT_VALUE_FIRST_CHAR, .glyph_count = FONT_VALUE_GLYPH_COUNT,
        .px = FONT_VALUE_PX, .ascent = FONT_VALUE_ASCENT, .descent = FONT_VALUE_DESCENT,
        .line_height = FONT_VALUE_LINE_HEIGHT, .read = read_value,
        .extra = FONT_VALUE_EXTRA, .extra_count = FONT_VALUE_EXTRA_COUNT,
        .find_extra = find_extra_value,
    },
};

static const face_t *face_of(font_id_t f)
{
    if (f < 0 || f >= FONT_COUNT) return NULL;
    return &FACES[f];
}

/* UTF-8 decode of the next character. Returns the codepoint and advances *p past it; on a
 * malformed sequence it consumes ONE byte and returns that byte's value, so a stray byte
 * cannot desynchronise the whole string.
 *
 * WHY THIS EXISTS AT ALL: the atlas covers printable ASCII plus a few named extras, and the
 * degree sign is U+00B0 — two bytes in UTF-8. Comparing bytes would never match it, and the
 * failure is not cosmetic: font_measure() would fail for "68.4°F" and the renderer would draw
 * NOTHING, leaving a blank where a temperature belongs. */
unsigned font_utf8_next(const char **p)
{
    const unsigned char c0 = (unsigned char)**p;
    if (c0 < 0x80) { (*p)++; return c0; }

    int n = 0;
    unsigned cp = 0;
    if ((c0 & 0xE0) == 0xC0) { n = 1; cp = c0 & 0x1Fu; }
    else if ((c0 & 0xF0) == 0xE0) { n = 2; cp = c0 & 0x0Fu; }
    else if ((c0 & 0xF8) == 0xF0) { n = 3; cp = c0 & 0x07u; }
    else { (*p)++; return c0; }             /* continuation byte or invalid lead */

    for (int i = 1; i <= n; i++) {
        const unsigned char ci = (unsigned char)(*p)[i];
        if ((ci & 0xC0) != 0x80) { (*p)++; return c0; }   /* truncated: consume one byte */
        cp = (cp << 6) | (ci & 0x3Fu);
    }
    *p += n + 1;
    return cp;
}

static int metrics_of_cp(font_id_t font, unsigned cp, glyph_metrics_t *out)
{
    const face_t *f = face_of(font);
    if (!f || !out) return -1;
    if (cp >= (unsigned)f->first_char && cp < (unsigned)(f->first_char + f->glyph_count)) {
        f->read(f->glyphs, (int)cp - f->first_char, out);
        return 0;
    }
    if (f->extra && f->find_extra) {
        return f->find_extra(f->extra, f->extra_count, cp, out);
    }
    return -1;
}

int font_glyph(font_id_t font, char ch, const uint8_t **bits, int *w, int *h)
{
    const face_t *f = face_of(font);
    if (!f || !bits || !w || !h) return -1;
    *bits = NULL; *w = 0; *h = 0;

    const char *p = &ch;
    unsigned cp = font_utf8_next(&p);

    glyph_metrics_t m;
    if (metrics_of_cp(font, cp, &m) != 0) return -1;
    *w = m.w; *h = m.h;
    if (m.w) *bits = f->bits + m.off;
    return 0;
}

int font_glyph_cp(font_id_t font, unsigned codepoint, const uint8_t **bits, int *w, int *h)
{
    const face_t *f = face_of(font);
    if (!f || !bits || !w || !h) return -1;
    *bits = NULL; *w = 0; *h = 0;

    glyph_metrics_t m;
    if (metrics_of_cp(font, codepoint, &m) != 0) return -1;
    *w = m.w; *h = m.h;
    if (m.w) *bits = f->bits + m.off;
    return 0;
}

int font_advance_cp(font_id_t font, unsigned codepoint)
{
    glyph_metrics_t m;
    if (metrics_of_cp(font, codepoint, &m) != 0) return 0;
    return m.advance;
}

int font_bearing_cp(font_id_t font, unsigned codepoint, int *bx, int *by)
{
    if (!bx || !by) return -1;
    glyph_metrics_t m;
    if (metrics_of_cp(font, codepoint, &m) != 0) return -1;
    *bx = m.bx; *by = m.by;
    return 0;
}

int font_advance(font_id_t font, char ch)
{
    const char *p = &ch;
    unsigned cp = font_utf8_next(&p);
    glyph_metrics_t m;
    if (metrics_of_cp(font, cp, &m) != 0) return 0;
    return m.advance;
}

int font_bearing(font_id_t font, char ch, int *bx, int *by)
{
    if (!bx || !by) return -1;
    const char *p = &ch;
    unsigned cp = font_utf8_next(&p);
    glyph_metrics_t m;
    if (metrics_of_cp(font, cp, &m) != 0) return -1;
    *bx = m.bx; *by = m.by;
    return 0;
}

int font_line_height(font_id_t font)
{
    const face_t *f = face_of(font);
    return f ? f->line_height : 0;
}

int font_ascent(font_id_t font)
{
    const face_t *f = face_of(font);
    return f ? f->ascent : 0;
}

int font_descent(font_id_t font)
{
    const face_t *f = face_of(font);
    return f ? f->descent : 0;
}

int font_measure(font_id_t font, const char *s, int *w, int *h)
{
    const face_t *f = face_of(font);
    if (!f || !s || !w || !h) return -1;

    int total = 0;
    const char *p = s;
    while (*p) {
        unsigned cp = font_utf8_next(&p);
        glyph_metrics_t m;
        if (metrics_of_cp(font, cp, &m) != 0) return -1;   /* no glyph: do not guess a width */
        total += m.advance;
    }
    *w = total;
    *h = f->line_height;    /* constant for the face, so lines do not jitter */
    return 0;
}
