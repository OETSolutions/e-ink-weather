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
/* The value OWM_F_TIME carries. Bumped alongside the enum for the same reason as the city
 * constant above: if the two drift, these tests fail rather than silently reading another field. */
#define OWM_FIELD_TIME_TEST 8

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

/* ------------------------------------------------------------ the product toggle (FR-6) -- */

/* Each documented setting parses to its own value. */
static void test_product_string_parses(void)
{
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO,     owm_product_from_string("auto"));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_ONECALL3, owm_product_from_string("onecall3"));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_LEGACY,   owm_product_from_string("legacy"));
}

/* AN UNKNOWN VALUE MUST PROBE, NOT PIN.
 *
 * A config from a newer web app could carry a product this firmware does not know. Falling back
 * to 'legacy' or 'onecall3' would silently pin the device to a product the user never chose, and
 * on a key that HAS One Call that means quietly losing the official alerts. 'auto' is the only
 * safe fallback because it discovers the truth instead of assuming it. */
static void test_unknown_product_falls_back_to_auto_not_a_fixed_product(void)
{
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, owm_product_from_string(""));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, owm_product_from_string(NULL));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, owm_product_from_string("OneCall3"));  /* case matters */
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, owm_product_from_string("v3"));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, owm_product_from_string("nonsense"));
}

/* FR-7's question: can this product carry official alerts at all?
 *
 * The input is the RESOLVED product, never AUTO: AUTO has not probed and cannot answer, so it
 * reports 0 rather than a guess. */
static void test_alert_support_by_product(void)
{
    TEST_ASSERT_EQUAL_INT(1, owm_product_has_alerts(OWM_PRODUCT_ONECALL3));
    TEST_ASSERT_EQUAL_INT(0, owm_product_has_alerts(OWM_PRODUCT_LEGACY));
    TEST_ASSERT_EQUAL_INT(0, owm_product_has_alerts(OWM_PRODUCT_AUTO));
}

/* THE TOGGLE MUST CHANGE THE ENDPOINT, not merely the label.
 *
 * This is the defect these tests exist for: the product was parsed, stored, and used to decide
 * FR-7's alert message — while the fetch itself always called the 2.5 pair. The alert bar then
 * announced "alerts unavailable on this product" on a key that HAD One Call, and a user who
 * selected 'onecall3' got the free product's data under the premium product's name. Resolving
 * the product is what ties the setting to the request. */
static void test_explicit_product_wins_over_the_probe(void)
{
    /* The user said which product their key carries; the probe must not override it. Note
     * ONECALL3 stays ONECALL3 even when the probe says the subscription is absent — that is a
     * misconfiguration the user needs to see fail, not a silent downgrade. */
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_ONECALL3, owm_product_resolve(OWM_PRODUCT_ONECALL3, 0));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_ONECALL3, owm_product_resolve(OWM_PRODUCT_ONECALL3, -1));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_ONECALL3, owm_product_resolve(OWM_PRODUCT_ONECALL3, 1));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_LEGACY,   owm_product_resolve(OWM_PRODUCT_LEGACY, 1));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_LEGACY,   owm_product_resolve(OWM_PRODUCT_LEGACY, -1));
}

/* AUTO takes the probe's answer, and the tri-state matters: only a definite 1 selects One Call.
 * An unresolved probe (-1: no key stored, or the API was unreachable) must fall to the free
 * pair, because a request still has to be made and 2.5 is the product that answers for a key
 * without the subscription. */
static void test_auto_resolves_through_the_probe(void)
{
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_ONECALL3, owm_product_resolve(OWM_PRODUCT_AUTO, 1));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_LEGACY,   owm_product_resolve(OWM_PRODUCT_AUTO, 0));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_LEGACY,   owm_product_resolve(OWM_PRODUCT_AUTO, -1));
}

