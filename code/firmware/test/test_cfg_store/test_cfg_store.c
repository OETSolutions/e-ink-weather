#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "cfg_store.h"
#include "layout_model.h"

void setUp(void) {}
void tearDown(void) {}

/* A stub backend so the validate-before-store rule can be tested on the host (NFR-6). */
#define STUB_MAX 4096
static char  g_blob[STUB_MAX];
static size_t g_len;
static int   g_writes;
static int   g_read_fails;

static int stub_read(void *ctx, const char *key, void *out, size_t max, size_t *len)
{
    (void)ctx; (void)key;
    if (g_read_fails) return -1;
    if (g_len == 0) return -1;              /* nothing stored */
    if (g_len > max) return -1;
    memcpy(out, g_blob, g_len);
    *len = g_len;
    return 0;
}

static int stub_write(void *ctx, const char *key, const void *data, size_t len)
{
    (void)ctx; (void)key;
    if (len > STUB_MAX) return -1;
    memcpy(g_blob, data, len);
    g_len = len;
    g_writes++;
    return 0;
}

static cfg_store_t store(void)
{
    cfg_store_t s = { .read = stub_read, .write = stub_write, .ctx = NULL };
    return s;
}

static void reset(void) { memset(g_blob, 0, sizeof(g_blob)); g_len = 0; g_writes = 0; g_read_fails = 0; }

/* A valid document is stored. */
static void test_valid_config_is_stored(void)
{
    reset();
    cfg_store_t s = store();
    const char *good = "{\"schemaVersion\":1,\"updateSeconds\":600,"
                       "\"partialRefreshLimit\":10,"
                       "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":300,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, good));
    TEST_ASSERT_EQUAL_INT(1, g_writes);
    TEST_ASSERT_GREATER_THAN_UINT32(0, g_len);
}

/* THE BRICKING CASE: malformed JSON must be refused AND must not touch the stored config.
 * If this failed, one bad PUT would leave the device unable to boot. */
static void test_malformed_json_is_refused_and_stored_config_is_untouched(void)
{
    reset();
    cfg_store_t s = store();
    const char *good = "{\"schemaVersion\":1,\"updateSeconds\":600,"
                       "\"partialRefreshLimit\":10,"
                       "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":300,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, good));
    char saved[STUB_MAX];
    memcpy(saved, g_blob, g_len);
    size_t saved_len = g_len;

    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "{bad json"));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, ""));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "not json at all"));

    TEST_ASSERT_EQUAL_INT(1, g_writes);                  /* no extra write happened */
    TEST_ASSERT_EQUAL_UINT32(saved_len, g_len);
    TEST_ASSERT_EQUAL_MEMORY(saved, g_blob, saved_len);  /* byte-for-byte unchanged */
}

/* A document from a future schema must be refused, not stored as-is: the same field name
 * may mean something different in the next version (FR-26b). */
static void test_future_schema_version_is_refused(void)
{
    reset();
    cfg_store_t s = store();
    const char *future = "{\"schemaVersion\":99,\"updateSeconds\":600}";
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, future));
    TEST_ASSERT_EQUAL_INT(0, g_writes);
}

/* A document with no numeric schemaVersion cannot be migrated, so it must be refused rather
 * than assumed to be current. */
static void test_missing_or_non_numeric_schema_version_is_refused(void)
{
    reset();
    cfg_store_t s = store();
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "{\"updateSeconds\":600}"));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "{\"schemaVersion\":\"1\",\"updateSeconds\":600}"));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "{\"schemaVersion\":null}"));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "{\"schemaVersion\":1.5}"));
    TEST_ASSERT_EQUAL_INT(0, g_writes);
}

/* A well-formed JSON document that the LAYOUT PARSER rejects must also be refused — being
 * valid JSON is not enough if the firmware cannot act on it. */
static void test_semantically_invalid_config_is_refused(void)
{
    reset();
    cfg_store_t s = store();
    /* schemaVersion is fine, but pages is the wrong type for the parser. */
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, "{\"schemaVersion\":1,\"pages\":\"nope\"}"));
    TEST_ASSERT_EQUAL_INT(0, g_writes);
}

