#include <string.h>
#include <stdio.h>
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

/* The picker's search term is interpolated into a Jinja template, so it is a trust boundary:
 * anything Jinja would interpret must be rejected, not escaped. */
static void test_search_query_rejects_injection_metacharacters(void)
{
    TEST_ASSERT_EQUAL_INT(1, ha_search_query_valid("hallway"));
    TEST_ASSERT_EQUAL_INT(1, ha_search_query_valid("upstairs_hallway_temperature"));
    TEST_ASSERT_EQUAL_INT(1, ha_search_query_valid("sensor.64b708cfe0fc"));

    /* Jinja/JSON metacharacters: a quote would close the string literal and let the rest be
     * template code; braces delimit Jinja blocks; '%' opens a statement. */
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("'"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("x' or '1"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("{{ 7 }}"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("{% if true %}"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("a\"b"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("a&b"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("a=b"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("has space"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("Upper"));
    /* '-' is outside HA's entity-id grammar, so it cannot match an id and is refused rather
     * than passed through to a template that would treat it as literal text. */
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid("temp-2"));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid(""));
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid(NULL));

    /* Over-long input is refused rather than truncated into a different match. */
    char big[80];
    memset(big, 'a', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(0, ha_search_query_valid(big));
}

/* The picker's response is 'id|name' per line. The tail line has NO trailing newline (the
 * template's last row), and that row must not be dropped — the same off-by-one the state
 * parser was fixed for. */
static void test_entity_list_parses_all_rows_including_the_last(void)
{
    const char *body =
        "sensor.upstairs_hallway_temperature|Upstairs Hallway Temperature\n"
        "sensor.upstairs_hallway_humidity|Upstairs Hallway Humidity\n"
        "climate.upstairs_hallway|Upstairs Hallway";
    ha_entity_t rows[8];
    int total = -1;
    const int n = ha_parse_entity_list(body, rows, 8, &total);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(3, total);
    TEST_ASSERT_EQUAL_STRING("sensor.upstairs_hallway_temperature", rows[0].id);
    TEST_ASSERT_EQUAL_STRING("Upstairs Hallway Temperature", rows[0].name);
    TEST_ASSERT_EQUAL_STRING("climate.upstairs_hallway", rows[2].id);
    TEST_ASSERT_EQUAL_STRING("Upstairs Hallway", rows[2].name);
}

/* A row with no '|' is still a usable id; the name falls back to the id. And junk rows that are
 * not valid entity ids are filtered, so a stray token cannot appear in the picker. */
static void test_entity_list_tolerates_missing_name_and_filters_junk(void)
{
    const char *body = "sensor.lonely\nnot an entity\nsensor.named|Friendly\n";
    ha_entity_t rows[8];
    int total = 0;
    const int n = ha_parse_entity_list(body, rows, 8, &total);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_INT(2, total);      /* junk was not counted as a row */
    TEST_ASSERT_EQUAL_STRING("sensor.lonely", rows[0].id);
    TEST_ASSERT_EQUAL_STRING("sensor.lonely", rows[0].name);
    TEST_ASSERT_EQUAL_STRING("sensor.named", rows[1].id);
}

/* When more rows match than the caller can hold, `total` still reports the real count so the
 * app can say the list was clipped instead of presenting a short list as complete. */
static void test_entity_list_reports_truncation(void)
{
    char body[512] = {0};
    for (int i = 0; i < 10; i++) {
        char line[48];
        snprintf(line, sizeof(line), "sensor.e%d|N%d\n", i, i);
        strcat(body, line);
    }
    ha_entity_t rows[3];
    int total = 0;
    const int n = ha_parse_entity_list(body, rows, 3, &total);
    TEST_ASSERT_EQUAL_INT(3, n);
    TEST_ASSERT_EQUAL_INT(10, total);
}

static void test_entity_list_empty_and_bad_args(void)
{
    ha_entity_t rows[4];
    int total = -1;
    TEST_ASSERT_EQUAL_INT(0, ha_parse_entity_list("", rows, 4, &total));
    TEST_ASSERT_EQUAL_INT(0, total);
    TEST_ASSERT_EQUAL_INT(0, ha_parse_entity_list(NULL, rows, 4, &total));
    TEST_ASSERT_EQUAL_INT(0, ha_parse_entity_list("sensor.a", rows, 0, &total));
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
    RUN_TEST(test_search_query_rejects_injection_metacharacters);
    RUN_TEST(test_entity_list_parses_all_rows_including_the_last);
    RUN_TEST(test_entity_list_tolerates_missing_name_and_filters_junk);
    RUN_TEST(test_entity_list_reports_truncation);
    RUN_TEST(test_entity_list_empty_and_bad_args);
    return UNITY_END();
}
