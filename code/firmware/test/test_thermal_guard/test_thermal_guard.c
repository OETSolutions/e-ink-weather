#include "unity.h"
#include "thermal_guard.h"

void setUp(void) {}
void tearDown(void) {}

/* Documented panel operating range is 0..50 degC (panel datasheet TOPR). */
static void test_in_range_is_ok(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_OK, thermal_check(0,  1));
    TEST_ASSERT_EQUAL_INT(THERMAL_OK, thermal_check(22, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_OK, thermal_check(50, 1));
}

static void test_below_range_is_flagged(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_COLD, thermal_check(-1,  1));
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_COLD, thermal_check(-20, 1));
}

static void test_above_range_is_flagged(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_HOT, thermal_check(51, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_TOO_HOT, thermal_check(70, 1));
}

/* THE REGRESSION THAT MATTERS. An unusable sensor must be UNKNOWN, never TOO_COLD.
 * The SSD2677 on this bench returns a constant -15 degC; folding "unreadable" into
 * "too cold" made the guard block EVERY render forever. If this test ever fails, the
 * device renders nothing outdoors in summer. */
static void test_unusable_sensor_is_unknown_not_cold(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(-15, 0));
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(0,   0));
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(20,  0));
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(999, 0));
    TEST_ASSERT_NOT_EQUAL(THERMAL_TOO_COLD, thermal_check(-15, 0));
    TEST_ASSERT_NOT_EQUAL(THERMAL_TOO_HOT,  thermal_check(99,  0));
}

/* The documented sentinel is a failed read even if a caller wrongly marks it valid. */
static void test_sentinel_is_unknown_even_when_marked_valid(void)
{
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(PANEL_TEMP_SENTINEL_C, 1));
    TEST_ASSERT_EQUAL_INT(THERMAL_UNKNOWN, thermal_check(-200, 1));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_in_range_is_ok);
    RUN_TEST(test_below_range_is_flagged);
    RUN_TEST(test_above_range_is_flagged);
    RUN_TEST(test_unusable_sensor_is_unknown_not_cold);
    RUN_TEST(test_sentinel_is_unknown_even_when_marked_valid);
    return UNITY_END();
}
