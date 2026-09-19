#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "canvas.h"

static canvas_t c;
static uint8_t *buf;

void setUp(void)
{
    buf = malloc(EPD_FB_BYTES);
    canvas_init(&c, buf);
}

void tearDown(void) { free(buf); }

static void test_buffer_size_is_exact(void)
{
    TEST_ASSERT_EQUAL_INT(920, EPD_WIDTH);
    TEST_ASSERT_EQUAL_INT(680, EPD_HEIGHT);
    /* 920 * 680 / 8 == 78200 exactly (HW-6) */
    TEST_ASSERT_EQUAL_INT(78200, EPD_FB_BYTES);
    TEST_ASSERT_EQUAL_INT(78200, EPD_WIDTH * EPD_HEIGHT / 8);
}

static void test_fill_white_sets_all_bits(void)
{
    canvas_fill(&c, 0);           /* 0 == white */
    for (size_t i = 0; i < EPD_FB_BYTES; i++) {
        TEST_ASSERT_EQUAL_UINT8(0xFF, buf[i]);
    }
}

static void test_set_px_roundtrip_msb_first(void)
{
    canvas_fill(&c, 0);
    canvas_set_px(&c, 0, 0, 1);   /* black, MSB of byte 0 */
    TEST_ASSERT_EQUAL_UINT8(0x7F, buf[0]);
    TEST_ASSERT_EQUAL_INT(1, canvas_get_px(&c, 0, 0));
    TEST_ASSERT_EQUAL_INT(0, canvas_get_px(&c, 1, 0));

    /* x=7 is the LSB of byte 0; x=8 starts byte 1 */
    canvas_set_px(&c, 7, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(0x7E, buf[0]);
    canvas_set_px(&c, 8, 0, 1);
    TEST_ASSERT_EQUAL_UINT8(0x7F, buf[1]);
}

static void test_row_pitch_is_width_over_8(void)
{
    canvas_fill(&c, 0);
    canvas_set_px(&c, 0, 1, 1);   /* row 1 => byte 920/8 == 115 */
    TEST_ASSERT_EQUAL_UINT8(0x7F, buf[115]);
    TEST_ASSERT_EQUAL_UINT8(0xFF, buf[114]);
}

static void test_blit_respects_destination_offset(void)
{
    canvas_fill(&c, 0);
    uint8_t src[2] = {0x00, 0x00};   /* 16 black px, 2 bytes wide */
    canvas_blit_1bpp(&c, 3, 5, src, 16, 1);
    TEST_ASSERT_EQUAL_INT(1, canvas_get_px(&c, 3, 5));
    TEST_ASSERT_EQUAL_INT(1, canvas_get_px(&c, 18, 5));
    TEST_ASSERT_EQUAL_INT(0, canvas_get_px(&c, 2, 5));
    TEST_ASSERT_EQUAL_INT(0, canvas_get_px(&c, 19, 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_buffer_size_is_exact);
    RUN_TEST(test_fill_white_sets_all_bits);
    RUN_TEST(test_set_px_roundtrip_msb_first);
    RUN_TEST(test_row_pitch_is_width_over_8);
    RUN_TEST(test_blit_respects_destination_offset);
    return UNITY_END();
}
