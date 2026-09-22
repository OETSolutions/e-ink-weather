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

/* Mirrors the real NVS backend's terminator rule: the read writes a NUL after the blob, so a
 * document that EXACTLY fills the buffer is refused rather than returned unterminated.
 *
 * WHY THE STUB MUST MODEL THIS: the real backend does it, and the stub did not — so a bug that
 * allocated the bare stored length (no room for the NUL) passed every host test and then failed
 * on the device, where the config silently reverted to the built-in default. A stub that is
 * more permissive than the thing it stands in for cannot catch that class of defect. */
static int stub_read(void *ctx, const char *key, void *out, size_t max, size_t *len)
{
    (void)ctx; (void)key;
    if (g_read_fails) return -1;
    if (g_len == 0) return -1;              /* nothing stored */
    if (g_len + 1 > max) return -1;         /* no room for the terminator */
    memcpy(out, g_blob, g_len);
    ((char *)out)[g_len] = '\0';
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

/* A widget id or label could contain the literal text `"schemaVersion"` as a VALUE. The reader
 * scans the text rather than building a cJSON tree (see cfg_store.c for why), so it must tell a
 * KEY from a value that merely looks like one — otherwise such a document would be read with a
 * bogus version and refused, or worse, accepted as a version it is not. */
static void test_schema_version_lookalike_string_value_is_not_the_key(void)
{
    reset();
    cfg_store_t s = store();
    /* The generator's value IS the string `schemaVersion`, so its own quotes form a bare
     * `"schemaVersion"` in the raw bytes — and it comes FIRST. This is the only way a VALID
     * document can hold that literal outside a key (an unescaped quote mid-value would not be
     * valid JSON), which is exactly why the guard must be tested with this shape. A naive strstr
     * reads a version from here and gets it wrong; the scan must require a key. */
    const char *doc = "{\"generator\":\"schemaVersion\",\"schemaVersion\":1,"
                      "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":900,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, doc));
    TEST_ASSERT_EQUAL_INT(1, g_writes);

    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    TEST_ASSERT_NOT_NULL(json);
    free(json);
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

/* ---------------------------------------------------------------- allocation sizing ---- */

/* The `size` hook reports the stored blob's length without reading it. */
static size_t stub_size(void *ctx, const char *key)
{
    (void)ctx; (void)key;
    return g_len;
}

/* A read must ask for the STORED length, not the 16,384-byte maximum.
 *
 * WHY THIS IS A TEST AND NOT A COMMENT: this over-request was the difference between the web
 * app saving a layout and getting HTTP 500 {"error":"oom"}. The shipped config is ~4.6 KB, so
 * the maximum is ~3.5x more than any read needs — and on this part a 16 KB request fails on a
 * heap whose largest free block is momentarily 13-17 KB, which is the normal state under HTTP
 * load. `max_seen` records what the backend was actually asked for, which is the only place
 * this is observable: the caller sees a correct document either way. */
static size_t g_max_seen;
static int stub_read_record(void *ctx, const char *key, void *out, size_t max, size_t *len)
{
    g_max_seen = max;
    return stub_read(ctx, key, out, max, len);
}

static void test_read_sizes_from_stored_length_not_the_maximum(void)
{
    reset();
    g_max_seen = 0;
    cfg_store_t s = { .read = stub_read_record, .write = stub_write,
                      .size = stub_size, .ctx = NULL };

    const char *doc = "{\"schemaVersion\":1,\"updateSeconds\":600,"
                      "\"partialRefreshLimit\":10,"
                      "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":300,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, doc));

    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    TEST_ASSERT_NOT_NULL(json);
    /* The whole point: comfortably under the 16 KB maximum, with room for the NUL. */
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(strlen(doc) + 1, g_max_seen);
    TEST_ASSERT_LESS_THAN_UINT32(CFG_JSON_MAX_LEN, g_max_seen);
    free(json);
}

/* Without a `size` hook the read still works — it just falls back to the maximum, which is
 * correct and merely larger than necessary. A stub-only backend must not break. */
