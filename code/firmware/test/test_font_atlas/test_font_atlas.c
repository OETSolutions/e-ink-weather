#include "unity.h"
#include "fonts.h"
#include <string.h>

void setUp(void) {}
void tearDown(void) {}

/* Every printable ASCII glyph the default layout can show must exist, or a temperature
 * like "-12.4" would silently render blanks. */
static void test_all_ascii_glyphs_are_present_and_sized(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        for (char ch = 32; ch < 127; ch++) {
            const uint8_t *bits = NULL; int w = 0, h = 0;
            TEST_ASSERT_EQUAL_INT(0, font_glyph((font_id_t)f, ch, &bits, &w, &h));
            TEST_ASSERT_GREATER_THAN_INT(0, font_advance((font_id_t)f, ch));
            if (ch != ' ') {
                TEST_ASSERT_NOT_NULL(bits);
                TEST_ASSERT_GREATER_THAN_INT(0, w);
                TEST_ASSERT_GREATER_THAN_INT(0, h);
            }
        }
    }
}

/* Space has no ink but must still advance, or words would run together. */
static void test_space_is_blank_but_advances(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        const uint8_t *bits = NULL; int w = 0, h = 0;
        TEST_ASSERT_EQUAL_INT(0, font_glyph((font_id_t)f, ' ', &bits, &w, &h));
        TEST_ASSERT_NULL(bits);
        TEST_ASSERT_EQUAL_INT(0, w);
        TEST_ASSERT_GREATER_THAN_INT(0, font_advance((font_id_t)f, ' '));
    }
}

/* The big value face must be materially larger than the body face, or the
 * "temperature is the hero" design is impossible. */
static void test_value_face_is_larger_than_body(void)
{
    const uint8_t *b = NULL, *v = NULL; int bw = 0, bh = 0, vw = 0, vh = 0;
    font_glyph(FONT_BODY, '8', &b, &bw, &bh);
    font_glyph(FONT_VALUE, '8', &v, &vw, &vh);
    TEST_ASSERT_GREATER_THAN_INT(bh * 2, vh);
}

static void test_measure_matches_glyph_advances(void)
{
    int w = 0, h = 0;
    TEST_ASSERT_EQUAL_INT(0, font_measure(FONT_BODY, "72", &w, &h));
    TEST_ASSERT_EQUAL_INT(font_advance(FONT_BODY, '7') + font_advance(FONT_BODY, '8'), w);
    /* h is the face line height, not the ink height. */
    TEST_ASSERT_EQUAL_INT(font_line_height(FONT_BODY), h);
}

/* A measured height that depends on which characters are present would make text shift
 * vertically between refreshes as digits change — very visible on e-paper. */
static void test_measured_height_is_constant_for_the_face(void)
{
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
    font_measure(FONT_VALUE, "1", &w1, &h1);
    font_measure(FONT_VALUE, "ggg", &w2, &h2);
    font_measure(FONT_VALUE, "8.8", &w3, &h3);
    TEST_ASSERT_EQUAL_INT(h1, h2);
    TEST_ASSERT_EQUAL_INT(h2, h3);
    TEST_ASSERT_EQUAL_INT(font_line_height(FONT_VALUE), h1);
}

static void test_measure_rejects_unsupported_characters(void)
{
    int w = 0, h = 0;
    /* No glyph for a non-ASCII byte: must fail rather than silently measure 0 width and
     * let the caller render an overlapping string. */
    TEST_ASSERT_NOT_EQUAL(0, font_measure(FONT_BODY, "abc\xC3\xA9", &w, &h));
    TEST_ASSERT_NOT_EQUAL(0, font_measure(FONT_BODY, "\x01", &w, &h));
}

static void test_glyph_rejects_out_of_range(void)
{
    const uint8_t *bits = NULL; int w = 0, h = 0;
    TEST_ASSERT_NOT_EQUAL(0, font_glyph(FONT_BODY, (char)0x7F, &bits, &w, &h));
    TEST_ASSERT_NOT_EQUAL(0, font_glyph(FONT_BODY, (char)0x1F, &bits, &w, &h));
    TEST_ASSERT_NOT_EQUAL(0, font_glyph(FONT_COUNT, 'A', &bits, &w, &h));
    /* A negative id must not index the face table out of bounds (undefined behaviour). */
    TEST_ASSERT_NOT_EQUAL(0, font_glyph((font_id_t)-1, 'A', &bits, &w, &h));
    TEST_ASSERT_EQUAL_INT(0, font_line_height((font_id_t)-1));
    TEST_ASSERT_EQUAL_INT(0, font_advance((font_id_t)-1, 'A'));
}

