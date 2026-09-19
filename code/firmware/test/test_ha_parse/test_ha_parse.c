#include <string.h>
#include <math.h>
#include "unity.h"
#include "ha.h"

void setUp(void) {}
void tearDown(void) {}

static void test_numeric_states_parse(void)
{
    double v = 0;
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, ha_classify_state("68.4", &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, ha_classify_state("-3", &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, -3.0, v);
}

/* HA states are strings and may be unavailable/unknown — never cast blindly (FR-5b). */
static void test_unavailable_and_junk_are_rejected(void)
{
    double v = 0;
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("unavailable", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("unknown", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state(NULL, &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("12abc", &v));
}

/* An HTTP body arrives with a trailing newline. Without trimming, the LAST entity of
 * every single response would read as unavailable. */
static void test_trailing_newline_does_not_break_the_last_value(void)
{
    double v = 0;
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, ha_classify_state("68.4\n", &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, ha_classify_state(" 41.2 ", &v));
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 41.2, v);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("\n", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("   ", &v));
}

/* strtod accepts "inf"/"nan" and would turn them into a plausible-looking reading.
 * A non-finite number must never reach the screen. */
static void test_non_finite_is_rejected(void)
{
    double v = 0;
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("inf", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("-inf", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("nan", &v));
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, ha_classify_state("1e999", &v));
}

static void test_template_line_parses_in_order(void)
{
    double v[3]; datasrc_status_t s[3];
    int n = ha_parse_template_line("68.4|41.2|unavailable", v, s, 3);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[0]); TEST_ASSERT_FLOAT_WITHIN(1e-6, 68.4, v[0]);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[1]); TEST_ASSERT_FLOAT_WITHIN(1e-6, 41.2, v[1]);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, s[2]);
}

/* A short response must leave the remaining slots UNAVAILABLE, never zero. */
static void test_short_line_marks_remainder_unavailable(void)
{
    double v[3]; datasrc_status_t s[3];
    int n = ha_parse_template_line("68.4", v, s, 3);
    TEST_ASSERT_EQUAL_INT(1, n);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, s[1]);
    TEST_ASSERT_EQUAL_INT(DATASRC_ERR_UNAVAILABLE, s[2]);
}

/* A whole rendered body, with the newline HA actually sends. */
static void test_realistic_body_parses(void)
{
    double v[3]; datasrc_status_t s[3];
    int n = ha_parse_template_line("68.4|41.2|5.2\n", v, s, 3);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[0]);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[1]);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, s[2]);
    TEST_ASSERT_FLOAT_WITHIN(1e-6, 5.2, v[2]);
}

static void test_template_body_is_built_from_entities(void)
{
    char b[256]; b[0] = '\0'; int len = 0;
    len = ha_template_add_entity(b, sizeof(b), len, "sensor.upstairs_hallway_temperature");
    len = ha_template_add_entity(b, sizeof(b), len, "sensor.64b708cfe0fc_sensor_2_temperature_f");
    TEST_ASSERT_GREATER_THAN_INT(0, len);
    TEST_ASSERT_EQUAL_STRING(
        "{{ states('sensor.upstairs_hallway_temperature') }}|"
        "{{ states('sensor.64b708cfe0fc_sensor_2_temperature_f') }}", b);
}

static void test_template_overflow_is_refused(void)
{
    char b[16]; b[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0,
                                                     "sensor.a_very_long_entity_name"));
}

/* The entity id is a trust boundary: it can be typed into the config UI and is then
 * interpolated into a Jinja template. An id containing a quote would be template
 * injection on the user's HA instance, not merely a bad request. */
static void test_entity_id_validation_rejects_injection_and_junk(void)
{
    char b[512]; b[0] = '\0';
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0,
        "sensor.x') }}{{ 7 }}{{ states('sensor.y"));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, "sensor.'"));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, "noseparator"));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, "sensor."));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, ".leading"));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, "sensor.UpperCase"));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, "sensor.with space"));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, ""));
    TEST_ASSERT_EQUAL_INT(-1, ha_template_add_entity(b, sizeof(b), 0, NULL));
    /* The real thing still works, including a leading digit in the object_id. */
    TEST_ASSERT_GREATER_THAN_INT(0, ha_template_add_entity(b, sizeof(b), 0,
                                                           "sensor.64b708cfe0fc_sensor_2"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_numeric_states_parse);
    RUN_TEST(test_unavailable_and_junk_are_rejected);
    RUN_TEST(test_trailing_newline_does_not_break_the_last_value);
    RUN_TEST(test_non_finite_is_rejected);
    RUN_TEST(test_template_line_parses_in_order);
    RUN_TEST(test_short_line_marks_remainder_unavailable);
    RUN_TEST(test_realistic_body_parses);
    RUN_TEST(test_template_body_is_built_from_entities);
    RUN_TEST(test_template_overflow_is_refused);
    RUN_TEST(test_entity_id_validation_rejects_injection_and_junk);
    return UNITY_END();
}
