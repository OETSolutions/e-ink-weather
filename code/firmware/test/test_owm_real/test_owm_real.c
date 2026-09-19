/* Validation against a REAL captured OpenWeatherMap response.
 *
 * The plan for this task asks for exactly this, and for a specific reason: a hand-written
 * fixture cannot contain the surprises real data does. It found three:
 *
 *   1. 37 of the 40 forecast blocks report temp_min == temp_max, so most of the reduction
 *      comes from the few blocks that carry a spread. A fixture with tidy, spread values
 *      would have hidden that.
 *   2. The response's first block is 00:00 LOCAL, so a UTC grouping puts it alone in a day
 *      of its own and shifts every day index by one.
 *   3. A partial day at the horizon edge would silently change the expected min/max.
 *
 * The expected values below were computed independently in Python directly from the
 * captured JSON (grouping (dt + tz) / 86400 and reducing temp_min/temp_max per day), not
 * from this parser — otherwise the test would only assert that the code agrees with
 * itself. */

#include <stdint.h>
#include "unity.h"
#include "owm.h"
#include "fixture_owm_forecast25.h"
#include "fixture_owm_weather25.h"
#include "fixture_owm_expected.h"

void setUp(void) {}
void tearDown(void) {}

/* From the real 2.5/weather capture. */
static void test_real_current_temp(void)
{
    datasrc_value_t v = owm_parse_current_temp(OWM_WEATHER25_REAL, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, OWM_REAL_CURRENT_TEMP, v.value);
    TEST_ASSERT_EQUAL_INT32((int32_t)OWM_REAL_CURRENT_DT, (int32_t)v.observed_at);
}

/* The forecast product is not a current reading, on real data too. */
static void test_real_forecast_is_not_a_current_reading(void)
{
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND,
                          owm_parse_current_temp(OWM_FORECAST25_REAL, 0).status);
}

/* Hand-computed from the capture: 40 blocks, 8 per local day, exactly 5 days.
 * The expected values come from the generated header, which is produced by an
 * independent Python grouping — never by the parser under test. */
static void test_real_forecast_days_match_hand_computation(void)
{
    for (int i = 0; i < OWM_REAL_NDAYS; i++) {
        datasrc_value_t lo = owm_parse_daily_min(OWM_FORECAST25_REAL, i, 0);
        datasrc_value_t hi = owm_parse_daily_max(OWM_FORECAST25_REAL, i, 0);
        TEST_ASSERT_EQUAL_INT(DATASRC_OK, lo.status);
        TEST_ASSERT_EQUAL_INT(DATASRC_OK, hi.status);
        TEST_ASSERT_FLOAT_WITHIN(1e-6, OWM_REAL_EXP_MIN[i], lo.value);
        TEST_ASSERT_FLOAT_WITHIN(1e-6, OWM_REAL_EXP_MAX[i], hi.value);
        TEST_ASSERT_TRUE(lo.value <= hi.value);   /* min can never exceed max */
    }
}

/* The horizon is exactly 5 days; index 5 must not invent a sixth. */
static void test_real_forecast_horizon_is_exactly_five_days(void)
{
    TEST_ASSERT_EQUAL_INT(DATASRC_OK,
                          owm_parse_daily_min(OWM_FORECAST25_REAL, OWM_REAL_NDAYS - 1, 0).status);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND,
                          owm_parse_daily_min(OWM_FORECAST25_REAL, OWM_REAL_NDAYS, 0).status);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_NOT_FOUND,
                          owm_parse_daily_max(OWM_FORECAST25_REAL, OWM_REAL_NDAYS, 0).status);
}

/* Day 0 must contain the first block. Under a UTC grouping the first block (22:00 UTC on
 * the previous day) would form a day of its own and shift every index, so day 0's min
 * would come out as that single block's 56.43 instead of the real 54.18. */
static void test_real_day_zero_is_a_local_day_not_a_utc_fragment(void)
{
    datasrc_value_t d0 = owm_parse_daily_min(OWM_FORECAST25_REAL, 0, 0);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, d0.status);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, OWM_REAL_EXP_MIN[0], d0.value);
    /* Under a UTC grouping day 0 would hold only the first block, whose temp_min is
     * strictly greater than the real local-day minimum. */
    TEST_ASSERT_TRUE((float)d0.value < 56.43f);
}

/* Neither 2.5 product has alerts; that is "none", not an error. */
static void test_real_25_products_report_no_alerts(void)
{
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts(OWM_FORECAST25_REAL));
    TEST_ASSERT_EQUAL_INT(0, owm_has_alerts(OWM_WEATHER25_REAL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_real_current_temp);
    RUN_TEST(test_real_forecast_is_not_a_current_reading);
    RUN_TEST(test_real_forecast_days_match_hand_computation);
    RUN_TEST(test_real_forecast_horizon_is_exactly_five_days);
    RUN_TEST(test_real_day_zero_is_a_local_day_not_a_utc_fragment);
    RUN_TEST(test_real_25_products_report_no_alerts);
    return UNITY_END();
}
