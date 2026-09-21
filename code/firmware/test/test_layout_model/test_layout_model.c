#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "layout_model.h"
#include "power.h"   /* the power_mode_t values the parser writes */
#include "owm.h"     /* the owm_product_t values the parser writes (FR-6) */

void setUp(void) {}
void tearDown(void) {}

static const char *MINIMAL = "{\"schemaVersion\":1}";
static const char *TWO_PAGES =
  "{\"schemaVersion\":1,\"updateSeconds\":600,\"partialRefreshLimit\":3,"
  "\"pages\":[{\"name\":\"Now\",\"refreshSeconds\":120},"
  "{\"name\":\"Forecast\",\"refreshSeconds\":600}]}";

/* A minimal document must be valid — absent fields take documented defaults. */
static void test_minimal_document_gets_defaults(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(MINIMAL, &c));
    TEST_ASSERT_EQUAL_INT(900, c.update_seconds);
    TEST_ASSERT_EQUAL_INT(5, c.partial_refresh_limit);
    TEST_ASSERT_EQUAL_INT(1, c.page_count);
}

static void test_pages_and_intervals_parse(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(TWO_PAGES, &c));
    TEST_ASSERT_EQUAL_INT(600, c.update_seconds);
    TEST_ASSERT_EQUAL_INT(3, c.partial_refresh_limit);
    TEST_ASSERT_EQUAL_INT(2, c.page_count);
    TEST_ASSERT_EQUAL_STRING("Now", c.pages[0].name);
    TEST_ASSERT_EQUAL_STRING("Forecast", c.pages[1].name);
}

/* FR-16: a single-page config must behave as if paging did not exist. */
static void test_single_page_always_returns_page_zero(void)
{
    layout_config_t c;
    layout_config_parse(MINIMAL, &c);
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 0));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 1000000));
}

/* Rotation boundaries: 120 s then 600 s, total 720 s. */
static void test_rotation_boundaries_and_wrap(void)
{
    layout_config_t c;
    layout_config_parse(TWO_PAGES, &c);
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 0));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 119));
    TEST_ASSERT_EQUAL_INT(1, layout_page_at(&c, 120));   /* switches exactly on the edge */
    TEST_ASSERT_EQUAL_INT(1, layout_page_at(&c, 719));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 720));   /* wraps cleanly */
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 1440));
}

static void test_malformed_json_is_an_error(void)
{
    layout_config_t c;
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("{nope", &c));
}

/* FR-26b: a document from a NEWER schema must be refused, not silently mis-read as if it
 * were the current one. The config UI is versioned independently of this firmware, so a
 * page field could mean something different in v2. */
static void test_newer_schema_is_refused(void)
{
    layout_config_t c;
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse(
        "{\"schemaVersion\":2,\"pages\":[{\"name\":\"Now\",\"refreshSeconds\":120}]}", &c));
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse(
        "{\"schemaVersion\":99}", &c));
    /* The current version, and an absent one, are both fine. */
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse("{\"schemaVersion\":1}", &c));
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse("{}", &c));
}

/* An absurd interval must be clamped, not truncated. 2147483647 seconds cast to a 32-bit
 * long and summed across pages overflows to a NEGATIVE total, and `pos = elapsed % total`
 * on a negative divisor is undefined — the device would pick an arbitrary page. */
static void test_absurd_intervals_are_clamped(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"updateSeconds\":2147483647,"
        "\"pages\":[{\"name\":\"A\",\"refreshSeconds\":2000000000},"
        "{\"name\":\"B\",\"refreshSeconds\":2000000000}]}", &c));
    TEST_ASSERT_TRUE(c.update_seconds >= 30);
    TEST_ASSERT_TRUE(c.update_seconds <= LAYOUT_MAX_INTERVAL_SECONDS);
    TEST_ASSERT_TRUE(c.pages[0].refresh_seconds >= 30);
    TEST_ASSERT_TRUE(c.pages[0].refresh_seconds <= LAYOUT_MAX_INTERVAL_SECONDS);
    /* Every page must still be reachable, and the rotation must be well defined. */
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 0));
    TEST_ASSERT_EQUAL_INT(1, layout_page_at(&c, c.pages[0].refresh_seconds));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 2L * c.pages[0].refresh_seconds));
}

