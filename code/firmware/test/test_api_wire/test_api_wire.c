#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "api_wire.h"

void setUp(void) {}
void tearDown(void) {}

/* A helper that asserts the parameter is rejected. Rejection is the safety-relevant outcome
 * here: a bad `offset` that parses into a plausible number is how a chunk lands in the
 * wrong place in the framebuffer. */
static void assert_rejected(const char *query, const char *key)
{
    uint32_t v = 0xDEADBEEFu;
    TEST_ASSERT_EQUAL_INT_MESSAGE(-1, api_query_u32(query, key, &v), query);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0xDEADBEEFu, v, "out must be untouched on failure");
}

static void test_parses_a_lone_parameter(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("offset=4096", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(4096, v);
}

static void test_parses_from_a_leading_question_mark(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("?offset=0", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(0, v);
}

static void test_finds_a_parameter_middle_and_last(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("?total=78200&offset=8192", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(8192, v);

    TEST_ASSERT_EQUAL_INT(0, api_query_u32("?offset=8192&total=78200", "total", &v));
    TEST_ASSERT_EQUAL_UINT32(78200, v);
}

static void test_does_not_match_a_key_prefix(void)
{
    /* "offset_total" must not satisfy a request for "offset". A substring match here would
     * read the wrong number into the upload state machine. */
    assert_rejected("offset_total=4096", "offset");
    assert_rejected("?offsets=4096", "offset");
}

static void test_does_not_match_a_suffix_of_a_key(void)
{
    assert_rejected("my_offset=4096", "offset");
}

static void test_absent_key_is_rejected(void)
{
    assert_rejected("total=78200", "offset");
    assert_rejected("", "offset");
    assert_rejected("?", "offset");
}

static void test_rejects_non_numeric_and_junk(void)
{
    assert_rejected("offset=", "offset");
    assert_rejected("offset=abc", "offset");
    assert_rejected("offset=-1", "offset");
    assert_rejected("offset=4096x", "offset");
    assert_rejected("offset=40%20", "offset");
    assert_rejected("offset=0x1000", "offset");
    assert_rejected("offset=4.9", "offset");
}

/* Every byte that is not a decimal digit must be refused IN ANY POSITION — including the
 * last. Checking only for digits above '9' lets a negative sign through as a huge number
 * whenever nothing follows it to trip the overflow guard. */
static void test_rejects_every_non_digit_byte_in_the_final_position(void)
{
    const char *bad = "-+.,/ :a;=~_";
    for (const char *q = bad; *q; q++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "offset=42%c", *q);
        assert_rejected(buf, "offset");
        /* ...and alone, with no preceding digit to force an overflow. */
        snprintf(buf, sizeof(buf), "offset=%c", *q);
        assert_rejected(buf, "offset");
    }
}

/* The digit cap is an explicit bound, not a consequence of the overflow check. A value of
 * arbitrary length made of leading zeros would fit numerically, and is still refused: the
 * parser's contract is "a plain non-negative decimal integer", and pinning that here keeps
 * the bound from being tightened or removed by accident. */
static void test_rejects_a_value_longer_than_ten_digits(void)
{
    assert_rejected("offset=000000000000000000000004096", "offset");
    assert_rejected("offset=12345678901", "offset");
}

static void test_rejects_values_that_overflow_uint32(void)
{
    /* 2^32 exactly: without a pre-multiply bound this wraps to 0, which is a VALID looking
     * offset — the single most dangerous silent failure this parser can have. */
    assert_rejected("offset=4294967296", "offset");
    assert_rejected("offset=99999999999", "offset");
}

static void test_accepts_the_uint32_ceiling(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("offset=4294967295", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(4294967295u, v);
}

static void test_leading_zeros_are_accepted(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("?offset=0004096", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(4096, v);
}

static void test_flag_without_a_value_is_not_a_match(void)
{
    /* "offset&total=1" — the bare `offset` key must not be read as "=0". */
    assert_rejected("offset&total=1", "offset");
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("offset&total=1", "total", &v));
    TEST_ASSERT_EQUAL_UINT32(1, v);
}

static void test_tolerates_a_trailing_ampersand(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(0, api_query_u32("offset=512&", "offset", &v));
    TEST_ASSERT_EQUAL_UINT32(512, v);
}

static void test_bad_arguments_are_rejected(void)
{
    uint32_t v = 0;
    TEST_ASSERT_EQUAL_INT(-1, api_query_u32(NULL, "offset", &v));
    TEST_ASSERT_EQUAL_INT(-1, api_query_u32("offset=1", NULL, &v));
    TEST_ASSERT_EQUAL_INT(-1, api_query_u32("offset=1", "", &v));
    TEST_ASSERT_EQUAL_INT(-1, api_query_u32("offset=1", "offset", NULL));
}

static void test_the_real_upload_query_parses(void)
{
    /* The exact shape the web app sends: every upload request carries both numbers, and the
     * final one carries a checksum too. */
    uint32_t off = 0, total = 0, crc = 0;
    const char *q = "?offset=77824&total=78200&crc=3126173357";
    TEST_ASSERT_EQUAL_INT(0, api_query_u32(q, "offset", &off));
    TEST_ASSERT_EQUAL_INT(0, api_query_u32(q, "total", &total));
    TEST_ASSERT_EQUAL_INT(0, api_query_u32(q, "crc", &crc));
    TEST_ASSERT_EQUAL_UINT32(77824, off);
    TEST_ASSERT_EQUAL_UINT32(78200, total);
    TEST_ASSERT_EQUAL_UINT32(3126173357u, crc);
}

/* ---- api_location_is_set(): the app's "not chosen yet" sentinel ---- */

/* The case that was a live defect: the app ships its default document at (0,0), and storing
 * that would pin the device to the Gulf of Guinea permanently, because the geo-IP autofill
 * only ever fills a BLANK location. */
static void test_sentinel_origin_is_not_a_location(void)
{
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(0.0, 0.0));
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(-0.0, 0.0));
}

/* A real position is accepted, including ones that share a single zero component — only BOTH
 * being zero is the sentinel, so the equator and the prime meridian stay usable. */
static void test_a_real_position_is_accepted(void)
{
    TEST_ASSERT_EQUAL_INT(1, api_location_is_set(45.6789012, -123.456789));
    TEST_ASSERT_EQUAL_INT(1, api_location_is_set(0.0, -111.8));
    TEST_ASSERT_EQUAL_INT(1, api_location_is_set(41.8, 0.0));
    TEST_ASSERT_EQUAL_INT(1, api_location_is_set(0.000001, 0.0));
}

/* Out of range is rejected, so a bogus coordinate never reaches the OWM URL. */
static void test_out_of_range_is_rejected(void)
{
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(90.5, 10.0));
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(-90.5, 10.0));
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(10.0, 180.5));
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(10.0, -180.5));
}