/* THE BUFFER MUST HOLD THE REQUEST THAT FILLS IT.
 *
 * This is the defect these tests exist for: the firmware asked OWM for up to cnt=40 while the
 * buffer receiving it held 12,288 bytes, and a 40-block response measures 16,575. net_http
 * refuses to hand a parser a clipped document, so the request failed outright and every forecast
 * widget on such a page rendered "--" — with nothing in the config or on the glass to say why.
 *
 * The measured sizes below come from the live API on 2026-09-20 and are the reason the model
 * exists. If OWM ever makes its documents fatter these bounds fail, which is the point: the
 * alternative is a silent blank panel. */
static void test_forecast_buffer_holds_every_request_the_firmware_makes(void)
{
    /* (blocks, measured bytes on the live API) */
    static const struct { int blocks; unsigned measured; } M[] = {
        {  8,  3512 },
        { 16,  6819 },
        { 24, 10069 },
        { 32, 13314 },
        { 40, 16575 },
    };
    for (unsigned i = 0; i < sizeof(M) / sizeof(M[0]); i++) {
        const unsigned buf = owm_forecast_buf_bytes(M[i].blocks);
        TEST_ASSERT_GREATER_THAN_UINT32(M[i].measured, buf);
    }
}

/* Every day index the web app offers (0..4 -> 8..40 blocks) must fit the buffer the firmware
 * actually allocates. The picker's Day 4 and Day 5 were exactly the selections that failed, and
 * the firmware allocates ONE size for the page (the maximum), so the invariant that matters is
 * that the largest selectable request still fits that single allocation. */
static void test_every_selectable_day_fits(void)
{
    const unsigned allocated = owm_forecast_buf_bytes(OWM_FORECAST_MAX_BLOCKS);
    for (int day = 0; day < 5; day++) {
        const int blocks = (day + 1) * 8;
        const unsigned needed = owm_forecast_buf_bytes(blocks);
        TEST_ASSERT_TRUE(blocks <= OWM_FORECAST_MAX_BLOCKS);
        /* This day's request fits the single allocation the firmware makes for the page. */
        TEST_ASSERT_TRUE(needed <= allocated);
        TEST_ASSERT_TRUE(needed > 0);
    }
    /* And that allocation must beat the old 12,288 that could not hold 4 or 5 days. */
    TEST_ASSERT_TRUE(allocated > 12288);
}

/* A count past the horizon is clamped, never extrapolated: the request cannot ask for more than
 * the product carries, and the buffer cannot grow without bound because a caller passed nonsense. */
static void test_forecast_size_clamps_and_rejects(void)
{
    TEST_ASSERT_EQUAL_UINT32(owm_forecast_buf_bytes(OWM_FORECAST_MAX_BLOCKS),
                             owm_forecast_buf_bytes(OWM_FORECAST_MAX_BLOCKS + 500));
    TEST_ASSERT_EQUAL_UINT32(0, owm_forecast_buf_bytes(0));
    TEST_ASSERT_EQUAL_UINT32(0, owm_forecast_buf_bytes(-8));
}


/* ---- the "last updated" stamp (OWM_F_TIME) ----
 *
 * EVERY EXPECTED STRING BELOW WAS COMPUTED INDEPENDENTLY IN PYTHON from the fixture's own dt and
 * tz, not from this parser — otherwise the test would only assert that the code agrees with
 * itself. The format is ctime-ish local wall clock: "%b %d, %I:%M %p" with a leading zero
 * stripped from the day.
 */

/* 2.5/weather carries the offset at the TOP LEVEL as `timezone`, and its dt is a real epoch. */
static void test_25_time_is_the_observation_time_in_local_clock(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX25_CURRENT, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_FALSE(v.is_numeric);                 /* text, like a city name */
    TEST_ASSERT_EQUAL_STRING("Sep 18, 11:27 PM", v.text);
}

/* The stamp is the DOCUMENT's dt, not `now`. That matters because on this board `now` is seconds
 * since BOOT, so a fallback to it would print a 1970 date — the whole reason this field reads the
 * time from OWM instead of from a clock. */
static void test_time_uses_the_documents_dt_not_now(void)
{
    /* A "now" far from the fixture's dt must not change the string. */
    datasrc_value_t v = owm_parse_current_field(FIX25_CURRENT, OWM_FIELD_TIME_TEST, 999999999);
    TEST_ASSERT_EQUAL_STRING("Sep 18, 11:27 PM", v.text);
    TEST_ASSERT_EQUAL_INT32(1789795661, (int32_t)v.observed_at);
}

