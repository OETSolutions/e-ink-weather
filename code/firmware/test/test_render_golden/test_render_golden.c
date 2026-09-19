#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "canvas.h"
#include "render.h"
#include "fonts.h"
#include "default_layout.h"   /* shared layout spec: tools/ is on the test include path */

static uint8_t *fb;
static uint8_t *static_layer;

void setUp(void)
{
    fb = malloc(EPD_FB_BYTES);
    static_layer = malloc(EPD_FB_BYTES);
    memset(static_layer, 0xFF, EPD_FB_BYTES);   /* white background */
}
void tearDown(void) { free(fb); free(static_layer); }

static int px(const uint8_t *buf, int x, int y)
{
    return (buf[(size_t)y * EPD_PITCH + (size_t)(x >> 3)] >> (7 - (x & 7))) & 1;
}

/* Count ink bits (0) inside a rectangle. */
static int ink_in(const uint8_t *buf, int x0, int y0, int x1, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            if (!px(buf, x, y)) n++;
    return n;
}

/* Blitting the static layer alone must reproduce it exactly. */
static void test_static_layer_is_blitted_verbatim(void)
{
    canvas_t c;
    canvas_init(&c, fb);
    TEST_ASSERT_EQUAL_INT(0, render_compose(&c, static_layer, NULL, NULL, 0));
    TEST_ASSERT_EQUAL_MEMORY(static_layer, fb, EPD_FB_BYTES);
}

/* A dynamic field must change the bitmap, and only inside its own box. */
static void test_value_field_draws_inside_its_box(void)
{
    value_field_t f = { .x = 100, .y = 100, .w = 200, .h = 80,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    const char *values[1] = { "72" };

    canvas_t c;
    canvas_init(&c, fb);
    TEST_ASSERT_EQUAL_INT(0, render_compose(&c, static_layer, &f, values, 1));

    TEST_ASSERT_TRUE(memcmp(static_layer, fb, EPD_FB_BYTES) != 0);   /* it drew something */

    /* Outside the field box, every bit must be identical to the static layer. */
    for (int y = 0; y < EPD_HEIGHT; y++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            if (y >= 100 && y < 180 && x >= 100 && x < 300) continue;
            size_t idx = (size_t)y * EPD_PITCH + (size_t)(x >> 3);
            uint8_t mask = (uint8_t)(0x80u >> (x & 7));
            TEST_ASSERT_EQUAL_INT((static_layer[idx] & mask) ? 1 : 0,
                                  (fb[idx] & mask) ? 1 : 0);
        }
    }
}

/* The static layer carries the labels and chrome. If the renderer painted each glyph's box
 * as solid white it would punch a rectangle out of that art — exactly the bug the
 * transparent-white blit exists to prevent.
 *
 * Test: cover the whole field box with solid black static ink, render, and require the ink
 * count to be UNCHANGED. Glyph ink is also black, so nothing can be added; any white-painted
 * pixel would make the count drop. */
static void test_transparent_white_never_erases_static_art(void)
{
    memset(static_layer, 0xFF, EPD_FB_BYTES);
    memset(static_layer + (size_t)100 * EPD_PITCH, 0x00, (size_t)80 * EPD_PITCH);

    value_field_t f = { .x = 100, .y = 100, .w = 200, .h = 80,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    const char *values[1] = { "72" };

    int before = ink_in(static_layer, 100, 100, 300, 180);
    TEST_ASSERT_EQUAL_INT(200 * 80, before);      /* the bar really is solid */

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, values, 1);

    TEST_ASSERT_EQUAL_INT(before, ink_in(fb, 100, 100, 300, 180));
}

/* A glyph's blank pixels must not paint white over static ink either. Put a single black
 * pixel in the box's top-left corner — inside the line-box leading above the glyph ink, so
 * no glyph can legitimately cover it — and require it to survive. */
static void test_glyph_leading_does_not_erase_static_art(void)
{
    memset(static_layer, 0xFF, EPD_FB_BYTES);
    canvas_t tmp; canvas_init(&tmp, static_layer);
    canvas_set_px(&tmp, 100, 100, 1);   /* black pixel at the box corner */

    value_field_t f = { .x = 100, .y = 100, .w = 200, .h = 80,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    const char *values[1] = { "72" };

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, values, 1);
    TEST_ASSERT_EQUAL_INT(0, px(fb, 100, 100));   /* 0 == black == still there */
}

/* An over-long value must be clipped to the box, not scribble across the layout. */
static void test_overlong_value_is_clipped_to_the_box(void)
{
    value_field_t f = { .x = 200, .y = 200, .w = 120, .h = 90,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    /* Far wider than 120 px. */
    const char *values[1] = { "12345678901234567890" };

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, values, 1);

    /* Every bit outside the box must be untouched. */
    for (int y = 0; y < EPD_HEIGHT; y++) {
        for (int x = 0; x < EPD_WIDTH; x++) {
            if (y >= 200 && y < 290 && x >= 200 && x < 320) continue;
            TEST_ASSERT_EQUAL_INT(px(static_layer, x, y), px(fb, x, y));
        }
    }
    TEST_ASSERT_GREATER_THAN_INT(0, ink_in(fb, 200, 200, 320, 290));  /* it did draw */
}

