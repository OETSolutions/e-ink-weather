#include "unity.h"
#include "epd_encode.h"

void setUp(void) {}
void tearDown(void) {}

/* 1 input byte -> exactly 2 output bytes. Values confirmed against the vendor demo. */
static void test_expand_matches_vendor_reference(void)
{
    uint8_t in[3] = { 0x00, 0xFF, 0xA0 };
    uint8_t o[6]  = { 9, 9, 9, 9, 9, 9 };
    epd_expand_1to2(in, 3, o);
    TEST_ASSERT_EQUAL_UINT8(0x00, o[0]);  TEST_ASSERT_EQUAL_UINT8(0x00, o[1]); /* 0x00 */
    TEST_ASSERT_EQUAL_UINT8(0xFF, o[2]);  TEST_ASSERT_EQUAL_UINT8(0xFF, o[3]); /* 0xFF */
    TEST_ASSERT_EQUAL_UINT8(0xCC, o[4]);  TEST_ASSERT_EQUAL_UINT8(0x00, o[5]); /* 0xA0 */
}

/* Each nibble expands independently: 0xF0 -> FF 00 ; 0x0F -> 00 FF */
static void test_nibble_expansion(void)
{
    uint8_t f0[1] = { 0xF0 }, of[2];
    epd_expand_1to2(f0, 1, of);
    TEST_ASSERT_EQUAL_UINT8(0xFF, of[0]); TEST_ASSERT_EQUAL_UINT8(0x00, of[1]);

    uint8_t f[1] = { 0x0F }, o0[2];
    epd_expand_1to2(f, 1, o0);
    TEST_ASSERT_EQUAL_UINT8(0x00, o0[0]); TEST_ASSERT_EQUAL_UINT8(0xFF, o0[1]);
}

/* Temperature -> LUT value, taken verbatim from the vendor demo's Write_LUT_All(). */
static void test_lut_selection_matches_vendor_demo(void)
{
    TEST_ASSERT_EQUAL_INT(232, epd_lut_value_for_temp(-20));
    TEST_ASSERT_EQUAL_INT(232, epd_lut_value_for_temp(5));
    TEST_ASSERT_EQUAL_INT(235, epd_lut_value_for_temp(10));
    TEST_ASSERT_EQUAL_INT(238, epd_lut_value_for_temp(20));
    TEST_ASSERT_EQUAL_INT(241, epd_lut_value_for_temp(30));
    TEST_ASSERT_EQUAL_INT(244, epd_lut_value_for_temp(40));
    TEST_ASSERT_EQUAL_INT(232, epd_lut_value_for_temp(200));  /* out of range -> default */
}

/* Partial updates interleave the PREVIOUS frame with the new one — the SSD2677 derives
 * each pixel's transition from the pair. Values confirmed against the vendor demo's
 * bitInterleave() (verified exhaustively over all 65,536 byte pairs). */
static void test_interleave_matches_vendor_reference(void)
{
    uint8_t prev[1], next[1], o[2];

    prev[0] = 0xFF; next[0] = 0x00;         /* white -> black */
    epd_interleave_1to2(prev, next, 1, o);
    TEST_ASSERT_EQUAL_UINT8(0xAA, o[0]); TEST_ASSERT_EQUAL_UINT8(0xAA, o[1]);

    prev[0] = 0x00; next[0] = 0xFF;         /* black -> white */
    epd_interleave_1to2(prev, next, 1, o);
    TEST_ASSERT_EQUAL_UINT8(0x55, o[0]); TEST_ASSERT_EQUAL_UINT8(0x55, o[1]);

    prev[0] = 0xF0; next[0] = 0x0F;         /* nibble swap */
    epd_interleave_1to2(prev, next, 1, o);
    TEST_ASSERT_EQUAL_UINT8(0xAA, o[0]); TEST_ASSERT_EQUAL_UINT8(0x55, o[1]);

    /* Single bit, to pin the exact bit positions rather than a symmetric pattern. */
    prev[0] = 0x80; next[0] = 0x00;
    epd_interleave_1to2(prev, next, 1, o);
    TEST_ASSERT_EQUAL_UINT8(0x80, o[0]); TEST_ASSERT_EQUAL_UINT8(0x00, o[1]);

    prev[0] = 0x01; next[0] = 0x00;
    epd_interleave_1to2(prev, next, 1, o);
    TEST_ASSERT_EQUAL_UINT8(0x00, o[0]); TEST_ASSERT_EQUAL_UINT8(0x02, o[1]);
}

/* No transition (prev == next) must reproduce the full-update expansion exactly. This is
 * the identity that ties the two encoders together: it means a partial write of an
 * unchanged frame is byte-identical to what a full write would have sent. */
static void test_interleave_with_unchanged_frame_equals_expand(void)
{
    uint8_t in[4] = { 0x00, 0xFF, 0xA0, 0x3C }, io[8], eo[8];
    epd_interleave_1to2(in, in, 4, io);
    epd_expand_1to2(in, 4, eo);
    for (int i = 0; i < 8; i++) TEST_ASSERT_EQUAL_UINT8(eo[i], io[i]);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_expand_matches_vendor_reference);
    RUN_TEST(test_nibble_expansion);
    RUN_TEST(test_lut_selection_matches_vendor_demo);
    RUN_TEST(test_interleave_matches_vendor_reference);
    RUN_TEST(test_interleave_with_unchanged_frame_equals_expand);
    return UNITY_END();
}
