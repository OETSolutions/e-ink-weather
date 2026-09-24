#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "otarelease.h"

void setUp(void) {}
void tearDown(void) {}

/* The exact shape release.yml writes. */
static const char *GOOD =
  "{\n"
  "  \"version\": \"0.1.0\",\n"
  "  \"firmware\": \"firmware.bin\",\n"
  "  \"sha256\": \"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\n"
  "  \"size\": 1838224\n"
  "}";

static void test_parses_the_manifest_the_release_writes(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(0, otarelease_parse(GOOD, &m));
    TEST_ASSERT_EQUAL_STRING("0.1.0", m.version);
    TEST_ASSERT_EQUAL_STRING("firmware.bin", m.firmware);
    TEST_ASSERT_EQUAL_INT(64, (int)strlen(m.sha256));
    TEST_ASSERT_EQUAL_INT(1838224, (int)m.size);
}

static void test_rejects_empty_and_garbage(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse(NULL, &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{not json", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("[1,2,3]", &m));   /* array, not object */
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("null", &m));
}

/* A manifest with no version cannot be compared, so it is refused rather than treated as "0". */
static void test_requires_version(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"firmware\":\"firmware.bin\"}", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"version\":\"\",\"firmware\":\"firmware.bin\"}", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"version\":5,\"firmware\":\"firmware.bin\"}", &m));
}

/* THE traversal guard. `firmware` is concatenated onto a trusted base URL, so a path-ish or
 * absolute value would point the download somewhere the base never agreed to. */
static void test_firmware_must_be_a_bare_filename(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"version\":\"1.0.0\",\"firmware\":\"../x.bin\"}", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"version\":\"1.0.0\",\"firmware\":\"a/b.bin\"}", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"version\":\"1.0.0\",\"firmware\":\"https://evil/x\"}", &m));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_parse("{\"version\":\"1.0.0\",\"firmware\":\"\"}", &m));
    /* A normal name still parses. */
    TEST_ASSERT_EQUAL_INT(0, otarelease_parse("{\"version\":\"1.0.0\",\"firmware\":\"fw-1_0.bin\"}", &m));
}

/* Optional fields: a release without them still updates, it just cannot be hash-verified. */
static void test_optional_fields_may_be_absent(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(0, otarelease_parse("{\"version\":\"1.2.3\",\"firmware\":\"firmware.bin\"}", &m));
    TEST_ASSERT_EQUAL_STRING("", m.sha256);
    TEST_ASSERT_EQUAL_INT(-1, (int)m.size);
}

/* A short/odd sha256 is not a hash we can check, so it is dropped rather than half-stored. */
static void test_bad_sha256_is_dropped_not_stored(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(0, otarelease_parse(
        "{\"version\":\"1.0.0\",\"firmware\":\"firmware.bin\",\"sha256\":\"deadbeef\"}", &m));
    TEST_ASSERT_EQUAL_STRING("", m.sha256);
}

/* ------------------------------------------------------------------ version compare -- */

/* THE bug this exists for: as strings, "0.10.0" < "0.9.0" ("1" < "9"), so a device on 0.9.0
 * would never see 0.10.0 as newer. Numeric comparison gets it right. */
static void test_double_digit_minor_is_newer(void)
{
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("0.9.0", "0.10.0"));
    TEST_ASSERT_EQUAL_INT(1, otarelease_version_cmp("0.10.0", "0.9.0"));
    TEST_ASSERT_TRUE(otarelease_is_newer("0.9.0", "0.10.0"));
    TEST_ASSERT_FALSE(otarelease_is_newer("0.10.0", "0.9.0"));
}

static void test_equal_versions(void)
{
    TEST_ASSERT_EQUAL_INT(0, otarelease_version_cmp("1.2.3", "1.2.3"));
    TEST_ASSERT_FALSE(otarelease_is_newer("1.2.3", "1.2.3"));   /* never "update" to the same */
}

static void test_ordering_each_component(void)
{
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("1.0.0", "1.0.1"));
    TEST_ASSERT_EQUAL_INT(1, otarelease_version_cmp("1.0.1", "1.0.0"));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("1.2.9", "1.3.0"));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("1.9.9", "2.0.0"));
    TEST_ASSERT_EQUAL_INT(1, otarelease_version_cmp("2.0.0", "1.99.99"));
}

