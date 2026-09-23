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

/* A render_read_fn over an in-memory static layer, for the band tests. The library's own
 * equivalent is static (it is an implementation detail of render_compose), so the test supplies
 * its own — which is also the stronger check, since it exercises the reader contract from outside
 * the library rather than through it. */
static int mem_reader_static(void *ctx, size_t offset, uint8_t *dst, size_t len)
{
    memcpy(dst, (const uint8_t *)ctx + offset, len);
    return 0;
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

/* ---- band composition: what makes the partial-refresh path possible ----
 *
 * `epd_write_frame_partial(prev, next)` needs BOTH frames at once, and two 78,200-byte frames do
 * not fit this part's DRAM (measured: the only region big enough for one is 113,840 B; the pair
 * needs 156,400). So the previous and next frames are composed a band of rows at a time, into two
 * small buffers. That is only correct if a band render is BYTE-IDENTICAL to the whole-frame render
 * for the rows it covers — otherwise a partial would push a subtly different picture than a full
 * would, and the panel would accumulate the difference as ghosting.
 *
 * These tests pin that equivalence across several band heights, including heights that do not
 * divide the panel evenly and bands that split a field box.
 */
static void test_a_band_matches_the_whole_frame_render(void)
{
    /* A range of band heights, deliberately including awkward ones: 1 row, a prime, and more than
     * half the panel. A band taller than the panel is also covered, because a caller that sizes a
     * band from a config value could produce one. */
    static const int heights[] = { 1, 7, 64, 128, 337, EPD_HEIGHT, EPD_HEIGHT + 50 };

    for (unsigned k = 0; k < sizeof(heights) / sizeof(heights[0]); k++) {
        const int bh = heights[k];

        canvas_t whole;
        canvas_init(&whole, fb);
        TEST_ASSERT_EQUAL_INT(0, render_compose(&whole, static_layer,
                                                GOLDEN_FIELDS, GOLDEN_VALUES,
                                                GOLDEN_FIELD_COUNT));

        /* Walk the panel in bands of `bh` rows and rebuild the frame. */
        uint8_t *band = malloc(EPD_FB_BYTES);
        TEST_ASSERT_NOT_NULL(band);
        uint8_t *assembled = malloc(EPD_FB_BYTES);
        TEST_ASSERT_NOT_NULL(assembled);

        for (int y0 = 0; y0 < EPD_HEIGHT; y0 += bh) {
            int rows = bh;
            if (y0 + rows > EPD_HEIGHT) rows = EPD_HEIGHT - y0;

            canvas_t b;
            canvas_init_band(&b, band, y0, rows);
            TEST_ASSERT_EQUAL_INT(0, render_compose_band(&b, mem_reader_static, static_layer,
                                                         GOLDEN_FIELDS, GOLDEN_VALUES,
                                                         GOLDEN_FIELD_COUNT));
            memcpy(assembled + (size_t)y0 * EPD_PITCH, band, (size_t)rows * EPD_PITCH);
        }

        TEST_ASSERT_EQUAL_MEMORY_MESSAGE(fb, assembled, EPD_FB_BYTES,
                                         "band render differs from the whole-frame render");
        free(band);
        free(assembled);
    }
}

/* A band that holds NO part of any field must be pure static layer — the failure this rules out is
 * a band renderer that stamps a field into every band (e.g. by treating y as band-relative). */
static void test_a_band_with_no_fields_is_pure_static_layer(void)
{
    /* Find a band of rows that contains no field box at all. */
    int empty_y = -1;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        int covered = 0;
        for (int i = 0; i < GOLDEN_FIELD_COUNT; i++) {
            if (y >= GOLDEN_FIELDS[i].y && y < GOLDEN_FIELDS[i].y + GOLDEN_FIELDS[i].h) covered = 1;
        }
        if (!covered) { empty_y = y; break; }
    }
    if (empty_y < 0) { TEST_IGNORE_MESSAGE("every row carries a field"); return; }

    uint8_t *band = malloc(EPD_FB_BYTES);
    TEST_ASSERT_NOT_NULL(band);
    canvas_t b;
    canvas_init_band(&b, band, empty_y, 1);
    TEST_ASSERT_EQUAL_INT(0, render_compose_band(&b, mem_reader_static, static_layer,
                                                 GOLDEN_FIELDS, GOLDEN_VALUES,
                                                 GOLDEN_FIELD_COUNT));
    TEST_ASSERT_EQUAL_MEMORY(static_layer + (size_t)empty_y * EPD_PITCH, band, EPD_PITCH);
    free(band);
}

/* Bad arguments are rejected rather than read out of bounds — a band canvas with no rows is a
 * caller bug and must not become a zero-length memcpy that silently looks like success. */
