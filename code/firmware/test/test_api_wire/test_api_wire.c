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



/* ------------------------------------------------------------- api_location_slices --- */

static char g_num[API_SLICE_NUM_BUF_LEN];

/* Concatenate the slices — what the HTTP client would receive after chunked reassembly. */
static const char *join(const char *json, double lat, double lon)
{
    static char buf[1024];
    api_slice_t sl[API_SLICE_MAX];
    const int n = api_location_slices(json, lat, lon, g_num, sizeof(g_num), sl, API_SLICE_MAX);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, json);
    size_t w = 0;
    for (int i = 0; i < n; i++) {
        TEST_ASSERT_TRUE(w + sl[i].len < sizeof(buf));
        memcpy(buf + w, sl[i].p, sl[i].len);
        w += sl[i].len;
    }
    buf[w] = '\0';
    return buf;
}

static void test_slices_replace_both_numbers(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"latitude\":41.5,\"longitude\":-111.25,\"zipCode\":\"\"}}",
        join("{\"location\":{\"latitude\":0,\"longitude\":0,\"zipCode\":\"\"}}", 41.5, -111.25));
}

static void test_slices_keep_the_rest_of_the_document(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"schemaVersion\":1,\"location\":{\"latitude\":1.25,\"longitude\":2.5},"
        "\"pages\":[{\"name\":\"main\"}]}",
        join("{\"schemaVersion\":1,\"location\":{\"latitude\":0,\"longitude\":0},"
             "\"pages\":[{\"name\":\"main\"}]}", 1.25, 2.5));
}

/* longitude-before-latitude must work: the code must not assume the app's field order. */
static void test_slices_handle_reversed_field_order(void)
{
    TEST_ASSERT_EQUAL_STRING("{\"location\":{\"longitude\":4.5,\"latitude\":3.5}}",
        join("{\"location\":{\"longitude\":0,\"latitude\":0}}", 3.5, 4.5));
}

/* A nested object/array between the two numbers must be copied through untouched. */
static void test_slices_skip_nested_structures(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"meta\":{\"a\":[1,2,3]},\"latitude\":7.5,\"longitude\":8.5},\"z\":9}",
        join("{\"location\":{\"meta\":{\"a\":[1,2,3]},\"latitude\":0,\"longitude\":0},\"z\":9}",
             7.5, 8.5));
}

/* A string containing braces must not confuse the depth counting. */
static void test_slices_ignore_braces_inside_strings(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"note\":\"}{ not real }\",\"latitude\":1.5,\"longitude\":2.5}}",
        join("{\"location\":{\"note\":\"}{ not real }\",\"latitude\":0,\"longitude\":0}}", 1.5, 2.5));
}

/* The default document has NO location: one is inserted rather than the call failing. */
static void test_slices_insert_a_missing_location(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"latitude\":5.5,\"longitude\":6.5},\"schemaVersion\":1,\"pages\":[]}",
        join("{\"schemaVersion\":1,\"pages\":[]}", 5.5, 6.5));
}

/* A same-named key NESTED deeper must not be mistaken for the top-level location. */
static void test_slices_only_edit_the_top_level_location(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"latitude\":1.5,\"longitude\":2.5},"
        "\"meta\":{\"location\":{\"latitude\":9,\"longitude\":9}}}",
        join("{\"location\":{\"latitude\":0,\"longitude\":0},"
             "\"meta\":{\"location\":{\"latitude\":9,\"longitude\":9}}}", 1.5, 2.5));
}

/* A value that round-trips through 15 significant digits prints short, exactly as cJSON does. */
static void test_slices_use_the_short_form_when_it_round_trips(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"latitude\":45.6789012,\"longitude\":-123.456789}}",
        join("{\"location\":{\"latitude\":0,\"longitude\":0}}", 45.6789012, -123.456789));
}

/* And one that does NOT round-trip prints 17 digits, again matching cJSON. The app re-reads these
 * numbers, and a document spliced differently from one that went through a tree would show the pin
 * a fraction off. */
static void test_slices_match_cjson_precision(void)
{
    TEST_ASSERT_EQUAL_STRING(
        "{\"location\":{\"latitude\":41.827139015592429,\"longitude\":-111.807861328125}}",
        join("{\"location\":{\"latitude\":0,\"longitude\":0}}",
             41.82713901559243, -111.807861328125));
}

static void test_slices_reject_a_document_with_nothing_to_insert_into(void)
{
    api_slice_t sl[API_SLICE_MAX];
    TEST_ASSERT_EQUAL_INT(-1,
        api_location_slices("not json at all", 1, 2, g_num, sizeof(g_num), sl, API_SLICE_MAX));
}

static void test_slices_reject_bad_arguments(void)
{
    api_slice_t sl[API_SLICE_MAX];
    TEST_ASSERT_EQUAL_INT(-1, api_location_slices(NULL, 1, 2, g_num, sizeof(g_num), sl, API_SLICE_MAX));
    TEST_ASSERT_EQUAL_INT(-1, api_location_slices("{\"location\":{}}", 1, 2, NULL, 10, sl, API_SLICE_MAX));
    TEST_ASSERT_EQUAL_INT(-1, api_location_slices("{\"location\":{}}", 1, 2, g_num, sizeof(g_num), sl, 0));
}

/* Too small a number buffer must fail rather than truncate a coordinate. */
static void test_slices_reject_a_too_small_number_buffer(void)
{
    char tiny[16];
    api_slice_t sl[API_SLICE_MAX];
    TEST_ASSERT_EQUAL_INT(-1,
        api_location_slices("{\"location\":{\"latitude\":0,\"longitude\":0}}", 1.5, 2.5, tiny, sizeof(tiny), sl, API_SLICE_MAX));
}

static void test_slices_handle_a_real_shipped_document(void)
{
    const char *in = "{\"schemaVersion\":1,\"generator\":\"eink-weather-webapp\","
                     "\"updateSeconds\":900,\"location\":{\"latitude\":0,\"longitude\":0,\"zipCode\":\"\"},"
                     "\"owmProduct\":\"auto\",\"ha\":{\"mode\":\"rest\"},\"pages\":[{\"name\":\"main\"}]}";
    TEST_ASSERT_EQUAL_STRING(
        "{\"schemaVersion\":1,\"generator\":\"eink-weather-webapp\","
        "\"updateSeconds\":900,\"location\":{\"latitude\":45.6789012,\"longitude\":-123.456789,\"zipCode\":\"\"},"
        "\"owmProduct\":\"auto\",\"ha\":{\"mode\":\"rest\"},\"pages\":[{\"name\":\"main\"}]}",
        join(in, 45.6789012, -123.456789));
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
    RUN_TEST(test_slices_replace_both_numbers);
    RUN_TEST(test_slices_keep_the_rest_of_the_document);
    RUN_TEST(test_slices_handle_reversed_field_order);
    RUN_TEST(test_slices_skip_nested_structures);
    RUN_TEST(test_slices_ignore_braces_inside_strings);
    RUN_TEST(test_slices_insert_a_missing_location);
    RUN_TEST(test_slices_only_edit_the_top_level_location);
    RUN_TEST(test_slices_use_the_short_form_when_it_round_trips);
    RUN_TEST(test_slices_match_cjson_precision);
    RUN_TEST(test_slices_reject_a_document_with_nothing_to_insert_into);
    RUN_TEST(test_slices_reject_bad_arguments);
    RUN_TEST(test_slices_reject_a_too_small_number_buffer);
    RUN_TEST(test_slices_handle_a_real_shipped_document);
    return UNITY_END();
}