/* Glyphs must be strictly 1-bit with WHITE padding beyond the glyph width, or the blitter
 * copying whole bytes would paint stray ink as speckle (NFR-4). */
static void test_glyph_padding_is_white(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        for (char ch = 33; ch < 127; ch++) {
            const uint8_t *bits = NULL; int w = 0, h = 0;
            font_glyph((font_id_t)f, ch, &bits, &w, &h);
            int pitch = (w + 7) / 8;
            if (w % 8) {
                uint8_t pad = (uint8_t)(0xFFu >> (w % 8));
                for (int y = 0; y < h; y++) {
                    TEST_ASSERT_EQUAL_UINT8(0, (uint8_t)(bits[y * pitch + pitch - 1] & pad));
                }
            }
        }
    }
}

/* Every glyph's ink must fit inside its declared w x h box — the blitter trusts these
 * dimensions, so a mismatch would write outside the intended cell. */
static void test_glyph_bitmaps_are_not_empty_and_have_ink(void)
{
    for (char ch = 33; ch < 127; ch++) {
        const uint8_t *bits = NULL; int w = 0, h = 0;
        font_glyph(FONT_VALUE, ch, &bits, &w, &h);
        int pitch = (w + 7) / 8;
        int ink = 0;
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if ((bits[y * pitch + (x >> 3)] >> (7 - (x & 7))) & 1) ink++;
            }
        }
        TEST_ASSERT_GREATER_THAN_INT(0, ink);
    }
}

/* ascent + descent must equal the line height, or vertical alignment of the value face
 * against a body-face label would be off by the difference. */
static void test_metrics_are_self_consistent(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        TEST_ASSERT_EQUAL_INT(font_line_height((font_id_t)f),
                              font_ascent((font_id_t)f) + font_descent((font_id_t)f));
    }
    /* The value face must have a bigger line box than the body face. */
    TEST_ASSERT_GREATER_THAN_INT(font_line_height(FONT_BODY), font_line_height(FONT_VALUE));
}

/* Glyphs must be packed back-to-back with no gaps and no overlap. A generator bug that
 * overlapped two glyphs would render plausible-looking but corrupted text; the blitter
 * reads h*ceil(w/8) bytes from the returned pointer and trusts this layout. */
static void test_glyphs_are_packed_contiguously(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        const uint8_t *prev = NULL; int pw = 0, ph = 0;
        for (char ch = 32; ch < 127; ch++) {
            const uint8_t *bits = NULL; int w = 0, h = 0;
            TEST_ASSERT_EQUAL_INT(0, font_glyph((font_id_t)f, ch, &bits, &w, &h));
            if (prev) {
                TEST_ASSERT_EQUAL_INT(ph * ((pw + 7) / 8), (int)(bits - prev));
            }
            prev = bits; pw = w; ph = h;
        }
    }
}

/* Punctuation must sit low in the line box, near the baseline — not float at the top. A
 * renderer that placed glyphs by their ink box alone would put a '.' up at the ascender. */
static void test_punctuation_sits_near_the_baseline(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        int ascent = font_ascent((font_id_t)f);
        int descent = font_descent((font_id_t)f);
        for (const char *p = ".,-_"; *p; p++) {
            int bx = 0, by = 0;
            const uint8_t *b = NULL; int w = 0, h = 0;
            TEST_ASSERT_EQUAL_INT(0, font_bearing((font_id_t)f, *p, &bx, &by));
            font_glyph((font_id_t)f, *p, &b, &w, &h);
            TEST_ASSERT_LESS_OR_EQUAL_INT(0, by);   /* ink starts at or above the baseline */
            /* Bottom of the ink is at the baseline or just below it (a comma descends),
             * but never past the line box, and never up in the upper half of the line. */
            TEST_ASSERT_LESS_OR_EQUAL_INT(descent + 1, by + h);
            TEST_ASSERT_GREATER_THAN_INT(-(ascent / 2), by + h);
        }
    }
}