static void test_band_rejects_bad_arguments(void)
{
    uint8_t *band = malloc(EPD_FB_BYTES);
    TEST_ASSERT_NOT_NULL(band);

    canvas_t b;
    canvas_init_band(&b, band, 0, 0);
    TEST_ASSERT_TRUE(render_compose_band(&b, mem_reader_static, static_layer, NULL, NULL, 0) < 0);

    canvas_init_band(&b, band, 0, 8);
    TEST_ASSERT_TRUE(render_compose_band(NULL, mem_reader_static, static_layer, NULL, NULL, 0) < 0);
    TEST_ASSERT_TRUE(render_compose_band(&b, NULL, static_layer, NULL, NULL, 0) < 0);
    TEST_ASSERT_TRUE(render_compose_band(&b, mem_reader_static, static_layer, NULL, NULL, -1) < 0);
    /* fields/values must be present when the count says so. */
    TEST_ASSERT_TRUE(render_compose_band(&b, mem_reader_static, static_layer, NULL, NULL, 3) < 0);

    free(band);
}

/* THE DEFINING PROPERTY OF AN UPSCALED FACE: rendering text at 256 px must be exactly the 128 px
 * render expanded, each pixel to a 2 x 2 block. That is what "a k x k block scale" MEANS, and it
 * is the one assertion that catches a scaling factor applied to some metrics and not others —
 * which would otherwise show up only as misaligned glyphs on the glass.
 *
 * THE ORIGIN IS THE BOX, NOT THE PANEL. The pen starts at the box's top-left in BOTH renders
 * (the box geometry is unscaled), so the block expansion is anchored there: an up-pixel at
 * (x, y) corresponds to the base pixel at (ox + (x-ox)/k, oy + (y-oy)/k). Anchoring at the panel
 * corner instead would demand the text also scale its BOX, which it must not — the box is the
 * user's layout, not part of the glyph.
 *
 * A large box is used so the whole string lands without clipping: a clipped glyph would break the
 * block-for-block correspondence at the box edge for a reason that is not this bug. */
static void test_an_upscaled_face_draws_a_block_scale(void)
{
    struct { int px, base, k; } cases[] = {
        { 160, 80,  2 },
        { 192, 96,  2 },
        { 256, 128, 2 },
        { 320, 80,  4 },
        { 384, 128, 3 },
        { 512, 128, 4 },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int up_px = cases[i].px, base_px = cases[i].base, k = cases[i].k;

        const int ox = 20, oy = 20, bw = 860, bh = 600;
        value_field_t f_up = { .x = ox, .y = oy, .w = bw, .h = bh,
                               .align_h = 'L', .align_v = 'T',
                               .font_id = font_face_for_px((double)up_px) };
        value_field_t f_base = f_up;
        f_base.font_id = font_face_for_px((double)base_px);
        TEST_ASSERT_EQUAL_INT(up_px, font_px((font_id_t)f_up.font_id));
        TEST_ASSERT_EQUAL_INT(base_px, font_px((font_id_t)f_base.font_id));

        const char *values[1] = { "Hg" };
        canvas_t cu, cb;
        canvas_init(&cu, fb);
        uint8_t *fb_base = malloc(EPD_FB_BYTES);
        TEST_ASSERT_NOT_NULL(fb_base);
        canvas_init(&cb, fb_base);

        TEST_ASSERT_EQUAL_INT(0, render_compose(&cu, static_layer, &f_up, values, 1));
        TEST_ASSERT_EQUAL_INT(0, render_compose(&cb, static_layer, &f_base, values, 1));

        /* Every pixel of the upscaled render must equal its base pixel: ink if that base pixel
         * is ink, white otherwise. Checked over the whole box. */
        int mismatches = 0;
        for (int y = oy; y < oy + bh && mismatches == 0; y++) {
            for (int x = ox; x < ox + bw; x++) {
                const int up_ink = !px(fb, x, y);
                const int base_ink = !px(fb_base, ox + (x - ox) / k, oy + (y - oy) / k);
                if (up_ink != base_ink) { mismatches++; break; }
            }
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, mismatches,
            "an upscaled face is not an exact k x k block scale of its base");

        /* And it did draw SOMETHING — a no-op renderer would trivially match a blank base. */
        TEST_ASSERT_GREATER_THAN_INT(0, ink_in(fb, ox, oy, ox + bw, oy + bh));

        free(fb_base);
    }
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
    RUN_TEST(test_a_band_matches_the_whole_frame_render);
    RUN_TEST(test_a_band_with_no_fields_is_pure_static_layer);
    RUN_TEST(test_band_rejects_bad_arguments);
    RUN_TEST(test_an_upscaled_face_draws_a_block_scale);
    RUN_TEST(test_golden_image_of_default_layout);
    return UNITY_END();
}