/* A leading "v" (how the git tag is written) is accepted and ignored. */
static void test_leading_v_is_ignored(void)
{
    TEST_ASSERT_EQUAL_INT(0, otarelease_version_cmp("v1.2.3", "1.2.3"));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("v0.1.0", "v0.2.0"));
}

/* Missing components are zero, so a two-part version equals its three-part spelling. */
static void test_short_versions_pad_with_zero(void)
{
    TEST_ASSERT_EQUAL_INT(0, otarelease_version_cmp("1.2", "1.2.0"));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("1.2", "1.2.1"));
    TEST_ASSERT_EQUAL_INT(0, otarelease_version_cmp("1", "1.0.0"));
}

/* A malformed version must NOT be able to claim it is newer by how the parse defaults. The
 * fallback is a plain strcmp of the originals. */
static void test_malformed_versions_fall_back_to_strcmp(void)
{
    /* "banana" is not numeric, so this is a string compare: "banana" > "0.1.0" because 'b' > '0'.
     * If a malformed version instead defaulted to 0.0.0 it would compare as OLDER (the other
     * sign), so this pins the strcmp fallback and not a numeric guess. */
    TEST_ASSERT_EQUAL_INT(1, otarelease_version_cmp("banana", "0.1.0"));
    /* A trailing dot is malformed -> string compare: "0.1.0" vs "1.2." starts '0' < '1'. */
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("0.1.0", "1.2."));
    /* A "-beta" suffix is malformed -> string compare: "1.0.0" < "1.0.0-x" (the prefix is
     * shorter and '\0' precedes '-'). */
    TEST_ASSERT_EQUAL_INT(-1, otarelease_version_cmp("1.0.0", "1.0.0-x"));
}

/* ------------------------------------------------------------------ url join -- */

static void test_firmware_url_joins_with_one_slash(void)
{
    otarelease_manifest_t m;
    TEST_ASSERT_EQUAL_INT(0, otarelease_parse(GOOD, &m));
    char url[256];
    const char *base = "https://github.com/oetsolutions/e-ink-weather/releases/latest/download";
    TEST_ASSERT_EQUAL_INT(0, otarelease_firmware_url(base, &m, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING(
        "https://github.com/oetsolutions/e-ink-weather/releases/latest/download/firmware.bin", url);
}

/* A base already ending in a slash must not produce a double slash. */
static void test_firmware_url_avoids_double_slash(void)
{
    otarelease_manifest_t m;
    otarelease_parse(GOOD, &m);
    char url[256];
    TEST_ASSERT_EQUAL_INT(0, otarelease_firmware_url("https://h/x/", &m, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("https://h/x/firmware.bin", url);
}

/* Truncation is refused and the buffer left empty, never a clipped URL. */
static void test_firmware_url_refuses_truncation(void)
{
    otarelease_manifest_t m;
    otarelease_parse(GOOD, &m);
    char url[16];
    TEST_ASSERT_EQUAL_INT(-1, otarelease_firmware_url("https://h/x", &m, url, sizeof(url)));
    TEST_ASSERT_EQUAL_STRING("", url);
}

static void test_firmware_url_rejects_empty_base(void)
{
    otarelease_manifest_t m;
    otarelease_parse(GOOD, &m);
    char url[64];
    TEST_ASSERT_EQUAL_INT(-1, otarelease_firmware_url("", &m, url, sizeof(url)));
    TEST_ASSERT_EQUAL_INT(-1, otarelease_firmware_url(NULL, &m, url, sizeof(url)));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_the_manifest_the_release_writes);
    RUN_TEST(test_rejects_empty_and_garbage);
    RUN_TEST(test_requires_version);
    RUN_TEST(test_firmware_must_be_a_bare_filename);
    RUN_TEST(test_optional_fields_may_be_absent);
    RUN_TEST(test_bad_sha256_is_dropped_not_stored);
    RUN_TEST(test_double_digit_minor_is_newer);
    RUN_TEST(test_equal_versions);
    RUN_TEST(test_ordering_each_component);
    RUN_TEST(test_leading_v_is_ignored);
    RUN_TEST(test_short_versions_pad_with_zero);
    RUN_TEST(test_malformed_versions_fall_back_to_strcmp);
    RUN_TEST(test_firmware_url_joins_with_one_slash);
    RUN_TEST(test_firmware_url_avoids_double_slash);
    RUN_TEST(test_firmware_url_refuses_truncation);
    RUN_TEST(test_firmware_url_rejects_empty_base);
    return UNITY_END();
}
