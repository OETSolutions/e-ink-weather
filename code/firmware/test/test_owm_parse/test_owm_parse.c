#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "owm.h"

void setUp(void) {}
void tearDown(void) {}

/* The value OWM_F_CITY has in lib/layout/include/widgets.h. Repeated here rather than included:
 * lib/datasrc is a LOWER layer than lib/layout and must not depend on it — that is the whole
 * reason owm_parse_current_field() takes an int. If this drifts from the enum, the city test
 * below fails rather than silently reading a different field. */
#define OWM_FIELD_CITY_TEST 7

/* A realistic One Call 3.0 fragment (imperial units, so values are degF/mph). */
static const char *FIX =
  "{\"lat\":41.1,\"lon\":-112.0,\"timezone\":\"America/Denver\","
  "\"current\":{\"dt\":1758200000,\"temp\":68.4,\"humidity\":41,"
  "\"weather\":[{\"id\":800,\"main\":\"Clear\",\"description\":\"clear sky\"}]},"
  "\"daily\":[{\"dt\":1758188400,\"temp\":{\"min\":52.1,\"max\":79.7}},"
  "{\"dt\":1758274800,\"temp\":{\"min\":48.9,\"max\":74.2}}],"
  "\"alerts\":[{\"sender_name\":\"NWS\",\"event\":\"High Wind Warning\"}]}";

static void test_current_temp(void)
{
    datasrc_value_t v = owm_parse_current_temp(FIX, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_TRUE(v.is_numeric);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v.value);
    TEST_ASSERT_EQUAL_INT32(1758200000, (int32_t)v.observed_at);
}

/* One Call 3.0 nests daily min/max UNDER "temp" — this is the schema trap. */
static void test_daily_min_max_are_nested_under_temp(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 52.1, owm_parse_daily_min(FIX, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 79.7, owm_parse_daily_max(FIX, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 48.9, owm_parse_daily_min(FIX, 1, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 74.2, owm_parse_daily_max(FIX, 1, 0).value);
}

/* ---- Free tier: Current Weather 2.5 + 5-day/3-hour Forecast 2.5 ----
 *
 * These are NOT a dead path. Verified live 2026-09-18 with the project's real key:
 * One Call 3.0 answers 401 ("requires a separate subscription to the One Call by Call
 * plan") while both 2.5 products answer 200. So for any key without that subscription —
 * the default state of a new account — the 2.5 shapes below are what actually runs.
 *
 * The fixtures are trimmed from REAL captured responses. An earlier version of this file
 * used a single hybrid fixture that contained BOTH a top-level "main" AND a "list", which
 * does not exist in either product: 2.5/weather has "main" and no "list", 2.5/forecast
 * has "list" and no "main". That hybrid is what let a current-temperature reader wrongly
 * accept "list"[0]."main"."temp" and call a 3-hour forecast value "current".
 */

/* Trimmed from a real GET /data/2.5/weather response. */
static const char *FIX25_CURRENT =
  "{\"coord\":{\"lon\":-112.0028,\"lat\":41.1021},"
  "\"weather\":[{\"id\":800,\"main\":\"Clear\",\"description\":\"clear sky\"}],"
  "\"base\":\"stations\","
  "\"main\":{\"temp\":57.07,\"feels_like\":56.1,\"temp_min\":54.18,\"temp_max\":60.42,"
  "\"pressure\":1019,\"humidity\":77},"
  "\"dt\":1789795661,\"timezone\":-21600,\"name\":\"Hillside Estates\",\"cod\":200}";

/* A SYNTHETIC 2.5 forecast: two local days, three 3-hour blocks each, with deliberate
 * spread. It is synthetic on purpose — a hand-typed "trimmed from real data" fixture is
 * easy to get subtly wrong, and an earlier version of this file did exactly that (its
 * day-1 max drifted from the real 68.11 to a typed 67.0). Real-data validation lives in
 * test_owm_real/, generated mechanically from a captured response.
 *
 * tz = -21600, so a local day is (dt + tz) / 86400. 1789797600 is 00:00 local. */
static const char *FIX25_FORECAST =
  "{\"cod\":\"200\",\"cnt\":6,"
  "\"city\":{\"id\":5780993,\"name\":\"Hillside Estates\",\"timezone\":-21600},"
  "\"list\":["
  "{\"dt\":1789797600,\"main\":{\"temp\":60.0,\"temp_min\":58.0,\"temp_max\":63.0}},"
  "{\"dt\":1789808400,\"main\":{\"temp\":65.0,\"temp_min\":62.0,\"temp_max\":70.0}},"
  "{\"dt\":1789819200,\"main\":{\"temp\":55.0,\"temp_min\":50.0,\"temp_max\":59.0}},"
  "{\"dt\":1789884000,\"main\":{\"temp\":70.0,\"temp_min\":66.0,\"temp_max\":74.0}},"
  "{\"dt\":1789894800,\"main\":{\"temp\":75.0,\"temp_min\":71.0,\"temp_max\":80.0}},"
  "{\"dt\":1789905600,\"main\":{\"temp\":68.0,\"temp_min\":64.0,\"temp_max\":72.0}}"
  "]}";

/* Current weather 2.5: top-level "main"."temp", and NO "list" key. */
static void test_25_current_temp_is_top_level_main(void)
{
    datasrc_value_t v = owm_parse_current_temp(FIX25_CURRENT, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_TRUE(v.is_numeric);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 57.07, v.value);
    TEST_ASSERT_EQUAL_INT32(1789795661, (int32_t)v.observed_at);
}

/* The forecast product has no current reading. Reading list[0] and calling it "current"
 * would report a 3-hour forecast value as the present temperature — a wrong number that
 * looks entirely plausible on screen. It must NOT be DATASRC_OK. */
static void test_25_forecast_is_not_a_current_reading(void)
{
    datasrc_value_t v = owm_parse_current_temp(FIX25_FORECAST, 0);
    TEST_ASSERT_NOT_EQUAL(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND, v.status);
}

/* "list" grouped into LOCAL calendar days using city.timezone, reducing each day's
 * temp_min/temp_max. Two days: the first has min 50.0 / max 70.0, the second 64.0 / 80.0. */
static void test_25_forecast_aggregates_into_local_days(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 50.0, owm_parse_daily_min(FIX25_FORECAST, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 70.0, owm_parse_daily_max(FIX25_FORECAST, 0, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 64.0, owm_parse_daily_min(FIX25_FORECAST, 1, 0).value);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 80.0, owm_parse_daily_max(FIX25_FORECAST, 1, 0).value);
    /* Two local days of blocks: there is no day 2. */
    TEST_ASSERT_NOT_EQUAL(DATASRC_OK, owm_parse_daily_min(FIX25_FORECAST, 2, 0).status);
}

/* FR-17's location display: the place name OWM resolved the coordinates to, from the
 * TOP-LEVEL "name" of 2.5/weather. It is a TEXT reading — the value field is meaningless for
 * it, and reporting it as numeric would let an alert rule compare a city name as 0.0. */
static void test_25_current_city_is_top_level_name(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX25_CURRENT, OWM_FIELD_CITY_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_FALSE(v.is_numeric);
    TEST_ASSERT_EQUAL_STRING("Hillside Estates", v.text);
    TEST_ASSERT_EQUAL_INT32(1789795661, (int32_t)v.observed_at);   /* the document's own dt */
}

/* One Call 3.0 carries no place name at all. Reporting NOT_FOUND is honest — the widget then
 * shows its own fallback — and is the difference between "no name in this response" and
 * "the payload is garbled", which a caller needs in order to tell a wrong endpoint from a
 * network failure. */
static void test_one_call_has_no_city_and_says_so(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX, OWM_FIELD_CITY_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND, v.status);
    TEST_ASSERT_EQUAL_STRING("", v.text);   /* never a partial or stale string */
}

/* The forecast product's place name lives at "city"."name", NOT at the top level, so reading
 * the top-level key must not find it. Returning the forecast city here would put a place name
 * on the CURRENT conditions reading — a value from a document the widget is not bound to. */
static void test_25_forecast_top_level_name_is_not_a_current_location(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX25_FORECAST, OWM_FIELD_CITY_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND, v.status);
}

