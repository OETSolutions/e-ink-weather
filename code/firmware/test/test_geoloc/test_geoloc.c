#include <string.h>
#include "unity.h"
#include "geoloc.h"

void setUp(void) {}
void tearDown(void) {}

/* Captured verbatim from the live service (2026-09-18, ip-api.com with
 * fields=status,message,lat,lon,city). The key order is the service's, not invented: a test
 * that feeds its parser keys in the order it happens to expect proves nothing. */
static const char *OK =
    "{\"status\":\"success\",\"city\":\"Logan\",\"lat\":41.7759,\"lon\":-111.8068}";

/* The shape that matters most: HTTP 200, no coordinates, and `status` saying so. This is what
 * a rejected query or a rate-limited caller gets, and it is the one case where a naive parser
 * would produce 0,0 — a real location in the Gulf of Guinea — and fetch the ocean's weather. */
static const char *FAIL = "{\"status\":\"fail\",\"message\":\"invalid query\"}";

static void test_parses_a_real_reply(void)
{
    double lat = 0, lon = 0;
    char city[32] = {0};
    TEST_ASSERT_EQUAL_INT(0, geoloc_parse(OK, &lat, &lon, city, sizeof(city)));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 41.7759, lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, -111.8068, lon);
    TEST_ASSERT_EQUAL_STRING("Logan", city);
}

/* A fail reply must not be mistaken for a location at 0,0, and must not touch the outputs. */
static void test_fail_status_is_rejected(void)
{
    double lat = 7, lon = 8;
    TEST_ASSERT_EQUAL_INT(-1, geoloc_parse(FAIL, &lat, &lon, NULL, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-9, 7.0, lat);   /* untouched, not zeroed */
    TEST_ASSERT_FLOAT_WITHIN(1e-9, 8.0, lon);
}

/* A success status with the coordinates missing is still not an answer. (The service does not
 * do this today, but "status says ok, fields say nothing" is the shape that breaks an
 * unchecked parser, and the fields list is caller-controlled.) */
static void test_success_without_coordinates_is_rejected(void)
{
    double lat = 0, lon = 0;
    TEST_ASSERT_EQUAL_INT(-1,
        geoloc_parse("{\"status\":\"success\",\"city\":\"Logan\"}", &lat, &lon, NULL, 0));
}

/* A quoted number is a string, not a number. Accepting it would mean this function disagreeing
 * with the caller's expectation of a numeric field. */
static void test_string_coordinates_are_rejected(void)
{
    double lat = 0, lon = 0;
    TEST_ASSERT_EQUAL_INT(-1,
        geoloc_parse("{\"status\":\"success\",\"lat\":\"41.7\",\"lon\":\"-111.8\"}",
                     &lat, &lon, NULL, 0));
}

/* Out-of-range values are rejected at the one point they still can be: they are about to be
 * written to NVS and interpolated into an API URL. */
static void test_out_of_range_coordinates_are_rejected(void)
{
    double lat = 0, lon = 0;
    TEST_ASSERT_EQUAL_INT(-1,
        geoloc_parse("{\"status\":\"success\",\"lat\":141.0,\"lon\":-111.8}", &lat, &lon, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1,
        geoloc_parse("{\"status\":\"success\",\"lat\":41.7,\"lon\":-811.8}", &lat, &lon, NULL, 0));
}

/* Null coordinates are a real place (0,0) and must be accepted as numbers, not treated as
 * absent — an equator/prime-meridian reading is legitimate. */
static void test_zero_coordinates_are_accepted(void)
{
    double lat = 9, lon = 9;
    TEST_ASSERT_EQUAL_INT(0,
        geoloc_parse("{\"status\":\"success\",\"lat\":0,\"lon\":0}", &lat, &lon, NULL, 0));
    TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-9, 0.0, lon);
}

/* A city is decoration and may be absent; its absence must not fail the parse. */
static void test_missing_city_is_not_a_failure(void)
{
    double lat = 0, lon = 0;
    char city[32];
    memset(city, 'x', sizeof(city));
    TEST_ASSERT_EQUAL_INT(0,
        geoloc_parse("{\"status\":\"success\",\"lat\":41.7,\"lon\":-111.8}",
                     &lat, &lon, city, sizeof(city)));
    TEST_ASSERT_EQUAL_STRING("", city);   /* emptied, not left holding stale bytes */
}

/* A city longer than the buffer is truncated and terminated, never run off the end. */
static void test_long_city_is_truncated_and_terminated(void)
{
    double lat = 0, lon = 0;
    char city[8];
    TEST_ASSERT_EQUAL_INT(0,
        geoloc_parse("{\"status\":\"success\",\"lat\":41.7,\"lon\":-111.8,"
                     "\"city\":\"A Very Long Place Name\"}", &lat, &lon, city, sizeof(city)));
    TEST_ASSERT_EQUAL_STRING_LEN("A Very ", city, 7);
    TEST_ASSERT_EQUAL_CHAR('\0', city[7]);
}

static void test_garbage_is_rejected(void)
{
    double lat = 0, lon = 0;
    TEST_ASSERT_EQUAL_INT(-1, geoloc_parse("not json at all", &lat, &lon, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, geoloc_parse("", &lat, &lon, NULL, 0));
    TEST_ASSERT_EQUAL_INT(-1, geoloc_parse(NULL, &lat, &lon, NULL, 0));
}

/* The city buffer may be omitted entirely by a caller that does not want it. */
static void test_null_city_buffer_is_allowed(void)
{
    double lat = 0, lon = 0;
    TEST_ASSERT_EQUAL_INT(0, geoloc_parse(OK, &lat, &lon, NULL, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_a_real_reply);
    RUN_TEST(test_fail_status_is_rejected);
    RUN_TEST(test_success_without_coordinates_is_rejected);
    RUN_TEST(test_string_coordinates_are_rejected);
    RUN_TEST(test_out_of_range_coordinates_are_rejected);
    RUN_TEST(test_zero_coordinates_are_accepted);
    RUN_TEST(test_missing_city_is_not_a_failure);
    RUN_TEST(test_long_city_is_truncated_and_terminated);
    RUN_TEST(test_garbage_is_rejected);
    RUN_TEST(test_null_city_buffer_is_allowed);
    return UNITY_END();
}
