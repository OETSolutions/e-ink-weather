#include <math.h>
#include "unity.h"
#include "alerts.h"

void setUp(void) {}
void tearDown(void) {}

static void test_gt_is_strict(void)
{
    alert_rule_t r = { ALERT_OP_GT, 100.0, ALERT_SEVERE };
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE, alerts_eval(&r, 100.1));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE,   alerts_eval(&r, 100.0));  /* boundary excluded */
}

static void test_gte_includes_boundary(void)
{
    alert_rule_t r = { ALERT_OP_GTE, 100.0, ALERT_SEVERE };
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE, alerts_eval(&r, 100.0));
}

/* A sensor reporting "unavailable" must never raise a weather alarm (FR-5b). */
static void test_non_finite_never_alarms(void)
{
    alert_rule_t r = { ALERT_OP_LT, 0.0, ALERT_SEVERE };
    TEST_ASSERT_EQUAL_INT(ALERT_NONE, alerts_eval(&r, NAN));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE, alerts_eval(&r, INFINITY));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE, alerts_eval(&r, -INFINITY));
}

static void test_most_severe_wins(void)
{
    alert_rule_t rs[] = {
        { ALERT_OP_GT,  90.0, ALERT_ADVISORY },
        { ALERT_OP_GT, 100.0, ALERT_SEVERE   },
    };
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE,   alerts_eval_all(rs, 2, 105.0));
    TEST_ASSERT_EQUAL_INT(ALERT_ADVISORY, alerts_eval_all(rs, 2,  95.0));
    TEST_ASSERT_EQUAL_INT(ALERT_NONE,     alerts_eval_all(rs, 2,  50.0));
}

static void test_level_names(void)
{
    TEST_ASSERT_EQUAL_STRING("none",     alerts_level_name(ALERT_NONE));
    TEST_ASSERT_EQUAL_STRING("advisory", alerts_level_name(ALERT_ADVISORY));
    TEST_ASSERT_EQUAL_STRING("warning",  alerts_level_name(ALERT_WARNING));
    TEST_ASSERT_EQUAL_STRING("severe",   alerts_level_name(ALERT_SEVERE));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_gt_is_strict);
    RUN_TEST(test_gte_includes_boundary);
    RUN_TEST(test_non_finite_never_alarms);
    RUN_TEST(test_most_severe_wins);
    RUN_TEST(test_level_names);
    return UNITY_END();
}