/* The exact bounds are legal positions. */
static void test_the_bounds_are_legal(void)
{
    TEST_ASSERT_EQUAL_INT(1, api_location_is_set(90.0, 180.0));
    TEST_ASSERT_EQUAL_INT(1, api_location_is_set(-90.0, -180.0));
}

/* NaN must be rejected rather than slipping through as "not the sentinel" — NaN fails every
 * comparison, so an unguarded `lat == 0 && lon == 0` check would let it pass as a location. */
static void test_nan_is_not_a_location(void)
{
    const double nan_v = 0.0 / 0.0;
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(nan_v, 10.0));
    TEST_ASSERT_EQUAL_INT(0, api_location_is_set(10.0, nan_v));
}

/* The entity-search token is interpolated into a Jinja template, so the whitelist is the
 * security boundary. */
static void test_token_query_accepts_an_entity_fragment(void)
{
    char out[32];
    TEST_ASSERT_EQUAL_INT(0, api_query_token("q=upstairs_hallway", "q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("upstairs_hallway", out);
    TEST_ASSERT_EQUAL_INT(0, api_query_token("?q=sensor.64b708cfe0fc", "q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("sensor.64b708cfe0fc", out);
    /* Found among other parameters, and only the value is copied. */
    TEST_ASSERT_EQUAL_INT(0, api_query_token("a=1&q=temp2&b=3", "q", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("temp2", out);
}

/* Every byte that means something to Jinja (or to JSON framing) must be refused, not escaped:
 * a quote would end the string literal and let the rest be template code. */
static void test_token_query_rejects_injection_and_encoded_input(void)
{
    char out[32];
    const char *bad[] = {
        "q='", "q=x'%20or%20'1", "q=%7B%7B7%7D%7D",
        "q=a b", "q=", "other=1", NULL,
    };
    for (int i = 0; bad[i]; i++) {
        /* "%20"/"%7B" are literal here: no percent-decoding, so they carry '%' and are refused. */
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, api_query_token(bad[i], "q", out, sizeof(out)), bad[i]);
    }
    /* Over-long values are refused, not truncated into a different match. */
    TEST_ASSERT_EQUAL_INT(-1, api_query_token("q=aaaaaaaaaa", "q", out, 5));
    /* A short destination must not be overrun. */
    TEST_ASSERT_EQUAL_INT(0, api_query_token("q=abc", "q", out, 4));
    TEST_ASSERT_EQUAL_STRING("abc", out);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_a_lone_parameter);
    RUN_TEST(test_parses_from_a_leading_question_mark);
    RUN_TEST(test_finds_a_parameter_middle_and_last);
    RUN_TEST(test_does_not_match_a_key_prefix);
    RUN_TEST(test_does_not_match_a_suffix_of_a_key);
    RUN_TEST(test_absent_key_is_rejected);
    RUN_TEST(test_rejects_non_numeric_and_junk);
    RUN_TEST(test_rejects_every_non_digit_byte_in_the_final_position);
    RUN_TEST(test_rejects_a_value_longer_than_ten_digits);
    RUN_TEST(test_rejects_values_that_overflow_uint32);
    RUN_TEST(test_accepts_the_uint32_ceiling);
    RUN_TEST(test_leading_zeros_are_accepted);
    RUN_TEST(test_flag_without_a_value_is_not_a_match);
    RUN_TEST(test_tolerates_a_trailing_ampersand);
    RUN_TEST(test_bad_arguments_are_rejected);
    RUN_TEST(test_the_real_upload_query_parses);
    RUN_TEST(test_sentinel_origin_is_not_a_location);
    RUN_TEST(test_a_real_position_is_accepted);
    RUN_TEST(test_out_of_range_is_rejected);
    RUN_TEST(test_the_bounds_are_legal);
    RUN_TEST(test_nan_is_not_a_location);
    RUN_TEST(test_token_query_accepts_an_entity_fragment);
    RUN_TEST(test_token_query_rejects_injection_and_encoded_input);
    return UNITY_END();
}