/* A box that runs off the panel edge must not corrupt memory or wrap to the next row. */
static void test_field_at_panel_edge_is_clipped(void)
{
    value_field_t f = { .x = EPD_WIDTH - 40, .y = EPD_HEIGHT - 120, .w = 200, .h = 120,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    const char *values[1] = { "88" };

    canvas_t c;
    canvas_init(&c, fb);
    TEST_ASSERT_EQUAL_INT(0, render_compose(&c, static_layer, &f, values, 1));
    /* It did draw, clipped at the right edge... */
    TEST_ASSERT_GREATER_THAN_INT(0, ink_in(fb, EPD_WIDTH - 40, EPD_HEIGHT - 120,
                                           EPD_WIDTH, EPD_HEIGHT));
    /* ...and did not wrap: the top-left of the panel is untouched. */
    TEST_ASSERT_EQUAL_INT(px(static_layer, 0, 0), px(fb, 0, 0));
    TEST_ASSERT_EQUAL_INT(px(static_layer, 5, 0), px(fb, 5, 0));
}

/* Alignment must actually move the text: three alignments of the same string in the same
 * box must produce three different bitmaps, each inside the box. */
static void test_alignment_moves_the_text(void)
{
    const char *values[1] = { "42" };
    uint8_t *out[3];
    char aligns[3] = { 'L', 'C', 'R' };

    for (int i = 0; i < 3; i++) {
        value_field_t f = { .x = 300, .y = 300, .w = 300, .h = 100,
                            .align_h = aligns[i], .align_v = 'M', .font_id = FONT_VALUE };
        out[i] = malloc(EPD_FB_BYTES);
        canvas_t c;
        canvas_init(&c, out[i]);
        render_compose(&c, static_layer, &f, values, 1);
        TEST_ASSERT_GREATER_THAN_INT(0, ink_in(out[i], 300, 300, 600, 400));
    }
    TEST_ASSERT_TRUE(memcmp(out[0], out[1], EPD_FB_BYTES) != 0);
    TEST_ASSERT_TRUE(memcmp(out[1], out[2], EPD_FB_BYTES) != 0);
    TEST_ASSERT_TRUE(memcmp(out[0], out[2], EPD_FB_BYTES) != 0);

    /* Left-aligned ink must start left of right-aligned ink. */
    int first_l = -1, first_r = -1;
    for (int x = 300; x < 600; x++) {
        if (first_l < 0 && ink_in(out[0], x, 300, x + 1, 400) > 0) first_l = x;
        if (first_r < 0 && ink_in(out[2], x, 300, x + 1, 400) > 0) first_r = x;
    }
    TEST_ASSERT_LESS_THAN_INT(first_r, first_l);

    for (int i = 0; i < 3; i++) free(out[i]);
}

/* Vertical alignment: 'B' must put ink lower than 'T'. */
static void test_vertical_alignment_moves_the_text(void)
{
    uint8_t *out[2];
    char aligns[2] = { 'T', 'B' };
    const char *values[1] = { "42" };

    for (int i = 0; i < 2; i++) {
        value_field_t f = { .x = 300, .y = 300, .w = 300, .h = 150,
                            .align_h = 'L', .align_v = aligns[i], .font_id = FONT_VALUE };
        out[i] = malloc(EPD_FB_BYTES);
        canvas_t c;
        canvas_init(&c, out[i]);
        render_compose(&c, static_layer, &f, values, 1);
    }
    int top_t = -1, top_b = -1;
    for (int y = 300; y < 450; y++) {
        if (top_t < 0 && ink_in(out[0], 300, y, 600, y + 1) > 0) top_t = y;
        if (top_b < 0 && ink_in(out[1], 300, y, 600, y + 1) > 0) top_b = y;
    }
    TEST_ASSERT_LESS_THAN_INT(top_b, top_t);
    free(out[0]); free(out[1]);
}

/* An unrecognised align char must fall back to top-left rather than drawing nothing: a
 * config from a newer web app must still show the reading. */
static void test_unknown_alignment_falls_back(void)
{
    uint8_t *a = malloc(EPD_FB_BYTES), *b = malloc(EPD_FB_BYTES);
    const char *values[1] = { "42" };
    value_field_t fa = { .x = 300, .y = 300, .w = 300, .h = 150,
                         .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    value_field_t fb2 = fa; fb2.align_h = 'Z'; fb2.align_v = 'Z';

    canvas_t ca, cb;
    canvas_init(&ca, a); render_compose(&ca, static_layer, &fa, values, 1);
    canvas_init(&cb, b); render_compose(&cb, static_layer, &fb2, values, 1);
    TEST_ASSERT_EQUAL_MEMORY(a, b, EPD_FB_BYTES);
    free(a); free(b);
}

/* An empty value must leave the static layer completely intact — not erase the field box. */
static void test_empty_value_leaves_the_static_layer_intact(void)
{
    value_field_t f = { .x = 100, .y = 100, .w = 200, .h = 80,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    const char *values[2] = { "", NULL };

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, values, 1);
    TEST_ASSERT_EQUAL_MEMORY(static_layer, fb, EPD_FB_BYTES);

    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, &values[1], 1);
    TEST_ASSERT_EQUAL_MEMORY(static_layer, fb, EPD_FB_BYTES);
}

/* A value with a character the atlas cannot render must draw nothing for that field rather
 * than a half-string or a guessed width. */
static void test_unrenderable_value_draws_nothing(void)
{
    value_field_t f = { .x = 100, .y = 100, .w = 200, .h = 80,
                        .align_h = 'L', .align_v = 'T', .font_id = FONT_VALUE };
    const char *values[1] = { "\xC3\xA9" };   /* é: not in the ASCII atlas */

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, &f, values, 1);
    TEST_ASSERT_EQUAL_MEMORY(static_layer, fb, EPD_FB_BYTES);
}

