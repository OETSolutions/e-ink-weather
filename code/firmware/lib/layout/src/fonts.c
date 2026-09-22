#include "fonts.h"
#include "atlas.h"      /* the bitmap data — THIS file alone includes it */
#include <stddef.h>

/* The ladder is DATA, so this file has no per-face code. It used to: each of the two faces had
 * its own glyph struct and its own pair of read/find functions, and the glue below was written
 * twice. That is exactly why only two sizes ever shipped — adding one meant editing C, not just
 * re-running a generator. The generator now emits every face with the SAME struct shape, so
 * lookup is an index into ATLAS_FACES and nothing here names a size. */

/* The generated face table, in ladder order. font_id_t's values are the indices into it (the
 * enum is built from the same ATLAS_LADDER list in atlas_ladder.h), so ATLAS_FACES[f] is valid
 * for every f in 0..FONT_COUNT-1 and the two cannot disagree. */
static const atlas_face_t *face_of(font_id_t f)
{
    if (f < 0 || f >= FONT_COUNT) return NULL;
    return &ATLAS_FACES[f];
}

/* The two faces the layout names by ROLE (FONT_BODY / FONT_VALUE) are #defines over ladder
 * entries, so nothing here needs to special-case them. */

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

/* One glyph's metrics, in the common shape. Both the ASCII table and the named-extra table
 * carry exactly these fields — the extra table just leads with a codepoint — so a lookup hands
 * them back uniformly without either table needing a cast to the other's type. */
typedef struct { uint32_t off; uint8_t w, h, advance; int8_t bx, by; } glyph_metrics_t;

/* Look up one glyph's metrics by codepoint. ASCII comes from the dense table indexed by
 * (codepoint - first); anything else from the face's small named-extra table. */
static int metrics_of_cp(font_id_t font, unsigned cp, glyph_metrics_t *out)
{
    const atlas_face_t *f = face_of(font);
    if (!f || !out) return -1;
    if (cp >= (unsigned)ATLAS_FIRST_CHAR &&
        cp < (unsigned)(ATLAS_FIRST_CHAR + ATLAS_GLYPH_COUNT)) {
        const atlas_glyph_t *g = &f->glyphs[cp - ATLAS_FIRST_CHAR];
        *out = (glyph_metrics_t){ g->off, g->w, g->h, g->advance, g->bx, g->by };
        return 0;
    }
    for (int i = 0; i < f->extra_count; i++) {
        const atlas_extra_t *e = &f->extra[i];
        if (e->codepoint == cp) {
            *out = (glyph_metrics_t){ e->off, e->w, e->h, e->advance, e->bx, e->by };
            return 0;
        }
    }
    return -1;
}

int font_glyph(font_id_t font, char ch, const uint8_t **bits, int *w, int *h)
{
    const atlas_face_t *f = face_of(font);
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
    const atlas_face_t *f = face_of(font);
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
    const atlas_face_t *f = face_of(font);
    return f ? f->line_height : 0;
}

int font_ascent(font_id_t font)
{
    const atlas_face_t *f = face_of(font);
    return f ? f->ascent : 0;
}

int font_descent(font_id_t font)
{
    const atlas_face_t *f = face_of(font);
    return f ? f->descent : 0;
}

int font_px(font_id_t font)
{
    const atlas_face_t *f = face_of(font);
    return f ? f->px : 0;
}

/* Nearest ladder face to a requested pixel size, comparing by RATIO. See fonts.h for why
 * nearest-by-ratio rather than a threshold, and why ties go to the smaller face. */
font_id_t font_face_for_px(double px)
{
    if (!(px > 0)) return FONT_BODY;     /* also catches NaN: !(NaN > 0) is true */

    font_id_t best = 0;
    double best_err = 0;
    for (int i = 0; i < FONT_COUNT; i++) {
        const double p = (double)ATLAS_FACES[i].px;
        const double err = (p > px) ? (p / px) : (px / p);
        if (i == 0 || err < best_err) { best = (font_id_t)i; best_err = err; }
        /* Ties go to the smaller face: faces are ascending, so an equal error on a later
         * (larger) face must NOT replace the earlier one. `err < best_err` does that. */
    }
    return best;
}

int font_measure(font_id_t font, const char *s, int *w, int *h)
{
    const atlas_face_t *f = face_of(font);
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
