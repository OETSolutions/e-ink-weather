#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "devcfg.h"

void setUp(void) {}
void tearDown(void) {}

static void test_same_version_is_passthrough(void)
{
    const char *in = "{\"schemaVersion\":1}";
    char *out = NULL;
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, 1, in, &out));
    TEST_ASSERT_EQUAL_STRING(in, out);
    free(out);
}

static void test_unknown_newer_version_is_refused(void)
{
    char *out = NULL;
    /* A config from the future must be refused, not silently mis-read (FR-26b). */
    TEST_ASSERT_NOT_EQUAL(0, devcfg_migrate(99, DEVCFG_SCHEMA_VERSION,
                                           "{\"schemaVersion\":99}", &out));
    TEST_ASSERT_NULL(out);
}

static void test_migrate_is_idempotent(void)
{
    char *once = NULL, *twice = NULL;
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, DEVCFG_SCHEMA_VERSION,
                                           "{\"schemaVersion\":1}", &once));
    TEST_ASSERT_EQUAL_INT(0, devcfg_migrate(1, DEVCFG_SCHEMA_VERSION, once, &twice));
    TEST_ASSERT_EQUAL_STRING(once, twice);
    free(once); free(twice);
}

/* ---------------------------------------------------------- HA url normalisation ---- */

static void test_url_strips_one_trailing_slash(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, devcfg_normalize_ha_url("http://host:8123/", out, sizeof(out)));
    /* The device appends "/api/template", so a trailing slash would make a double slash. */
    TEST_ASSERT_EQUAL_STRING("http://host:8123", out);
}

static void test_url_strips_many_trailing_slashes(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, devcfg_normalize_ha_url("https://host///", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://host", out);
}

static void test_url_without_scheme_is_refused(void)
{
    char out[64];
    /* The common typo. net_http would report a generic failure; this names the problem. */
    TEST_ASSERT_EQUAL_INT(-2, devcfg_normalize_ha_url("homeassistant.local:8123", out, sizeof(out)));
}

static void test_scheme_alone_has_no_host(void)
{
    char out[64];
    /* THE REGRESSION THIS EXISTS FOR: "http://" passes a scheme check, and a naive trailing-slash
     * strip then eats the scheme's own "//" and stores "http:". Must be refused, not mangled. */
    TEST_ASSERT_EQUAL_INT(-3, devcfg_normalize_ha_url("http://", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-3, devcfg_normalize_ha_url("http:///", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-3, devcfg_normalize_ha_url("https:///", out, sizeof(out)));
}

static void test_url_with_a_host_is_kept(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, devcfg_normalize_ha_url("http://a", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://a", out);
    TEST_ASSERT_EQUAL_INT(0, devcfg_normalize_ha_url("https://a.b.c:8123", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("https://a.b.c:8123", out);
}

static void test_url_path_is_preserved(void)
{
    char out[96];
    /* A base under a path (a reverse proxy) keeps the path and only loses the trailing slash. */
    TEST_ASSERT_EQUAL_INT(0, devcfg_normalize_ha_url("http://host/ha/", out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("http://host/ha", out);
}

static void test_url_empty_and_null_are_refused(void)
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(-1, devcfg_normalize_ha_url("", out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, devcfg_normalize_ha_url(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, devcfg_normalize_ha_url("http://host", NULL, 0));
}

static void test_url_too_long_for_buffer_is_refused(void)
{
    char small[10];
    /* Refused, not truncated: a silently shortened URL is a wrong address, not a short one. */
    TEST_ASSERT_EQUAL_INT(-1, devcfg_normalize_ha_url("http://averylonghost", small, sizeof(small)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_same_version_is_passthrough);
    RUN_TEST(test_unknown_newer_version_is_refused);
    RUN_TEST(test_migrate_is_idempotent);
    RUN_TEST(test_url_strips_one_trailing_slash);
    RUN_TEST(test_url_strips_many_trailing_slashes);
    RUN_TEST(test_url_without_scheme_is_refused);
    RUN_TEST(test_scheme_alone_has_no_host);
    RUN_TEST(test_url_with_a_host_is_kept);
    RUN_TEST(test_url_path_is_preserved);
    RUN_TEST(test_url_empty_and_null_are_refused);
    RUN_TEST(test_url_too_long_for_buffer_is_refused);
    return UNITY_END();
}
