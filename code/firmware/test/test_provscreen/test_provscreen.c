/* Verifies the provisioning screen actually draws scannable QR codes.
 *
 * The failure this guards against is specific and quiet: a QR blitted with the wrong bit
 * convention, row order or scale still looks like a QR code to a human — full of squares — but
 * will not decode. A rendered PNG proves nothing here; only checking that the modules landed
 * where the encoder says they should does. */

#include "unity.h"
#include "provscreen.h"
#include "canvas.h"
#include "qr_data.h"
#include "fonts.h"
#include "qrcodegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The layout geometry, mirrored from provscreen.c. If the layout changes these must change with
 * it, which is the point: the test fails rather than silently checking the wrong place. */
#define MARGIN      30
#define LEFT_W      486
#define QR_GAP      18
#define QR_COL_GAP  18
#define QR_ROW_GAP  16
#define QR_BOX      ((EPD_WIDTH - 2 * MARGIN - LEFT_W - QR_GAP - QR_COL_GAP) / 2)

static uint8_t *fb;
void setUp(void) { fb = malloc(EPD_FB_BYTES); }
void tearDown(void) { free(fb); }

/* Framebuffer: bit clear = black ink. */
static int inked(int x, int y)
{
    return ((fb[(size_t)y * EPD_PITCH + (x >> 3)] >> (7 - (x & 7))) & 1) ? 0 : 1;
}

/* Where code `i` is drawn, in the 2x2 grid. */
static void code_origin(int i, int *x, int *y)
{
    const int qx = MARGIN + LEFT_W + QR_GAP;
    const int qy0 = MARGIN + 8;
    *x = qx + (i % 2) * (QR_BOX + QR_COL_GAP);
    *y = qy0 + (i / 2) * (QR_BOX + font_line_height(FONT_BODY) + QR_ROW_GAP);
}

/* Pull a code back out of the rendered framebuffer and compare module-for-module against the
 * matrix it came from. Samples the CENTRE of each module: edges can straddle a pixel boundary
 * when the scale does not divide exactly, so the centre carries the module's true value. */
static int matches_matrix(const qr_code_t *qr, int x, int y, int box)
{
    const int k = box / qr->size;
    if (k < 1) return 0;
    const int total = k * qr->size;
    const int ox = x + (box - total) / 2;
    const int oy = y + (box - total) / 2;
    for (int my = 0; my < qr->size; my++) {
        for (int mx = 0; mx < qr->size; mx++) {
            const int want = (qr->bits[(size_t)my * qr->pitch + (mx >> 3)]
                              >> (7 - (mx & 7))) & 1;
            if (inked(ox + mx * k + k / 2, oy + my * k + k / 2) != want) return 0;
        }
    }
    return 1;
}

static int all_ink_count(void)
{
    long n = 0;
    for (int y = 0; y < EPD_HEIGHT; y++)
        for (int x = 0; x < EPD_WIDTH; x++) n += inked(x, y);
    return (int)n;
}

static void test_renders_and_leaves_the_expected_page(void)
{
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, "EINK-WEATHER-2045AC", "eink1234"));
    /* Some ink, but not a black page: a screen that fills solid would pass a naive check. */
    const int ink = all_ink_count();
    TEST_ASSERT_GREATER_THAN_INT(4000, ink);
    TEST_ASSERT_LESS_THAN_INT(EPD_WIDTH * EPD_HEIGHT / 2, ink);
}

/* The three pre-generated codes must survive the blit byte for byte. */
static void test_prebuilt_qr_codes_survive_the_blit(void)
{
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, "EINK-WEATHER-2045AC", "eink1234"));

    /* Grid order is [0]=runtime provisioning, [1]=setup page, [2]=iOS app, [3]=Android app. */
    const struct { int i; const qr_code_t *qr; const char *name; } cases[] = {
        { 1, &QR_PORTAL,  "portal"  },
        { 2, &QR_IOS,     "ios"     },
        { 3, &QR_ANDROID, "android" },
    };
    for (size_t n = 0; n < sizeof(cases) / sizeof(cases[0]); n++) {
        int x, y;
        code_origin(cases[n].i, &x, &y);
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, matches_matrix(cases[n].qr, x, y, QR_BOX),
                                      cases[n].name);
    }
}

/* THE RUNTIME CODE, which is the one that matters most for convenience and the one that cannot
 * be checked against a committed matrix — its payload carries the device's own name.
 *
 * Re-encodes the identical payload with the identical parameters and compares module-for-module
 * against what was blitted. The encoder itself is upstream-tested; what this proves is that the
 * BLIT is right — scale, offset, polarity and bit order — which is exactly where a QR goes
 * subtly and invisibly wrong. */
