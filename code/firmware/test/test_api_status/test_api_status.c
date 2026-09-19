#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "api_status.h"
#include "cJSON.h"

void setUp(void) {}
void tearDown(void) {}

/* Parse the output with a real JSON parser. Asserting on substrings would pass even for
 * malformed JSON, and a malformed status body is the one failure that makes the endpoint
 * useless exactly when it is needed. */
static cJSON *parse(const char *json)
{
    cJSON *j = cJSON_Parse(json);
    TEST_ASSERT_NOT_NULL_MESSAGE(j, json);
    return j;
}

static api_status_t full_status(void)
{
    static const char *errs[2] = { "tls: handshake timeout", "owm: 401" };
    api_status_t s;
    memset(&s, 0, sizeof(s));
    s.version = "1.2.3";
    s.uptime_s = 12345;
    s.free_heap = 197396;
    s.free_heap_min = 180000;
    s.rssi = -61;
    s.has_rssi = 1;
    s.vbat = 3.87;
    s.has_vbat = 1;
    s.vbat_source = 1;   /* battery — matches power_source_t, NOT the field's order */
    s.partials_since_full = 7;
    s.fulls_total = 3;
    s.last_refresh_age_s = 120;
    s.bitmap_slot = 1;
    s.errors[0] = errs[0];
    s.errors[1] = errs[1];
    s.error_count = 2;
    return s;
}

static void test_full_status_is_valid_json_with_all_fields(void)
{
    api_status_t s = full_status();
    char buf[1024];
    int n = api_status_json(&s, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    TEST_ASSERT_EQUAL_INT((int)strlen(buf), n);

    cJSON *j = parse(buf);
    TEST_ASSERT_EQUAL_STRING("1.2.3", cJSON_GetObjectItem(j, "version")->valuestring);
    TEST_ASSERT_EQUAL_INT(12345, cJSON_GetObjectItem(j, "uptime_s")->valueint);
    TEST_ASSERT_EQUAL_INT(197396, cJSON_GetObjectItem(j, "free_heap")->valueint);
    TEST_ASSERT_EQUAL_INT(180000, cJSON_GetObjectItem(j, "free_heap_min")->valueint);
    TEST_ASSERT_EQUAL_INT(-61, cJSON_GetObjectItem(j, "rssi")->valueint);
    TEST_ASSERT_EQUAL_INT(7, cJSON_GetObjectItem(j, "partials_since_full")->valueint);
    TEST_ASSERT_EQUAL_INT(3, cJSON_GetObjectItem(j, "fulls_total")->valueint);
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetObjectItem(j, "bitmap_slot")->valueint);
    TEST_ASSERT_EQUAL_INT(120, cJSON_GetObjectItem(j, "last_refresh_age_s")->valueint);
    TEST_ASSERT_TRUE(cJSON_IsNumber(cJSON_GetObjectItem(j, "vbat")));
    TEST_ASSERT_EQUAL_STRING("battery",
                             cJSON_GetObjectItem(j, "power_source")->valuestring);

    cJSON *errs = cJSON_GetObjectItem(j, "errors");
    TEST_ASSERT_TRUE(cJSON_IsArray(errs));
    TEST_ASSERT_EQUAL_INT(2, cJSON_GetArraySize(errs));
    TEST_ASSERT_EQUAL_STRING("tls: handshake timeout",
                             cJSON_GetArrayItem(errs, 0)->valuestring);
    cJSON_Delete(j);
}

/* A quote or backslash in an error string must not end the JSON string early — that would
 * make the whole status body unparseable. */
/* A bare voltage is ambiguous — 4.29 V is either a full Li-ion pack or USB — so the source
 * must round-trip. Pinned at all three values because the encoding is numeric and an
 * off-by-one silently reports mains as battery: the field's whole purpose inverted, with
 * every other assertion still green. */
static void test_power_source_maps_every_value(void)
{
    static const struct { int in; const char *out; } cases[] = {
        { 0, "unknown" }, { 1, "battery" }, { 2, "usb" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        api_status_t s = full_status();
        s.vbat_source = cases[i].in;
        char buf[1024];
        TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
        cJSON *j = parse(buf);
        TEST_ASSERT_EQUAL_STRING(cases[i].out,
                                 cJSON_GetObjectItem(j, "power_source")->valuestring);
        cJSON_Delete(j);
    }
}

static void test_error_strings_are_escaped(void)
{
    api_status_t s = full_status();
    s.errors[0] = "bad \"quote\" and \\ backslash";
    s.error_count = 1;
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));

    cJSON *j = parse(buf);
    cJSON *errs = cJSON_GetObjectItem(j, "errors");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(errs));
    /* Round-trips to exactly the original string. */
    TEST_ASSERT_EQUAL_STRING("bad \"quote\" and \\ backslash",
                             cJSON_GetArrayItem(errs, 0)->valuestring);
    cJSON_Delete(j);
}

