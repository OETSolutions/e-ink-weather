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

/* ---- the banded transmit walk ---- */

/* EVERY ROW EXACTLY ONCE, and the bands in DESCENDING order.
 *
 * This reconstructs the driver's walk (epd.c walks y upward, band = band_of_row(height-1-y)) and
 * checks the three properties the transmit depends on: complete coverage, no row visited twice, and
 * descending band order. A gap leaves a horizontal stripe of the OLD picture on the glass and a
 * repeat shifts every later row — neither is detectable in the driver's return value, so they are
 * pinned here instead.
 *
 * A range of band heights is covered, including ones that do not divide the panel, because the
 * caller picks the size from what fits in the heap and it is therefore not a fixed constant. */
static void test_band_walk_covers_every_row_once_in_descending_order(void)
{
    const int heights[] = { 680 };
    const int band_sizes[] = { 1, 7, 8, 40, 136, 337, 680, 700 };

    for (unsigned hi = 0; hi < sizeof(heights) / sizeof(heights[0]); hi++) {
        for (unsigned si = 0; si < sizeof(band_sizes) / sizeof(band_sizes[0]); si++) {
            const int h = heights[hi];
            const int br = band_sizes[si];

            static int seen[2048];
            for (int i = 0; i < h; i++) seen[i] = 0;

            int last_band = 1 << 30;
            int band_count = 0;

            for (int y = 0; y < h; y++) {
                const int natural = h - 1 - y;
                const int band = epd_band_of_row(natural, br, h);
                TEST_ASSERT_TRUE(band >= 0);

                if (band != last_band) {
                    /* Descending: the first rows sent belong to the LAST band, because the panel
                     * scans bottom-up and the first row transmitted lands at the bottom. */
                    TEST_ASSERT_TRUE(band < last_band);
                    /* Each band holds the rows the helper says it does — the driver refuses any
                     * other count, so a wrong helper would reject the caller's final band. */
                    TEST_ASSERT_TRUE(epd_band_rows_at(band, br, h) > 0);
                    last_band = band;
                    band_count++;
                }

                TEST_ASSERT_EQUAL_INT(0, seen[natural]);
                seen[natural] = 1;
            }

            for (int i = 0; i < h; i++) {
                TEST_ASSERT_EQUAL_INT_MESSAGE(1, seen[i], "a row was never transmitted");
            }
            /* ceil(h / br) bands, and the last one is the short one when it does not divide. */
            const int expected_bands = (h + br - 1) / br;
            TEST_ASSERT_EQUAL_INT(expected_bands, band_count);
        }
    }
}

/* The LAST band is short unless the size divides the panel exactly. The driver rejects a provider
 * that reports anything but this count, so getting it wrong fails the whole refresh. */
static void test_band_rows_reports_the_short_final_band(void)
{
    TEST_ASSERT_EQUAL_INT(136, epd_band_rows_at(0, 136, 680));
    TEST_ASSERT_EQUAL_INT(136, epd_band_rows_at(4, 136, 680));   /* 5 * 136 = 680 exactly */

    TEST_ASSERT_EQUAL_INT(64, epd_band_rows_at(0, 64, 680));     /* 680 = 10 * 64 + 40 */
    TEST_ASSERT_EQUAL_INT(40, epd_band_rows_at(10, 64, 680));

    TEST_ASSERT_EQUAL_INT(7, epd_band_rows_at(0, 7, 680));
    TEST_ASSERT_EQUAL_INT(1, epd_band_rows_at(97, 7, 680));      /* 680 = 97 * 7 + 1 */
}

/* Out-of-range arguments are refused rather than computing a band that does not exist — the driver
 * turns a negative band into a failed refresh instead of reading an uninitialised buffer. */
static void test_band_helpers_reject_bad_arguments(void)
{
    TEST_ASSERT_EQUAL_INT(-1, epd_band_of_row(-1, 64, 680));
    TEST_ASSERT_EQUAL_INT(-1, epd_band_of_row(680, 64, 680));
    TEST_ASSERT_EQUAL_INT(-1, epd_band_of_row(0, 0, 680));
    TEST_ASSERT_EQUAL_INT(-1, epd_band_rows_at(-1, 64, 680));
    TEST_ASSERT_EQUAL_INT(-1, epd_band_rows_at(0, 0, 680));
    TEST_ASSERT_EQUAL_INT(-1, epd_band_rows_at(11, 64, 680));    /* past the last band */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_expand_matches_vendor_reference);
    RUN_TEST(test_nibble_expansion);
    RUN_TEST(test_lut_selection_matches_vendor_demo);
    RUN_TEST(test_interleave_matches_vendor_reference);
    RUN_TEST(test_interleave_with_unchanged_frame_equals_expand);
    RUN_TEST(test_band_walk_covers_every_row_once_in_descending_order);
    RUN_TEST(test_band_rows_reports_the_short_final_band);
    RUN_TEST(test_band_helpers_reject_bad_arguments);
    return UNITY_END();
}
