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

/* A NUMERIC READING IS REPORTED WITH ITS RAW VALUE, so the editor can re-format it after the user
 * edits the decimals or affixes instead of showing the string the device formatted with the OLD
 * format until a save and a repaint (reported as "the editor doesn't show the updated values until
 * you first save and refresh").
 *
 * The DOUBLE IS ROUND-TRIPPED EXACTLY: %.17g is the shortest precision that reproduces an IEEE-754
 * double bit-for-bit, and anything less would have the editor's digits differ from the device's by
 * an ULP — a preview reading one digit off the glass. 72.5 and 0.1 are used because both are
 * inexact in binary, so a lower precision would visibly lose them. */
static void test_a_rendered_number_carries_its_exact_value(void)
{
    const api_value_t items[] = {
        { .id = "owm_temp", .text = "72.5°F", .has_value = 1, .value = 72.5,      .rendered_number = 1 },
        { .id = "ha_tenth", .text = "0.1",    .has_value = 1, .value = 0.1,       .rendered_number = 1 },
        { .id = "owm_city", .text = "Provo",  .has_value = 1, .value = 0.0,       .rendered_number = 0 },
        { .id = "owm_cond", .text = "Clouds", .has_value = 1, .value = 0.0,       .rendered_number = 0 },
    };
    api_values_t v = { .items = items, .count = 4, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"text\":\"72.5°F\",\"has_value\":1,\"rendered_number\":1,\"value\":72.5}"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"rendered_number\":1,\"value\":0.10000000000000001}"));
}

/* A NON-NUMERIC READING REPORTS rendered_number 0 AND a zero value, NEVER the reading.
 *
 * This is the case that would silently produce a wrong preview: an alert word, a condition, a
 * binary sensor's state and an OWM icon code all reach the panel as text, and there is no number
 * behind them that re-formatting could produce. If the endpoint advertised a number here the editor
 * would draw "72.5°F" over a box the panel paints "severe" in, which is precisely the
 * preview-contradicts-the-glass failure NFR-4 forbids. A caller that has no reading at all (a
 * string-only caller) is the same case, which is why the flag — not the value — is what the editor
 * gates on. */
static void test_a_non_numeric_reading_reports_no_number(void)
{
    const api_value_t items[] = {
        { .id = "owm_cond", .text = "Clouds", .has_value = 1, .value = 0.0, .rendered_number = 0 },
        { .id = "alert_bar",.text = "severe", .has_value = 1, .value = 0.0, .rendered_number = 0 },
    };
    api_values_t v = { .items = items, .count = 2, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"text\":\"Clouds\",\"has_value\":1,\"rendered_number\":0,\"value\":0}"));
}

/* A ZERO READING IS STILL A READING. `rendered_number` is 1 with value 0 for a real 0.0 °F, and 0
 * with value 0 for text — which is why the two are separate fields rather than one nullable number:
 * JSON has no way to say "0 but not a number", and 0°F is plausible weather that must be drawn. */
static void test_a_zero_reading_is_distinct_from_no_reading(void)
{
    const api_value_t items[] = {
        { .id = "real_zero", .text = "0.0°F", .has_value = 1, .value = 0.0, .rendered_number = 1 },
        { .id = "text",      .text = "off",   .has_value = 1, .value = 0.0, .rendered_number = 0 },
    };
    api_values_t v = { .items = items, .count = 2, .page = 0, .drawn_page = 0, .page_count = 1, .resolved_at = 0 };
    const int n = api_values_json(&v, out, sizeof(out));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"real_zero\",\"text\":\"0.0°F\",\"has_value\":1,\"rendered_number\":1,\"value\":0}"));
    TEST_ASSERT_NOT_NULL(strstr(out, "\"id\":\"text\",\"text\":\"off\",\"has_value\":1,\"rendered_number\":0,\"value\":0}"));
}

/* API_VALUES_JSON_MAX MUST COVER A FULL PAGE OF THE LONGEST ITEMS THE STORE CAN HOLD.
 *
 * The endpoint used a hand-picked 2048-byte buffer, and a 24-widget page had ALREADY outgrown it
 * (~2.5 KB) before the raw reading widened each item further: a page at the cap would have been
 * answered 500 rather than with its values, unnoticed because real pages bind far fewer widgets
 * than the cap allows. The bound is derived from the format now, and THIS is the test that keeps
 * the derivation honest — filling every field to its real maximum (a 23-character id, a
 * 47-character value) and requiring the document to fit. */
static void test_the_json_bound_covers_a_full_page_of_long_items(void)
{
    static api_value_t items[API_VALUES_MAX];
    char ids[API_VALUES_MAX][API_VALUES_ID_LEN];
    char texts[API_VALUES_MAX][API_VALUES_TEXT_LEN];
    for (int i = 0; i < API_VALUES_MAX; i++) {
        /* One character short of each buffer's capacity, which is the longest the store can hold
         * after its own truncation. */
        memset(ids[i], 'a' + (i % 26), API_VALUES_ID_LEN - 1);
        ids[i][API_VALUES_ID_LEN - 1] = '\0';
        memset(texts[i], '0' + (i % 10), API_VALUES_TEXT_LEN - 1);
        texts[i][API_VALUES_TEXT_LEN - 1] = '\0';
        items[i].id = ids[i];
        items[i].text = texts[i];
        items[i].has_value = 1;
        items[i].value = 12345.6789;
        items[i].rendered_number = 1;
    }
    api_values_t v = { .items = items, .count = API_VALUES_MAX, .page = 7, .drawn_page = 3,
                       .page_count = 8, .resolved_at = 1758300000L };

    static char big[API_VALUES_JSON_MAX];
    const int n = api_values_json(&v, big, sizeof(big));
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_LESS_THAN_INT((int)sizeof(big), n);
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
    RUN_TEST(test_a_rendered_number_carries_its_exact_value);
    RUN_TEST(test_a_non_numeric_reading_reports_no_number);
    RUN_TEST(test_a_zero_reading_is_distinct_from_no_reading);
    RUN_TEST(test_the_json_bound_covers_a_full_page_of_long_items);
    RUN_TEST(test_null_fields_are_safe);
    RUN_TEST(test_a_short_buffer_is_refused_not_truncated);
    RUN_TEST(test_an_oversized_count_is_rejected);
    RUN_TEST(test_null_arguments_are_rejected);
    return UNITY_END();
}