static void test_read_without_size_hook_falls_back_to_the_maximum(void)
{
    reset();
    g_max_seen = 0;
    cfg_store_t s = { .read = stub_read_record, .write = stub_write, .ctx = NULL };

    const char *doc = "{\"schemaVersion\":1,\"updateSeconds\":600,"
                      "\"partialRefreshLimit\":10,"
                      "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":300,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, doc));

    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL_UINT32(CFG_JSON_MAX_LEN, g_max_seen);
    free(json);
}

/* A stored length at or above the cap must not be trusted into a too-small buffer: the read
 * keeps the cap and the backend reports the error, rather than the document being truncated. */
static void test_over_long_stored_blob_is_not_read_into_a_short_buffer(void)
{
    reset();
    g_max_seen = 0;
    cfg_store_t s = { .read = stub_read_record, .write = stub_write,
                      .size = stub_size, .ctx = NULL };

    /* Forge a stored blob at the cap, bypassing the write path's own limits. */
    g_len = CFG_JSON_MAX_LEN;
    char *json = NULL;
    const int rc = cfg_store_get(&s, &json);
    /* Either the read is refused, or it used the full cap; what must NOT happen is a silent
     * truncation into a buffer sized below the document. */
    if (rc == 0) {
        TEST_ASSERT_NOT_NULL(json);
        free(json);
    }
    TEST_ASSERT_EQUAL_UINT32(CFG_JSON_MAX_LEN, g_max_seen);
}

/* THE REGRESSION: the read must leave room for the NUL terminator the backend writes.
 *
 * Sizing the buffer to the bare stored length fails every read against the real NVS backend,
 * which refuses a blob that exactly fills the buffer. On the device that surfaced as the config
 * silently reverting to the built-in default — a device with a saved layout rendering the empty
 * one — while every host test passed, because the stub was more permissive than the backend.
 * The stub now models the terminator rule, so this is caught here instead. */
static void test_read_leaves_room_for_the_terminator(void)
{
    reset();
    g_max_seen = 0;
    cfg_store_t s = { .read = stub_read_record, .write = stub_write,
                      .size = stub_size, .ctx = NULL };

    const char *doc = "{\"schemaVersion\":1,\"updateSeconds\":600,"
                      "\"partialRefreshLimit\":10,"
                      "\"pages\":[{\"name\":\"a\",\"refreshSeconds\":300,\"weight\":1}]}";
    TEST_ASSERT_EQUAL_INT(0, cfg_store_put(&s, doc));
    const size_t stored = strlen(doc) + 1;      /* the write stores the NUL too */

    char *json = NULL;
    TEST_ASSERT_EQUAL_INT(0, cfg_store_get(&s, &json));
    TEST_ASSERT_NOT_NULL(json);
    TEST_ASSERT_EQUAL_STRING(doc, json);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(stored + 1, g_max_seen);
    free(json);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_valid_config_is_stored);
    RUN_TEST(test_malformed_json_is_refused_and_stored_config_is_untouched);
    RUN_TEST(test_future_schema_version_is_refused);
    RUN_TEST(test_missing_or_non_numeric_schema_version_is_refused);
    RUN_TEST(test_schema_version_lookalike_string_value_is_not_the_key);
    RUN_TEST(test_semantically_invalid_config_is_refused);
    RUN_TEST(test_unconfigured_device_returns_default);
    RUN_TEST(test_stored_config_round_trips);
    RUN_TEST(test_out_of_range_values_are_not_silently_stored_as_written);
    RUN_TEST(test_null_arguments_are_rejected);
    RUN_TEST(test_read_sizes_from_stored_length_not_the_maximum);
    RUN_TEST(test_read_without_size_hook_falls_back_to_the_maximum);
    RUN_TEST(test_over_long_stored_blob_is_not_read_into_a_short_buffer);
    RUN_TEST(test_read_leaves_room_for_the_terminator);
    return UNITY_END();
}