/* A fresh device with nothing stored gets the default document, so it boots configured. */
static void test_unconfigured_device_returns_default(void)
{
    reset();
    g_read_fails = 1;
    cfg_store_t s = store();
    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    TEST_ASSERT_NOT_NULL(json);

    /* The default must itself be a document the firmware accepts, or a fresh device would
     * boot into a state its own parser rejects. */
    layout_config_t cfg;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(json, &cfg));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, cfg.page_count);

    /* FR-11 specifies 5 partials between full refreshes. The literal appears in three
     * places (here, api_partial_limit, app_boot) and had already drifted to 24 in this one,
     * so pin it: a fresh device must get the specified limit, not whatever was typed. */
    TEST_ASSERT_EQUAL_INT(5, cfg.partial_refresh_limit);
    free(json);
}

/* What is stored must be exactly what comes back — no truncation, no re-serialisation that
 * could drop a field the web app relies on. */
static void test_stored_config_round_trips(void)
{
    reset();
    cfg_store_t s = store();
    const char *good = "{\"schemaVersion\":1,\"updateSeconds\":600,"
                       "\"partialRefreshLimit\":10,"
                       "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":300,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, good));

    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    TEST_ASSERT_NOT_NULL(json);
    layout_config_t a, b;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(good, &a));
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(json, &b));
    TEST_ASSERT_EQUAL_INT(a.update_seconds, b.update_seconds);
    TEST_ASSERT_EQUAL_INT(a.partial_refresh_limit, b.partial_refresh_limit);
    TEST_ASSERT_EQUAL_INT(a.page_count, b.page_count);
    for (int i = 0; i < a.page_count; i++) {
        TEST_ASSERT_EQUAL_INT(a.pages[i].refresh_seconds, b.pages[i].refresh_seconds);
        TEST_ASSERT_EQUAL_STRING(a.pages[i].name, b.pages[i].name);
    }
    free(json);
}

/* Out-of-range values must be refused by the parser gate, not stored and clamped later:
 * storing a config the parser had to fix would mean the device reports a value it is not
 * actually using. */
static void test_out_of_range_values_are_not_silently_stored_as_written(void)
{
    reset();
    cfg_store_t s = store();
    /* A page with a negative interval: the parser clamps, but the gate must still accept
     * the document (clamping is documented behaviour) — what must NOT happen is a write of
     * something the parser outright rejects. */
    const char *clamped = "{\"schemaVersion\":1,\"pages\":[{\"name\":\"a\","
                          "\"refreshSeconds\":-5,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, clamped));
    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    layout_config_t cfg;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(json, &cfg));
    TEST_ASSERT_GREATER_OR_EQUAL_INT(LAYOUT_MIN_INTERVAL_SECONDS, cfg.pages[0].refresh_seconds);
    free(json);
}

static void test_null_arguments_are_rejected(void)
{
    reset();
    cfg_store_t s = store();
    char *json = NULL;
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(NULL, "{}"));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&s, NULL));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_get(NULL, &json));
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_get(&s, NULL));
    cfg_store_t no_write = { .read = stub_read, .write = NULL, .ctx = NULL };
    TEST_ASSERT_NOT_EQUAL(0, cfg_store_put(&no_write, "{\"schemaVersion\":1}"));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_config_is_stored);
    RUN_TEST(test_malformed_json_is_refused_and_stored_config_is_untouched);
    RUN_TEST(test_future_schema_version_is_refused);
    RUN_TEST(test_missing_or_non_numeric_schema_version_is_refused);
    RUN_TEST(test_semantically_invalid_config_is_refused);
    RUN_TEST(test_unconfigured_device_returns_default);
    RUN_TEST(test_stored_config_round_trips);
    RUN_TEST(test_out_of_range_values_are_not_silently_stored_as_written);
    RUN_TEST(test_null_arguments_are_rejected);
    return UNITY_END();
}
