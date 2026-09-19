#include "unity.h"
#include "gt30_addr.h"

void setUp(void) {}
void tearDown(void) {}

/* The address LibDriver's own gt30l32s4w_init() self-test reads: base + ('A'-0x20)*16.
 * Computing it here independently pins the stride and the base together — get either
 * wrong and the probe reads a different glyph, which looks exactly like "chip absent". */
static void test_address_of_A_matches_libdriver_selftest(void)
{
    TEST_ASSERT_EQUAL_UINT32(0x1DD780u + ('A' - 0x20) * 16u,
                             gt30_ascii_addr(GT30_ADDR_8X16_ASCII, 'A'));
}

/* Space is code point 0x20, i.e. offset 0 — the first glyph in the face. */
static void test_space_is_offset_zero(void)
{
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_8X16_ASCII,
                             gt30_ascii_addr(GT30_ADDR_8X16_ASCII, ' '));
}

/* The last printable ASCII character must still land inside the 4 MB ROM. */
static void test_tilde_is_last_glyph_and_fits_in_rom(void)
{
    uint32_t a = gt30_ascii_addr(GT30_ADDR_8X16_ASCII, '~');
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_8X16_ASCII + (0x7E - 0x20) * 16u, a);
    TEST_ASSERT_TRUE(a + GT30_GLYPH_BYTES_8X16 <= GT30_ROM_BYTES);
}

/* Control characters and DEL are not in the font; asking for one must be reported,
 * never silently read from a neighbouring glyph. */
static void test_non_printable_is_invalid(void)
{
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_INVALID, gt30_ascii_addr(GT30_ADDR_8X16_ASCII, 0x1F));
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_INVALID, gt30_ascii_addr(GT30_ADDR_8X16_ASCII, 0x00));
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_INVALID, gt30_ascii_addr(GT30_ADDR_8X16_ASCII, 0x7F));
}

/* char is UNSIGNED on both Xtensa toolchains (__CHAR_UNSIGNED__), so a byte with the
 * high bit set arrives as 0x80..0xFF, not as a negative value. Either way it is out of
 * range and must be rejected rather than wrapping into the font. */
static void test_high_bit_byte_is_invalid(void)
{
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_INVALID, gt30_ascii_addr(GT30_ADDR_8X16_ASCII, (char)0x80));
    TEST_ASSERT_EQUAL_UINT32(GT30_ADDR_INVALID, gt30_ascii_addr(GT30_ADDR_8X16_ASCII, (char)0xFF));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_address_of_A_matches_libdriver_selftest);
    RUN_TEST(test_space_is_offset_zero);
    RUN_TEST(test_tilde_is_last_glyph_and_fits_in_rom);
    RUN_TEST(test_non_printable_is_invalid);
    RUN_TEST(test_high_bit_byte_is_invalid);
    return UNITY_END();
}