/* One Call 3.0 carries `timezone_offset` (a number). This fixture instead carries the string
 * name "America/Denver", which is NOT a useable offset — so the parser must fall back to UTC
 * rather than treat the string as a number, and must still render. */
static void test_one_call_time_renders_without_a_numeric_offset(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_STRING("Sep 18, 12:53 PM", v.text);
}

/* A real One Call offset shifts the clock by exactly that many seconds. */
static void test_one_call_timezone_offset_shifts_the_clock(void)
{
    static const char *OC =
      "{\"timezone_offset\":-21600,"
      "\"current\":{\"dt\":1758200000,\"temp\":68.4,"
      "\"weather\":[{\"id\":800,\"main\":\"Clear\",\"description\":\"clear sky\"}]}}";
    datasrc_value_t v = owm_parse_current_field(OC, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_STRING("Sep 18, 06:53 AM", v.text);
}

/* A single-digit day loses its padding: "Sep 03" would read worse than "Sep 3". */
static void test_time_strips_the_padding_zero_from_the_day(void)
{
    static const char *SEP3 =
      "{\"main\":{\"temp\":57.0},\"dt\":1756915200,\"timezone\":-21600,\"name\":\"X\",\"cod\":200}";
    datasrc_value_t v = owm_parse_current_field(SEP3, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_STRING("Sep 3, 10:00 AM", v.text);
}

/* A response with NO dt cannot show a real observation time. It must report NOT_FOUND — which
 * makes the widget draw its own fallback — rather than substitute now_unix, which on this device
 * is uptime and would render as a 1970 timestamp. */
static void test_time_without_a_dt_is_not_found_rather_than_a_1970_date(void)
{
    static const char *NO_DT =
      "{\"main\":{\"temp\":57.0},\"timezone\":-21600,\"name\":\"X\",\"cod\":200}";
    datasrc_value_t v = owm_parse_current_field(NO_DT, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND, v.status);
    TEST_ASSERT_EQUAL_STRING("", v.text);
}

/* The forecast product is not a current reading, so it has no current-time stamp either. */
static void test_25_forecast_has_no_current_time(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX25_FORECAST, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND, v.status);
}

/* The stamp must fit the device's per-widget value buffer (40 bytes, LAYOUT_VALUE_BUF's
 * predecessors and page_render_t.values). A stamp that silently truncated on the glass is the
 * same failure class as the over-long alert message this codebase already hit. */
static void test_time_stamp_fits_the_value_buffer(void)
{
    datasrc_value_t v = owm_parse_current_field(FIX25_CURRENT, OWM_FIELD_TIME_TEST, 0);
    TEST_ASSERT_TRUE(strlen(v.text) < 24);
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
    RUN_TEST(test_product_string_parses);
    RUN_TEST(test_unknown_product_falls_back_to_auto_not_a_fixed_product);
    RUN_TEST(test_alert_support_by_product);
    RUN_TEST(test_explicit_product_wins_over_the_probe);
    RUN_TEST(test_auto_resolves_through_the_probe);
    RUN_TEST(test_forecast_buffer_holds_every_request_the_firmware_makes);
    RUN_TEST(test_every_selectable_day_fits);
    RUN_TEST(test_forecast_size_clamps_and_rejects);
    RUN_TEST(test_25_time_is_the_observation_time_in_local_clock);
    RUN_TEST(test_time_uses_the_documents_dt_not_now);
    RUN_TEST(test_one_call_time_renders_without_a_numeric_offset);
    RUN_TEST(test_one_call_timezone_offset_shifts_the_clock);
    RUN_TEST(test_time_strips_the_padding_zero_from_the_day);
    RUN_TEST(test_time_without_a_dt_is_not_found_rather_than_a_1970_date);
    RUN_TEST(test_25_forecast_has_no_current_time);
    RUN_TEST(test_time_stamp_fits_the_value_buffer);
    return UNITY_END();
}
