#include <string.h>
#include "unity.h"
#include "api_values.h"

void setUp(void) {}
void tearDown(void) {}

static char out[2048];

/* The empty case is what the editor sees BEFORE the first refresh, and it has to be a valid
 * document rather than an error: the app's question is "what will the panel show", and "the
 * device has not drawn yet" is a real answer. A malformed body here would read as a device
 * fault. */
static void test_empty_list_is_a_valid_document(void)
{
    api_values_t v = { .items = NULL, .count = 0, .page = 0, .drawn_page = -1, .page_count = 2, .resolved_at = 0 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_STRING(
        "{\"page\":0,\"drawn_page\":-1,\"page_count\":2,\"resolved_at\":0,\"values\":[]}", out);
}

/* `page` is the page the CALLER asked about and `drawn_page` is the page on the glass: the
 * editor edits one page while the device rotates on its own, so the two routinely differ and
 * the app needs both to fill in the edited page without lying about what the panel shows. */
static void test_reports_values_with_ids(void)
{
    const api_value_t items[] = {
        { .id = "owm_temp",      .text = "58.0°F",      .has_value = 1 },
        { .id = "ha_hallway",    .text = "--",          .has_value = 0 },
    };
    api_values_t v = { .items = items, .count = 2, .page = 1, .drawn_page = 0,
                       .page_count = 2, .resolved_at = 42 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"owm_temp\""));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"has_value\":1"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"has_value\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"page\":1"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"drawn_page\":0"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"resolved_at\":42"));
}

/* A quote or backslash in a value must not end the string early. A place name is user-visible
 * text and OWM returns whatever the locality is called; a broken body means the editor shows
 * NOTHING rather than one wrong box, which is the worse failure. */
static void test_quotes_and_backslashes_are_escaped(void)
{
    const api_value_t items[] = {
        { .id = "owm_city", .text = "Ba\"ck\\slash", .has_value = 1 },
    };
    api_values_t v = { .items = items, .count = 1, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(out, "Ba\\\"ck\\\\slash"));
}

/* A NULL id or text is skipped as an empty string rather than dereferenced: the caller builds
 * these from fixed arrays and a widget with no id is legal in the document. */
static void test_null_fields_are_safe(void)
{
    const api_value_t items[] = { { .id = NULL, .text = NULL, .has_value = 0 } };
    api_values_t v = { .items = items, .count = 1, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"\""));
}

/* Too small a buffer must be REFUSED, never truncated: a truncated JSON body is unparseable
 * and would look like a device fault when the real problem is the caller's buffer size. */
static void test_a_short_buffer_is_refused_not_truncated(void)
{
    const api_value_t items[] = { { .id = "owm_temp", .text = "58.0", .has_value = 1 } };
    api_values_t v = { .items = items, .count = 1, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    char small[16];
    TEST_ASSERT_EQUAL_INT(-1, api_values_json(&v, small, sizeof(small)));
}

/* A count beyond the cap is a caller bug, not something to serialise partially — the arrays it
 * points at cannot have that many entries. */
static void test_an_oversized_count_is_rejected(void)
{
    const api_value_t items[1] = { { .id = "x", .text = "y", .has_value = 1 } };
    api_values_t v = { .items = items, .count = API_VALUES_MAX + 1, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    TEST_ASSERT_EQUAL_INT(-1, api_values_json(&v, out, sizeof(out)));
}

static void test_null_arguments_are_rejected(void)
{
    const api_value_t items[1] = { { .id = "x", .text = "y", .has_value = 1 } };
    api_values_t v = { .items = items, .count = 1, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    TEST_ASSERT_EQUAL_INT(-1, api_values_json(NULL, out, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, api_values_json(&v, NULL, sizeof(out)));
    TEST_ASSERT_EQUAL_INT(-1, api_values_json(&v, out, 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_empty_list_is_a_valid_document);
    RUN_TEST(test_reports_values_with_ids);
    RUN_TEST(test_quotes_and_backslashes_are_escaped);
    RUN_TEST(test_null_fields_are_safe);
    RUN_TEST(test_a_short_buffer_is_refused_not_truncated);
    RUN_TEST(test_an_oversized_count_is_rejected);
    RUN_TEST(test_null_arguments_are_rejected);
    return UNITY_END();
}