/* Below the documented floor the value is replaced by the default, matching the plan. */
static void test_intervals_below_floor_fall_back(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"updateSeconds\":5,"
        "\"pages\":[{\"name\":\"A\",\"refreshSeconds\":1}]}", &c));
    TEST_ASSERT_EQUAL_INT(900, c.update_seconds);
    TEST_ASSERT_EQUAL_INT(900, c.pages[0].refresh_seconds);
}

static void test_partial_limit_is_bounded(void)
{
    layout_config_t c;
    /* A limit of 0 would mean "full refresh every time" — but the plan's contract says the
     * field is >= 1, so 0 falls back to the default rather than being stored as 0. */
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"partialRefreshLimit\":0}", &c));
    TEST_ASSERT_EQUAL_INT(5, c.partial_refresh_limit);
    /* An absurd limit is clamped so the partial counter cannot overflow. */
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"partialRefreshLimit\":2000000000}", &c));
    TEST_ASSERT_TRUE(c.partial_refresh_limit <= LAYOUT_MAX_PARTIAL_LIMIT);
}

/* More pages than the device can hold must keep the first LAYOUT_MAX_PAGES, not fail. */
static void test_too_many_pages_are_truncated(void)
{
    char json[1024];
    int n = snprintf(json, sizeof(json), "{\"schemaVersion\":1,\"pages\":[");
    for (int i = 0; i < LAYOUT_MAX_PAGES + 4; i++) {
        n += snprintf(json + n, sizeof(json) - n, "%s{\"name\":\"P%d\",\"refreshSeconds\":60}",
                      i ? "," : "", i);
    }
    snprintf(json + n, sizeof(json) - n, "]}");

    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(json, &c));
    TEST_ASSERT_EQUAL_INT(LAYOUT_MAX_PAGES, c.page_count);
    TEST_ASSERT_EQUAL_STRING("P0", c.pages[0].name);
    TEST_ASSERT_EQUAL_STRING("P7", c.pages[LAYOUT_MAX_PAGES - 1].name);
}

/* A non-object entry must be skipped without shifting the page_count off the end of the
 * array — the pages that DID parse must remain usable. */
static void test_junk_entries_do_not_corrupt_page_count(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"pages\":[\"junk\",{\"name\":\"Real\",\"refreshSeconds\":60}]}",
        &c));
    TEST_ASSERT_EQUAL_INT(1, c.page_count);
    TEST_ASSERT_EQUAL_STRING("Real", c.pages[0].name);
}

/* A name longer than the field must be truncated safely, not overflow the struct. */
static void test_long_page_name_is_truncated(void)
{
    char json[256];
    snprintf(json, sizeof(json),
             "{\"schemaVersion\":1,\"pages\":[{\"name\":\"%s\",\"refreshSeconds\":60}]}",
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(json, &c));
    TEST_ASSERT_EQUAL_INT((int)sizeof(c.pages[0].name) - 1, (int)strlen(c.pages[0].name));
}

/* A `pages` that is present but the WRONG TYPE is a serialization bug, not a user choice.
 * Falling through to the default would silently discard the user's layout; it must be an
 * error. An ABSENT `pages` stays lenient (a minimal document is valid). */
static void test_wrong_typed_pages_is_rejected(void)
{
    layout_config_t c;
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("{\"schemaVersion\":1,\"pages\":\"nope\"}", &c));
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("{\"schemaVersion\":1,\"pages\":42}", &c));
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("{\"schemaVersion\":1,\"pages\":{}}", &c));
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("{\"schemaVersion\":1,\"pages\":null}", &c));
    /* Absent is still fine. */
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse("{\"schemaVersion\":1}", &c));
    TEST_ASSERT_EQUAL_INT(1, c.page_count);
}

/* An empty pages array must not leave page_count pointing past the default page. */
static void test_empty_pages_array_keeps_a_usable_default(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse("{\"schemaVersion\":1,\"pages\":[]}", &c));
    TEST_ASSERT_EQUAL_INT(1, c.page_count);
    TEST_ASSERT_TRUE(c.pages[0].refresh_seconds >= 30);
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 999999));
}

static void test_null_and_empty_input_are_rejected(void)
{
    layout_config_t c;
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse(NULL, &c));
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse("", &c));
    TEST_ASSERT_NOT_EQUAL(0, layout_config_parse(MINIMAL, NULL));
}