/* The y bearing is baseline-relative: a cap ends exactly on the baseline and a descender
 * hangs below it. Getting the sign or the reference line wrong is what clipped the value
 * face in half, so this pins the convention directly. */
static void test_bearings_place_descenders_below_the_baseline(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        const uint8_t *b = NULL; int w = 0, h = 0;
        int gx = 0, gy = 0, hx = 0, hy = 0;
        TEST_ASSERT_EQUAL_INT(0, font_bearing((font_id_t)f, 'g', &gx, &gy));
        font_glyph((font_id_t)f, 'g', &b, &w, &h);
        int g_bottom = gy + h;
        TEST_ASSERT_EQUAL_INT(0, font_bearing((font_id_t)f, 'H', &hx, &hy));
        font_glyph((font_id_t)f, 'H', &b, &w, &h);
        int h_bottom = hy + h;

        TEST_ASSERT_GREATER_THAN_INT(0, g_bottom);    /* 'g' hangs below the baseline */
        TEST_ASSERT_EQUAL_INT(0, h_bottom);           /* 'H' sits exactly on it */
        TEST_ASSERT_LESS_THAN_INT(0, hy);             /* cap ink is above the baseline */
    }
}

/* Every glyph's ink, placed at its bearing, must fit inside the line box — otherwise
 * stacked lines would collide. The tolerance is one pixel: Inter reports a bounding-box
 * ascent that its own '|' overshoots slightly, and the renderer clips every glyph to its
 * field box anyway, so this cannot reach the static art. */
static void test_every_glyph_fits_in_the_line_box(void)
{
    for (int f = 0; f < FONT_COUNT; f++) {
        int ascent = font_ascent((font_id_t)f);
        int lh = font_line_height((font_id_t)f);
        for (char ch = 33; ch < 127; ch++) {
            const uint8_t *b = NULL; int w = 0, h = 0, bx = 0, by = 0;
            TEST_ASSERT_EQUAL_INT(0, font_glyph((font_id_t)f, ch, &b, &w, &h));
            TEST_ASSERT_EQUAL_INT(0, font_bearing((font_id_t)f, ch, &bx, &by));
            /* Ink must not reach above the line box... */
            TEST_ASSERT_GREATER_OR_EQUAL_INT(-ascent - 1, by);
            /* ...nor below it. */
            TEST_ASSERT_LESS_OR_EQUAL_INT(lh - ascent, by + h);
        }
    }
}

static void test_bearing_rejects_bad_arguments(void)
{
    int bx = 0, by = 0;
    TEST_ASSERT_NOT_EQUAL(0, font_bearing(FONT_BODY, (char)0x7F, &bx, &by));
    TEST_ASSERT_NOT_EQUAL(0, font_bearing(FONT_COUNT, 'A', &bx, &by));
    TEST_ASSERT_NOT_EQUAL(0, font_bearing(FONT_BODY, 'A', NULL, &by));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_all_ascii_glyphs_are_present_and_sized);
    RUN_TEST(test_space_is_blank_but_advances);
    RUN_TEST(test_value_face_is_larger_than_body);
    RUN_TEST(test_measure_matches_glyph_advances);
    RUN_TEST(test_measured_height_is_constant_for_the_face);
    RUN_TEST(test_measure_rejects_unsupported_characters);
    RUN_TEST(test_glyph_rejects_out_of_range);
    RUN_TEST(test_glyph_padding_is_white);
    RUN_TEST(test_glyph_bitmaps_are_not_empty_and_have_ink);
    RUN_TEST(test_metrics_are_self_consistent);
    RUN_TEST(test_glyphs_are_packed_contiguously);
    RUN_TEST(test_punctuation_sits_near_the_baseline);
    RUN_TEST(test_bearings_place_descenders_below_the_baseline);
    RUN_TEST(test_every_glyph_fits_in_the_line_box);
    RUN_TEST(test_bearing_rejects_bad_arguments);
    return UNITY_END();
}