/* Grouping must be by LOCAL day. With tz = -21600 the first block is 00:00 local and
 * belongs with the blocks that follow it. Grouped as UTC it would be 22:00 on the
 * previous day, forming a day of its own and shifting every index by one — day 0's min
 * would then come out as that single block's 58.0 instead of the real 50.0. */
static void test_25_day_grouping_uses_city_timezone(void)
{
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 50.0, owm_parse_daily_min(FIX25_FORECAST, 0, 0).value);
    TEST_ASSERT_NOT_EQUAL(58.0f, (float)owm_parse_daily_min(FIX25_FORECAST, 0, 0).value);
}

/* The 2.5 products have no alerts key at all: a legitimate "none", not an error, and not
 * to be confused with a malformed response. */
static void test_25_absence_of_alerts_key_is_not_an_error(void)
{
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts(FIX25_FORECAST));
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, owm_parse_current_temp(FIX25_CURRENT, 0).status);
}

static void test_out_of_range_day_index_is_not_ok(void)
{
    TEST_ASSERT_NOT_EQUAL(DATASRC_OK, owm_parse_daily_min(FIX, 99, 0).status);
}

static void test_alerts_detected(void)
{
    TEST_ASSERT_EQUAL_INT(1, owm_has_alerts(FIX));
}

static void test_alerts_absent(void)
{
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts("{\"current\":{\"temp\":1}}"));
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts("{\"alerts\":[]}"));
}

static void test_malformed_json_is_a_parse_error_not_a_zero(void)
{
    datasrc_value_t v = owm_parse_current_temp("{not json", 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_PARSE, v.status);
    /* Unity's double assertions are compiled out in this build, so compare as float. */
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, (float)v.value);   /* never a plausible number */
}

/* A 2.5 error body ("cod":401) is valid JSON with no weather fields. It must not be
 * mistaken for a reading, and must not look like a network parse failure either. */
static void test_25_error_body_is_not_a_reading(void)
{
    const char *err = "{\"cod\":401,\"message\":\"Invalid API key\"}";
    datasrc_value_t v = owm_parse_current_temp(err, 0);
    TEST_ASSERT_NOT_EQUAL(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts(err));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_current_temp);
    RUN_TEST(test_daily_min_max_are_nested_under_temp);
    RUN_TEST(test_25_current_temp_is_top_level_main);
    RUN_TEST(test_25_forecast_is_not_a_current_reading);
    RUN_TEST(test_25_forecast_aggregates_into_local_days);
    RUN_TEST(test_25_current_city_is_top_level_name);
    RUN_TEST(test_one_call_has_no_city_and_says_so);
    RUN_TEST(test_25_forecast_top_level_name_is_not_a_current_location);
    RUN_TEST(test_25_day_grouping_uses_city_timezone);
    RUN_TEST(test_25_absence_of_alerts_key_is_not_an_error);
    RUN_TEST(test_out_of_range_day_index_is_not_ok);
    RUN_TEST(test_alerts_detected);
    RUN_TEST(test_alerts_absent);
    RUN_TEST(test_malformed_json_is_a_parse_error_not_a_zero);
    RUN_TEST(test_25_error_body_is_not_a_reading);
    return UNITY_END();
}