/* layout_page_at must be defensive on a config that was never parsed. */
static void test_page_at_on_zeroed_config(void)
{
    layout_config_t c;
    memset(&c, 0, sizeof(c));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 500));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(NULL, 500));
}

/* The parser can never emit a zero interval, but a config read back from NVS can be
 * corrupt. A zero total makes `elapsed % total` a division by zero, which on the ESP32 is
 * a hardware exception — a panic-and-reboot loop rather than a wrong page. */
static void test_page_at_with_zero_total_does_not_divide_by_zero(void)
{
    layout_config_t c;
    memset(&c, 0, sizeof(c));
    c.page_count = 3;
    c.pages[0].refresh_seconds = 0;
    c.pages[1].refresh_seconds = 0;
    c.pages[2].refresh_seconds = 0;
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 500));
    TEST_ASSERT_EQUAL_INT(0, layout_page_at(&c, 0));
}


/* The FR-8 override must actually reach the struct — a config field the app can set but the
 * firmware ignores is worse than not offering it, because the UI confirms a change that has
 * no effect. */
static void test_power_mode_override_is_parsed(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"powerMode\":\"battery\"}", &c));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_BATTERY, c.power_mode);

    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"powerMode\":\"always-on\"}", &c));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_ALWAYS_ON, c.power_mode);
}

/* Absent, or unrecognised, must mean 'auto' — the inference, never an invented behaviour. */
static void test_power_mode_defaults_to_auto(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse("{\"schemaVersion\":1}", &c));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, c.power_mode);

    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"powerMode\":\"mains\"}", &c));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, c.power_mode);

    /* Wrong type: not a string, so it cannot be a mode. */
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"powerMode\":7}", &c));
    TEST_ASSERT_EQUAL_INT(POWER_MODE_AUTO, c.power_mode);
}

/* FR-6's product toggle is PARSED, for the same reason powerMode is: the web app writes it, and a
 * field the firmware silently ignores is a lie in the UI — the user picks "One Call 3.0", the app
 * confirms it, and the device keeps calling the free endpoints. */
static void test_owm_product_override_is_parsed(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"owmProduct\":\"onecall3\"}", &c));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_ONECALL3, c.owm_product);

    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"owmProduct\":\"legacy\"}", &c));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_LEGACY, c.owm_product);
}

/* Absent, unrecognised, or the wrong type must mean 'auto' — which probes. It must never pin the
 * device to a product the user did not choose, because on a key that HAS One Call that silently
 * loses the official alerts. */
static void test_owm_product_defaults_to_auto(void)
{
    layout_config_t c;
    TEST_ASSERT_EQUAL_INT(0, layout_config_parse("{\"schemaVersion\":1}", &c));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, c.owm_product);

    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"owmProduct\":\"v3\"}", &c));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, c.owm_product);

    TEST_ASSERT_EQUAL_INT(0, layout_config_parse(
        "{\"schemaVersion\":1,\"owmProduct\":7}", &c));
    TEST_ASSERT_EQUAL_INT(OWM_PRODUCT_AUTO, c.owm_product);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_minimal_document_gets_defaults);
    RUN_TEST(test_pages_and_intervals_parse);
    RUN_TEST(test_single_page_always_returns_page_zero);
    RUN_TEST(test_rotation_boundaries_and_wrap);
    RUN_TEST(test_malformed_json_is_an_error);
    RUN_TEST(test_newer_schema_is_refused);
    RUN_TEST(test_absurd_intervals_are_clamped);
    RUN_TEST(test_intervals_below_floor_fall_back);
    RUN_TEST(test_partial_limit_is_bounded);
    RUN_TEST(test_too_many_pages_are_truncated);
    RUN_TEST(test_junk_entries_do_not_corrupt_page_count);
    RUN_TEST(test_long_page_name_is_truncated);
    RUN_TEST(test_wrong_typed_pages_is_rejected);
    RUN_TEST(test_empty_pages_array_keeps_a_usable_default);
    RUN_TEST(test_null_and_empty_input_are_rejected);
    RUN_TEST(test_page_at_on_zeroed_config);
    RUN_TEST(test_page_at_with_zero_total_does_not_divide_by_zero);
    RUN_TEST(test_power_mode_override_is_parsed);
    RUN_TEST(test_power_mode_defaults_to_auto);
    RUN_TEST(test_owm_product_override_is_parsed);
    RUN_TEST(test_owm_product_defaults_to_auto);
    return UNITY_END();
}
