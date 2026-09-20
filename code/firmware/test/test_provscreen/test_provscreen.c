/* Verifies the provisioning screen actually draws scannable QR codes.
 *
 * The failure this guards against is specific and quiet: a QR blitted with the wrong bit
 * convention or row order still looks like a QR code to a human — full of squares — but will
 * not decode. A rendered PNG proves nothing here; only checking that the modules landed where
 * the reference matrix says they should does. */

#include "unity.h"
#include "provscreen.h"
#include "canvas.h"
#include "qr_data.h"
#include "fonts.h"
#include <stdlib.h>
#include <string.h>

static uint8_t *fb;
void setUp(void) { fb = malloc(EPD_FB_BYTES); }
void tearDown(void) { free(fb); }

/* Framebuffer: bit clear = black ink. */
static int inked(int x, int y)
{
    return ((fb[(size_t)y * EPD_PITCH + (x >> 3)] >> (7 - (x & 7))) & 1) ? 0 : 1;
}

static void test_renders_and_leaves_the_expected_page(void)
{
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, "EINK-WEATHER-2045AC", "eink1234"));
    /* Some ink, but not a black page: a screen that fills solid would pass a naive check. */
    long ink = 0;
    for (int y = 0; y < EPD_HEIGHT; y++)
        for (int x = 0; x < EPD_WIDTH; x++) ink += inked(x, y);
    TEST_ASSERT_GREATER_THAN_INT(1000, (int)ink);
    TEST_ASSERT_LESS_THAN_INT(EPD_WIDTH * EPD_HEIGHT / 2, (int)ink);
}

/* THE REAL CHECK: pull each QR code back out of the rendered framebuffer, downsample it to
 * modules, and compare module-for-module against the reference matrix it was generated from.
 *
 * This is what a finder-pattern spot check cannot do. A code blitted with the wrong bit order,
 * the wrong row order, or a bad scale still contains finder patterns and still looks like a QR
 * code — it just will not decode. Comparing every module against the source matrix is the only
 * check that actually proves the code is scannable. */
static int qr_roundtrip_matches(const qr_code_t *qr, int qx, int qy, int box)
{
    const int k = box / qr->size;
    if (k < 1) return 0;
    const int total = k * qr->size;
    const int ox = qx + (box - total) / 2;
    const int oy = qy + (box - total) / 2;

    for (int my = 0; my < qr->size; my++) {
        for (int mx = 0; mx < qr->size; mx++) {
            const int want = (qr->bits[(size_t)my * qr->pitch + (mx >> 3)]
                              >> (7 - (mx & 7))) & 1;
            /* Sample the CENTRE of the module: edges can straddle a pixel boundary when the
             * scale does not divide exactly, so the centre is the module's true value. */
            const int got = inked(ox + mx * k + k / 2, oy + my * k + k / 2);
            if (got != want) return 0;
        }
    }
    return 1;
}

static void test_qr_codes_survive_the_blit_byte_for_byte(void)
{
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, "EINK-WEATHER-2045AC", "eink1234"));

    /* Same geometry the layout uses. If the layout changes these must change with it, which is
     * the point: the test fails rather than silently checking the wrong place. */
    const int qx = 36 + 520 + 30;
    int qy = 36 + 24;
    const struct { const qr_code_t *qr; const char *name; } codes[] = {
        { &QR_PORTAL,  "portal"  },
        { &QR_IOS,     "iOS"     },
        { &QR_ANDROID, "android" },
    };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, qr_roundtrip_matches(codes[i].qr, qx, qy, 150),
                                      codes[i].name);
        qy += 150 + font_line_height(FONT_BODY) + 26;   /* QR_BOX + caption + gap */
    }
}

/* The quiet zone must be clear, or the code will not scan no matter how correct the modules
 * are. Checks a ring of pixels just outside the matrix. */
static void test_qr_quiet_zone_is_clear(void)
{
    provscreen_render(fb, "EINK-WEATHER-2045AC", "eink1234");
    const int qx = 36 + 520 + 30;
    const int k = 150 / QR_PORTAL.size;
    const int total = k * QR_PORTAL.size;
    const int ox = qx + (150 - total) / 2;
    const int oy = 36 + 24 + (150 - total) / 2;
    for (int i = -4; i < total + 4; i++) {
        TEST_ASSERT_EQUAL_INT(0, inked(ox + i, oy - 4));
        TEST_ASSERT_EQUAL_INT(0, inked(ox + i, oy + total + 3));
        TEST_ASSERT_EQUAL_INT(0, inked(ox - 4, oy + i));
        TEST_ASSERT_EQUAL_INT(0, inked(ox + total + 3, oy + i));
    }
}

/* Sanity: a null or empty SSID is a caller bug and must be refused rather than rendering a
 * screen that cannot be followed. */
static void test_bad_arguments_are_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(-1, provscreen_render(NULL, "x", "y"));
    TEST_ASSERT_EQUAL_INT(-1, provscreen_render(fb, NULL, "y"));
    TEST_ASSERT_EQUAL_INT(-1, provscreen_render(fb, "", "y"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_renders_and_leaves_the_expected_page);
    RUN_TEST(test_qr_codes_survive_the_blit_byte_for_byte);
    RUN_TEST(test_qr_quiet_zone_is_clear);
    RUN_TEST(test_bad_arguments_are_rejected);
    return UNITY_END();
}