/* Fields are independent: one empty field must not stop a later one from drawing. */
static void test_fields_are_independent(void)
{
    value_field_t fields[3] = {
        { .x = 40,  .y = 40,  .w = 300, .h = 100, .align_h='L', .align_v='T', .font_id = FONT_VALUE },
        { .x = 40,  .y = 200, .w = 300, .h = 100, .align_h='L', .align_v='T', .font_id = FONT_VALUE },
        { .x = 40,  .y = 360, .w = 300, .h = 100, .align_h='L', .align_v='T', .font_id = FONT_VALUE },
    };
    const char *values[3] = { "", "41.2", "" };

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, fields, values, 3);

    TEST_ASSERT_GREATER_THAN_INT(0, ink_in(fb, 40, 200, 340, 300));
    TEST_ASSERT_EQUAL_INT(0, ink_in(fb, 40, 40, 340, 140));
    TEST_ASSERT_EQUAL_INT(0, ink_in(fb, 40, 360, 340, 460));
}

static void test_bad_arguments_are_rejected(void)
{
    canvas_t c;
    canvas_init(&c, fb);
    value_field_t f = { .x = 0, .y = 0, .w = 10, .h = 10,
                        .align_h='L', .align_v='T', .font_id = FONT_BODY };
    const char *values[1] = { "1" };

    TEST_ASSERT_NOT_EQUAL(0, render_compose(NULL, static_layer, NULL, NULL, 0));
    TEST_ASSERT_NOT_EQUAL(0, render_compose(&c, NULL, NULL, NULL, 0));
    TEST_ASSERT_NOT_EQUAL(0, render_compose(&c, static_layer, NULL, NULL, 1));
    TEST_ASSERT_NOT_EQUAL(0, render_compose(&c, static_layer, &f, NULL, 1));
    TEST_ASSERT_NOT_EQUAL(0, render_compose(&c, static_layer, &f, values, -1));
}

/* Golden image: a pinned config + pinned values => an exact byte-for-byte result.
 * Regenerate ONLY with a deliberate, reviewed reason (NFR-9).
 *
 * The layout spec is included from tools/golden/default_layout.h, which the generator also
 * uses — if these were duplicated the golden could lock a layout nothing actually renders. */
static void test_golden_image_of_default_layout(void)
{
    FILE *f = fopen("tools/golden/default_layout.bin", "rb");
    if (!f) { TEST_IGNORE_MESSAGE("golden image not generated yet"); return; }
    uint8_t golden[EPD_FB_BYTES];
    size_t got = fread(golden, 1, EPD_FB_BYTES, f);
    fclose(f);
    TEST_ASSERT_EQUAL_INT(EPD_FB_BYTES, (int)got);

    golden_build_static_layer(static_layer);

    canvas_t c;
    canvas_init(&c, fb);
    render_compose(&c, static_layer, GOLDEN_FIELDS, GOLDEN_VALUES, GOLDEN_FIELD_COUNT);
    TEST_ASSERT_EQUAL_MEMORY(golden, fb, EPD_FB_BYTES);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_static_layer_is_blitted_verbatim);
    RUN_TEST(test_value_field_draws_inside_its_box);
    RUN_TEST(test_transparent_white_never_erases_static_art);
    RUN_TEST(test_overlong_value_is_clipped_to_the_box);
    RUN_TEST(test_field_at_panel_edge_is_clipped);
    RUN_TEST(test_alignment_moves_the_text);
    RUN_TEST(test_vertical_alignment_moves_the_text);
    RUN_TEST(test_unknown_alignment_falls_back);
    RUN_TEST(test_empty_value_leaves_the_static_layer_intact);
    RUN_TEST(test_unrenderable_value_draws_nothing);
    RUN_TEST(test_fields_are_independent);
    RUN_TEST(test_bad_arguments_are_rejected);
    RUN_TEST(test_golden_image_of_default_layout);
    return UNITY_END();
}