/* Control characters (newlines, tabs, raw 0x01) must be escaped; a raw newline inside a
 * JSON string is invalid. */
static void test_control_characters_are_escaped(void)
{
    api_status_t s = full_status();
    s.errors[0] = "line1\nline2\ttab\x01ctrl";
    s.error_count = 1;
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));

    /* No raw control byte may appear in the output. */
    for (const char *p = buf; *p; p++) {
        TEST_ASSERT_GREATER_OR_EQUAL_INT(0x20, (unsigned char)*p);
    }
    cJSON *j = parse(buf);
    TEST_ASSERT_EQUAL_STRING("line1\nline2\ttab\x01ctrl",
                             cJSON_GetArrayItem(cJSON_GetObjectItem(j, "errors"), 0)->valuestring);
    cJSON_Delete(j);
}

/* A quote in the VERSION string must also be escaped. */
static void test_version_is_escaped(void)
{
    api_status_t s = full_status();
    s.version = "1.0\"x";
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
    cJSON *j = parse(buf);
    TEST_ASSERT_EQUAL_STRING("1.0\"x", cJSON_GetObjectItem(j, "version")->valuestring);
    cJSON_Delete(j);
}

/* "No reading" must be distinguishable from a real zero. 0 dBm is a plausible (very strong)
 * signal, so conflating them would mislead a remote diagnosis. */
static void test_missing_rssi_and_vbat_are_null_not_zero(void)
{
    api_status_t s = full_status();
    s.has_rssi = 0;
    s.has_vbat = 0;
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
    cJSON *j = parse(buf);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(j, "rssi")));
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(j, "vbat")));
    cJSON_Delete(j);
}

/* "Never refreshed" must be null, not a huge or negative age. */
static void test_never_refreshed_is_null(void)
{
    api_status_t s = full_status();
    s.last_refresh_age_s = -1;
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
    cJSON *j = parse(buf);
    TEST_ASSERT_TRUE(cJSON_IsNull(cJSON_GetObjectItem(j, "last_refresh_age_s")));
    cJSON_Delete(j);
}

/* No errors is an empty array, not a missing key: the web app should not have to handle a
 * shape that changes with state. */
static void test_no_errors_is_an_empty_array(void)
{
    api_status_t s = full_status();
    s.error_count = 0;
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
    cJSON *j = parse(buf);
    cJSON *errs = cJSON_GetObjectItem(j, "errors");
    TEST_ASSERT_TRUE(cJSON_IsArray(errs));
    TEST_ASSERT_EQUAL_INT(0, cJSON_GetArraySize(errs));
    cJSON_Delete(j);
}

/* NULL entries in the error list must be skipped, not emitted as the string "null" or as a
 * malformed element. */
static void test_null_error_entries_are_skipped(void)
{
    api_status_t s = full_status();
    s.errors[0] = NULL;
    s.errors[1] = "real";
    s.error_count = 2;
    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
    cJSON *j = parse(buf);
    cJSON *errs = cJSON_GetObjectItem(j, "errors");
    TEST_ASSERT_EQUAL_INT(1, cJSON_GetArraySize(errs));
    TEST_ASSERT_EQUAL_STRING("real", cJSON_GetArrayItem(errs, 0)->valuestring);
    cJSON_Delete(j);
}

/* A too-small buffer must FAIL rather than emit a truncated document: a truncated body is
 * unparseable and would look like a device fault when the real cause is this function. */