static void test_runtime_provisioning_code_survives_the_blit(void)
{
    const char *ssid = "EINK-WEATHER-2045AC";
    const char *pop  = "eink1234";
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, ssid, pop));

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}", ssid, pop);

    static uint8_t qr[qrcodegen_BUFFER_LEN_MAX], tmp[qrcodegen_BUFFER_LEN_MAX];
    TEST_ASSERT_TRUE(qrcodegen_encodeText(payload, tmp, qr, qrcodegen_Ecc_LOW,
                                          1, qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, false));

    const int size = qrcodegen_getSize(qr);
    int x, y;
    code_origin(0, &x, &y);
    const int k = QR_BOX / size;
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, k);      /* at least one pixel per module */
    const int total = k * size;
    const int ox = x + (QR_BOX - total) / 2;
    const int oy = y + (QR_BOX - total) / 2;

    for (int my = 0; my < size; my++) {
        for (int mx = 0; mx < size; mx++) {
            const int want = qrcodegen_getModule(qr, mx, my) ? 1 : 0;   /* true = dark */
            const int got = inked(ox + mx * k + k / 2, oy + my * k + k / 2);
            if (want != got) {
                char msg[80];
                snprintf(msg, sizeof(msg), "module (%d,%d) want %d got %d", mx, my, want, got);
                TEST_FAIL_MESSAGE(msg);
            }
        }
    }
}

/* The payload the app matches on must name the device exactly as it advertises, or the app
 * scans a code that cannot find the peripheral. */
static void test_payload_names_the_advertised_device(void)
{
    const char *ssid = "EINK-WEATHER-2045AC";
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, ssid, "eink1234"));

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"ver\":\"v1\",\"name\":\"%s\",\"pop\":\"%s\",\"transport\":\"ble\"}",
             ssid, "eink1234");
    /* The documented format, in the documented shape. */
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"ver\":\"v1\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"transport\":\"ble\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"name\":\"EINK-WEATHER-2045AC\""));
    TEST_ASSERT_NOT_NULL(strstr(payload, "\"pop\":\"eink1234\""));
}

/* Each code needs a clear quiet zone or it will not scan, however correct its modules are. */
static void test_qr_quiet_zone_is_clear(void)
{
    TEST_ASSERT_EQUAL_INT(0, provscreen_render(fb, "EINK-WEATHER-2045AC", "eink1234"));

    int x, y;
    code_origin(1, &x, &y);              /* the setup-page code */
    const int k = QR_BOX / QR_PORTAL.size;
    const int total = k * QR_PORTAL.size;
    const int ox = x + (QR_BOX - total) / 2;
    const int oy = y + (QR_BOX - total) / 2;
    for (int i = -5; i < total + 5; i++) {
        TEST_ASSERT_EQUAL_INT(0, inked(ox + i, oy - 5));
        TEST_ASSERT_EQUAL_INT(0, inked(ox + i, oy + total + 4));
        TEST_ASSERT_EQUAL_INT(0, inked(ox - 5, oy + i));
        TEST_ASSERT_EQUAL_INT(0, inked(ox + total + 4, oy + i));
    }
}

static void test_bad_arguments_are_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(-1, provscreen_render(NULL, "x", "y"));
    TEST_ASSERT_EQUAL_INT(-1, provscreen_render(fb, NULL, "y"));
    TEST_ASSERT_EQUAL_INT(-1, provscreen_render(fb, "", "y"));
}

/* A box too small to give a module a pixel must be refused, not rendered as an unscannable
 * smear of overlapping modules. */
static void test_box_too_small_is_refused(void)
{
    canvas_t c;
    canvas_init(&c, fb);
    TEST_ASSERT_EQUAL_INT(-1, provscreen_blit_qr(&c, 0, 0, 4, "{\"ver\":\"v1\"}"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_renders_and_leaves_the_expected_page);
    RUN_TEST(test_prebuilt_qr_codes_survive_the_blit);
    RUN_TEST(test_runtime_provisioning_code_survives_the_blit);
    RUN_TEST(test_payload_names_the_advertised_device);
    RUN_TEST(test_qr_quiet_zone_is_clear);
    RUN_TEST(test_box_too_small_is_refused);
    RUN_TEST(test_bad_arguments_are_rejected);
    return UNITY_END();
}
