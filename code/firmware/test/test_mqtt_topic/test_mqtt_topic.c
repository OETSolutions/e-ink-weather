#include "unity.h"
#include "ha_mqtt.h"
#include <string.h>

/* FR-5c. The topic string is the entire addressing scheme for MQTT — there is no
 * request/response to correct a mistake — so a wrong topic silently subscribes to
 * nothing (or to somebody else's entity) and the glass shows a stale value forever.
 * These tests pin the shape in both directions. */

void setUp(void) {}
void tearDown(void) {}

static void test_topic_builds_from_entity_id(void)
{
    char buf[128];
    int n = ha_mqtt_state_topic(buf, sizeof(buf), "homeassistant", "sensor.outdoor_temp");
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/outdoor_temp/state", buf);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);
}

static void test_topic_rejects_invalid_entity(void)
{
    char buf[128];
    /* No dot: not a full entity id. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "ha", "outdoor_temp"));
    /* Empty object_id. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "ha", "sensor."));
    /* A '/' would retarget the subscription; '#' and '+' are MQTT wildcards. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "ha", "sensor.a/b"));
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "ha", "sensor.#"));
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "ha", "sensor.+"));
    /* Uppercase is not in HA's entity-id grammar. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "ha", "sensor.Outdoor"));
}

static void test_topic_reports_overflow_instead_of_clipping(void)
{
    char buf[16];
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_state_topic(buf, sizeof(buf), "homeassistant",
                                                  "sensor.outdoor_temp"));
}

static void test_entity_recovered_from_topic(void)
{
    char out[64];
    int n = ha_mqtt_entity_from_topic("homeassistant/sensor/outdoor_temp/state",
                                      "homeassistant", out, sizeof(out));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("sensor.outdoor_temp", out);
    TEST_ASSERT_EQUAL_INT((int)strlen(out), n);
}

static void test_topic_round_trips(void)
{
    const char *ids[] = { "sensor.outdoor_temp", "light.kitchen", "binary_sensor.door_1" };
    char topic[128], back[64];
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
        TEST_ASSERT_TRUE(ha_mqtt_state_topic(topic, sizeof(topic), "ha", ids[i]) > 0);
        TEST_ASSERT_TRUE(ha_mqtt_entity_from_topic(topic, "ha", back, sizeof(back)) > 0);
        TEST_ASSERT_EQUAL_STRING(ids[i], back);
    }
}

static void test_entity_rejects_foreign_and_deep_topics(void)
{
    char out[64];
    /* Different base topic entirely. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic("other/sensor/temp/state",
                                                        "homeassistant", out, sizeof(out)));
    /* An attribute topic sits under the same prefix — it is NOT the state. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/outdoor_temp/attributes/battery", "homeassistant",
        out, sizeof(out)));
    /* Trailing segment after /state must not be parsed as if it ended at /state. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/outdoor_temp/state/extra", "homeassistant", out, sizeof(out)));
    /* A topic that merely STARTS with the base but is not under it. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic("homeassistantx/sensor/t/state",
                                                        "homeassistant", out, sizeof(out)));
    /* Missing the state tail. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic("homeassistant/sensor/temp",
                                                        "homeassistant", out, sizeof(out)));
    /* Wildcard subscription topic is not a concrete entity. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic("homeassistant/sensor/+/state",
                                                        "homeassistant", out, sizeof(out)));
    /* Domain present but object_id empty. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic("homeassistant/sensor//state",
                                                        "homeassistant", out, sizeof(out)));
    /* Two separators in the body. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic("homeassistant/a/b/c/state",
                                                        "homeassistant", out, sizeof(out)));
    /* "/state" appearing EARLIER in the path must not be taken as the tail: a naive
     * "contains /state" match would accept this and report sensor.state_x as
     * sensor.state_ — silently subscribing to the wrong entity. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/state_x/state/extra", "homeassistant", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic(
        "homeassistant/state_x/state", "homeassistant", out, sizeof(out)));
    /* A topic ending in "state2" must be rejected. A "contains /state" match would accept
     * it and, worse, chop the last character off the object_id to do so — reporting
     * sensor.temp for a topic that is not sensor.temp's. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/temp/state2", "homeassistant", out, sizeof(out)));
}

static void test_entity_rejects_output_overflow(void)
{
    char out[8];
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_entity_from_topic(
        "homeassistant/sensor/outdoor_temp/state", "homeassistant", out, sizeof(out)));
}

static void test_unquote_strips_json_string_quotes(void)
{
    char out[64];
    int n = ha_mqtt_unquote("\"on\"", out, sizeof(out));
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("on", out);
}

static void test_unquote_leaves_bare_payloads_alone(void)
{
    char out[64];
    /* Numbers and booleans are published unquoted; they must pass through unchanged. */
    TEST_ASSERT_EQUAL_INT(5, ha_mqtt_unquote("23.75", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("23.75", out);
    TEST_ASSERT_EQUAL_INT(4, ha_mqtt_unquote("true", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("true", out);
    /* A lone quote character is not a quoted string. */
    TEST_ASSERT_EQUAL_INT(1, ha_mqtt_unquote("\"", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("\"", out);
}

static void test_unquote_handles_escapes(void)
{
    char out[64];
    /* "a\"b\\c" -> a"b\c */
    TEST_ASSERT_EQUAL_INT(5, ha_mqtt_unquote("\"a\\\"b\\\\c\"", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\"b\\c", out);

    /* \t IS a defined JSON escape, so it must become a real tab, not the two
     * characters backslash+t. */
    TEST_ASSERT_EQUAL_INT(6, ha_mqtt_unquote("\"C:\\temp\"", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT('\t', out[2]);
    TEST_ASSERT_EQUAL_STRING("C:\temp", out);

    /* An escape JSON does not define must keep its backslash verbatim rather than
     * silently eating both characters. */
    TEST_ASSERT_EQUAL_INT(4, ha_mqtt_unquote("\"a\\qb\"", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\\qb", out);

    /* A trailing bare backslash must not consume the closing quote or run off the end. */
    TEST_ASSERT_EQUAL_INT(2, ha_mqtt_unquote("\"a\\\\\"", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("a\\", out);
}

static void test_unquote_rejects_overflow(void)
{
    char out[4];
    /* Silently clipping would let a long state masquerade as a short one. */
    TEST_ASSERT_EQUAL_INT(-1, ha_mqtt_unquote("\"abcdefghij\"", out, sizeof(out)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_topic_builds_from_entity_id);
    RUN_TEST(test_topic_rejects_invalid_entity);
    RUN_TEST(test_topic_reports_overflow_instead_of_clipping);
    RUN_TEST(test_entity_recovered_from_topic);
    RUN_TEST(test_topic_round_trips);
    RUN_TEST(test_entity_rejects_foreign_and_deep_topics);
    RUN_TEST(test_entity_rejects_output_overflow);
    RUN_TEST(test_unquote_strips_json_string_quotes);
    RUN_TEST(test_unquote_leaves_bare_payloads_alone);
    RUN_TEST(test_unquote_handles_escapes);
    RUN_TEST(test_unquote_rejects_overflow);
    return UNITY_END();
}