static void test_small_buffer_fails_rather_than_truncating(void)
{
    api_status_t s = full_status();
    char buf[1024];
    int need = api_status_json(&s, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_INT(0, need);

    /* Every size below the required one must return -1. */
    for (size_t sz = 1; sz <= (size_t)need; sz++) {
        char small[1024];
        TEST_ASSERT_EQUAL_INT(-1, api_status_json(&s, small, sz));
    }
    /* Exactly enough (need + 1 for the NUL) succeeds. */
    char exact[1024];
    TEST_ASSERT_EQUAL_INT(need, api_status_json(&s, exact, (size_t)need + 1));
}

static void test_null_and_empty_inputs(void)
{
    char buf[256];
    TEST_ASSERT_EQUAL_INT(-1, api_status_json(NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, api_status_json(&(api_status_t){0}, NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(-1, api_status_json(&(api_status_t){0}, buf, 0));
    /* A zeroed status must still produce valid JSON. */
    api_status_t z;
    memset(&z, 0, sizeof(z));
    int n = api_status_json(&z, buf, sizeof(buf));
    TEST_ASSERT_GREATER_THAN_INT(0, n);
    cJSON *j = parse(buf);
    TEST_ASSERT_EQUAL_STRING("unknown", cJSON_GetObjectItem(j, "version")->valuestring);
    cJSON_Delete(j);
}

/* The error ring is bounded: a device stuck in an error loop must not grow it without
 * limit, and the NEWEST error must be the one you see. */
static void test_error_ring_keeps_newest_and_stays_bounded(void)
{
    char errs[API_STATUS_MAX_ERRORS][API_STATUS_ERROR_LEN];
    int count = 0;
    memset(errs, 0, sizeof(errs));

    for (int i = 0; i < 20; i++) {
        char msg[32];
        snprintf(msg, sizeof(msg), "err%d", i);
        TEST_ASSERT_EQUAL_INT(0, api_status_push_error(errs, &count, msg));
    }
    TEST_ASSERT_EQUAL_INT(API_STATUS_MAX_ERRORS, count);
    /* Newest first. */
    TEST_ASSERT_EQUAL_STRING("err19", errs[0]);
    TEST_ASSERT_EQUAL_STRING("err15", errs[API_STATUS_MAX_ERRORS - 1]);

    /* An over-long message must be truncated, not overflow the buffer. */
    char longmsg[256];
    memset(longmsg, 'x', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';
    TEST_ASSERT_EQUAL_INT(0, api_status_push_error(errs, &count, longmsg));
    TEST_ASSERT_EQUAL_INT(API_STATUS_ERROR_LEN - 1, (int)strlen(errs[0]));
    TEST_ASSERT_EQUAL_INT(API_STATUS_MAX_ERRORS, count);

    TEST_ASSERT_EQUAL_INT(-1, api_status_push_error(NULL, &count, "x"));
    TEST_ASSERT_EQUAL_INT(-1, api_status_push_error(errs, NULL, "x"));
}

/* The whole point: a status response built from the ring must still be valid JSON even when
 * the ring is full of truncated, control-character-laden messages. */
static void test_full_ring_produces_valid_json(void)
{
    char errs[API_STATUS_MAX_ERRORS][API_STATUS_ERROR_LEN];
    int count = 0;
    memset(errs, 0, sizeof(errs));
    for (int i = 0; i < API_STATUS_MAX_ERRORS; i++) {
        api_status_push_error(errs, &count, "e\"q\\x\n\x02");
    }
    api_status_t s = full_status();
    for (int i = 0; i < API_STATUS_MAX_ERRORS; i++) s.errors[i] = errs[i];
    s.error_count = count;

    char buf[1024];
    TEST_ASSERT_GREATER_THAN_INT(0, api_status_json(&s, buf, sizeof(buf)));
    cJSON *j = parse(buf);
    TEST_ASSERT_EQUAL_INT(API_STATUS_MAX_ERRORS,
                          cJSON_GetArraySize(cJSON_GetObjectItem(j, "errors")));
    cJSON_Delete(j);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_full_status_is_valid_json_with_all_fields);
    RUN_TEST(test_power_source_maps_every_value);
    RUN_TEST(test_error_strings_are_escaped);
    RUN_TEST(test_control_characters_are_escaped);
    RUN_TEST(test_version_is_escaped);
    RUN_TEST(test_missing_rssi_and_vbat_are_null_not_zero);
    RUN_TEST(test_never_refreshed_is_null);
    RUN_TEST(test_no_errors_is_an_empty_array);
    RUN_TEST(test_null_error_entries_are_skipped);
    RUN_TEST(test_small_buffer_fails_rather_than_truncating);
    RUN_TEST(test_null_and_empty_inputs);
    RUN_TEST(test_error_ring_keeps_newest_and_stays_bounded);
    RUN_TEST(test_full_ring_produces_valid_json);
    return UNITY_END();
}
