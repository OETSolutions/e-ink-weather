#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "widgets.h"
#include "fonts.h"
#include "alerts.h"
/* cJSON is used directly here because layout_widgets_from_root() and layout_scan_all_pages()
 * take an already-parsed tree (void *), so a test of them must own one. */
#include "cJSON.h"

/* The parser that turns a pushed layout into the boxes the device stamps. Before this existed
 * the firmware used a hard-coded two-box array, so a layout from the web app was parsed for
 * scheduling and then discarded: every widget but the temperature was blank on the glass, and
 * it appeared in a box the user never authored. These tests are what stop that regressing. */

void setUp(void) {}
void tearDown(void) {}

/* A document shaped exactly like the one the web app pushes — the same key spellings
 * (owmField, dayIndex, entityId, align, valign) that webapp/src/model/config.ts emits. */
static const char *DOC =
"{"
  "\"schemaVersion\":1,"
  "\"pages\":[{\"name\":\"Weather\",\"refreshSeconds\":900,\"weight\":1,\"widgets\":["
    "{\"id\":\"owm_temp\",\"x\":40,\"y\":64,\"w\":360,\"h\":120,\"role\":\"dynamic\","
     "\"binding\":{\"kind\":\"owm-current\",\"owmField\":\"temp\"},"
     "\"format\":{\"decimals\":1,\"suffix\":\"°F\",\"fallback\":\"--\"},"
     "\"font\":{\"size\":64,\"align\":\"left\",\"valign\":\"top\"},"
     "\"alerts\":[{\"op\":\"gte\",\"threshold\":100,\"level\":\"severe\"},"
                 "{\"op\":\"lte\",\"threshold\":32,\"level\":\"advisory\"}]},"
    "{\"id\":\"fc_max\",\"x\":680,\"y\":64,\"w\":200,\"h\":90,\"role\":\"dynamic\","
     "\"binding\":{\"kind\":\"owm-daily\",\"dayIndex\":1,\"owmField\":\"max\"},"
     "\"font\":{\"size\":20,\"align\":\"right\",\"valign\":\"middle\"}},"
    "{\"id\":\"ha_hall\",\"x\":440,\"y\":64,\"w\":200,\"h\":90,\"role\":\"dynamic\","
     "\"binding\":{\"kind\":\"ha\",\"entityId\":\"sensor.upstairs_hallway_temperature\"},"
     "\"font\":{\"size\":20,\"align\":\"left\",\"valign\":\"top\"}},"
    "{\"id\":\"bar\",\"x\":40,\"y\":452,\"w\":840,\"h\":80,\"role\":\"dynamic\","
     "\"binding\":{\"kind\":\"owm-alert\"},"
     "\"format\":{\"fallback\":\"\"},"
     "\"font\":{\"size\":20,\"align\":\"left\",\"valign\":\"middle\"}},"
    "{\"id\":\"divider\",\"x\":0,\"y\":140,\"w\":920,\"h\":2,\"role\":\"static\"}"
  "]}]"
"}";

static void test_parses_every_widget_in_order(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const int n = layout_widgets_parse(DOC, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_INT(5, n);
}

static void test_geometry_binding_and_font_round_trip(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const int n = layout_widgets_parse(DOC, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_INT(5, n);

    const layout_widget_t *t = &w[0];
    TEST_ASSERT_EQUAL_INT(40, t->x);
    TEST_ASSERT_EQUAL_INT(64, t->y);
    TEST_ASSERT_EQUAL_INT(360, t->w);
    TEST_ASSERT_EQUAL_INT(120, t->h);
    TEST_ASSERT_EQUAL_INT(BIND_OWM_CURRENT, t->binding.kind);
    TEST_ASSERT_EQUAL_INT(OWM_F_TEMP, t->binding.owm_field);
    /* size 64 >= the 48 px threshold selects the VALUE face. */
    TEST_ASSERT_EQUAL_INT(FONT_VALUE, t->font_id);
    TEST_ASSERT_EQUAL_CHAR('L', t->align_h);
    TEST_ASSERT_EQUAL_CHAR('T', t->align_v);
    TEST_ASSERT_EQUAL_STRING("\xc2\xb0""F", t->format.suffix);
    TEST_ASSERT_EQUAL_INT(1, t->format.decimals);
    TEST_ASSERT_EQUAL_STRING("--", t->format.fallback);
    TEST_ASSERT_EQUAL_INT(2, t->n_rules);
    TEST_ASSERT_EQUAL_INT(ALERT_OP_GTE, t->rules[0].op);
    TEST_ASSERT_EQUAL_INT(100, (int)t->rules[0].threshold);
    TEST_ASSERT_EQUAL_INT(ALERT_SEVERE, t->rules[0].level);
    TEST_ASSERT_EQUAL_INT(ALERT_ADVISORY, t->rules[1].level);
}

/* size 20 is below the threshold, so a widget authored small must NOT get the big face — the
 * editor and the golden fixture disagreed about this once, and a widget previewed in one face
 * and was stamped in another. */
static void test_small_size_selects_the_body_face(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    layout_widgets_parse(DOC, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_INT(FONT_BODY, w[1].font_id);
    TEST_ASSERT_EQUAL_CHAR('R', w[1].align_h);
    TEST_ASSERT_EQUAL_CHAR('M', w[1].align_v);
    TEST_ASSERT_EQUAL_INT(BIND_OWM_DAILY, w[1].binding.kind);
    TEST_ASSERT_EQUAL_INT(1, w[1].binding.day_index);
    TEST_ASSERT_EQUAL_INT(OWM_F_MAX, w[1].binding.owm_field);
}

static void test_ha_entity_id_is_read(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    layout_widgets_parse(DOC, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_INT(BIND_HA, w[2].binding.kind);
    TEST_ASSERT_EQUAL_STRING("sensor.upstairs_hallway_temperature", w[2].binding.entity_id);
}

/* The alert bar's fallback is the EMPTY STRING, and that is deliberate: two dashes across the
 * bottom of the panel would read as a reading. An explicit "" must survive parsing. */
static void test_empty_fallback_survives(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    layout_widgets_parse(DOC, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_STRING("", w[3].format.fallback);
    TEST_ASSERT_EQUAL_INT(BIND_OWM_ALERT, w[3].binding.kind);
}

static void test_static_role_is_marked(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    layout_widgets_parse(DOC, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_CHAR('s', w[4].role);
    TEST_ASSERT_EQUAL_CHAR('d', w[0].role);
}

/* A document with no such page yields zero widgets, NOT an error: a one-page config is asked
 * for page 0, and probing past the end must not blank the panel. */
static void test_missing_page_yields_no_widgets(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    TEST_ASSERT_EQUAL_INT(0, layout_widgets_parse(DOC, 7, w, LAYOUT_MAX_FIELDS));
}

static void test_no_widgets_key_yields_none(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const char *doc = "{\"schemaVersion\":1,\"pages\":[{\"name\":\"p\"}]}";
    TEST_ASSERT_EQUAL_INT(0, layout_widgets_parse(doc, 0, w, LAYOUT_MAX_FIELDS));
}

/* A widget that runs off the panel is CLAMPED, not drawn off-canvas. The renderer clips too,
 * but a field whose intersection with the panel is empty would look correct in the config
 * while showing nothing. */
static void test_out_of_range_geometry_is_clamped(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const char *doc =
      "{\"schemaVersion\":1,\"pages\":[{\"widgets\":["
      "{\"x\":900,\"y\":600,\"w\":400,\"h\":400,\"binding\":{\"kind\":\"owm-current\"}},"
      "{\"x\":-20,\"y\":-20,\"w\":100,\"h\":100,\"binding\":{\"kind\":\"owm-current\"}}"
      "]}]}";
    TEST_ASSERT_EQUAL_INT(2, layout_widgets_parse(doc, 0, w, LAYOUT_MAX_FIELDS));
    TEST_ASSERT_EQUAL_INT(920, w[0].x + w[0].w);
    TEST_ASSERT_EQUAL_INT(680, w[0].y + w[0].h);
    TEST_ASSERT_EQUAL_INT(0, w[1].x);
    TEST_ASSERT_EQUAL_INT(0, w[1].y);
    TEST_ASSERT_EQUAL_INT(100, w[1].w);
}

/* An unknown binding kind must NOT be guessed at: it renders the fallback rather than a
 * plausible-looking number from a binding that may mean something else in a newer schema. */
static void test_unknown_binding_is_none(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const char *doc =
      "{\"schemaVersion\":1,\"pages\":[{\"widgets\":["
      "{\"x\":0,\"y\":0,\"w\":100,\"h\":50,\"binding\":{\"kind\":\"quantum-weather\"}}"
      "]}]}";
    TEST_ASSERT_EQUAL_INT(1, layout_widgets_parse(doc, 0, w, LAYOUT_MAX_FIELDS));
    TEST_ASSERT_EQUAL_INT(BIND_NONE, w[0].binding.kind);
}

/* A rule with no numeric threshold cannot fire, and a rule whose level is unrecognised is
 * dropped rather than stored as a slot that can never fire — the widget only has six. */
static void test_unfireable_rules_are_dropped(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const char *doc =
      "{\"schemaVersion\":1,\"pages\":[{\"widgets\":["
      "{\"x\":0,\"y\":0,\"w\":100,\"h\":50,\"binding\":{\"kind\":\"owm-current\"},"
       "\"alerts\":[{\"op\":\"gt\",\"level\":\"severe\"},"
                   "{\"op\":\"gt\",\"threshold\":5,\"level\":\"catastrophic\"},"
                   "{\"op\":\"gt\",\"threshold\":10,\"level\":\"warning\"}]}"
      "]}]}";
    TEST_ASSERT_EQUAL_INT(1, layout_widgets_parse(doc, 0, w, LAYOUT_MAX_FIELDS));
    TEST_ASSERT_EQUAL_INT(1, w[0].n_rules);
    TEST_ASSERT_EQUAL_INT(ALERT_WARNING, w[0].rules[0].level);
}

/* More widgets than the cap must truncate at the cap, not overflow it. */
static void test_cap_is_respected(void)
{
    char doc[4096];
    int off = snprintf(doc, sizeof(doc),
                       "{\"schemaVersion\":1,\"pages\":[{\"widgets\":[");
    for (int i = 0; i < 40; i++) {
        off += snprintf(doc + off, sizeof(doc) - (size_t)off,
                        "%s{\"x\":0,\"y\":0,\"w\":10,\"h\":10,"
                        "\"binding\":{\"kind\":\"owm-current\"}}",
                        i ? "," : "");
    }
    snprintf(doc + off, sizeof(doc) - (size_t)off, "]}]}");

    layout_widget_t w[8];
    TEST_ASSERT_EQUAL_INT(8, layout_widgets_parse(doc, 0, w, 8));
}

static void test_garbage_and_empty_input(void)
{
    layout_widget_t w[LAYOUT_MAX_FIELDS];
    TEST_ASSERT_TRUE(layout_widgets_parse("not json", 0, w, LAYOUT_MAX_FIELDS) < 0);
    TEST_ASSERT_TRUE(layout_widgets_parse("[]", 0, w, LAYOUT_MAX_FIELDS) < 0);
    TEST_ASSERT_TRUE(layout_widgets_parse(NULL, 0, w, LAYOUT_MAX_FIELDS) < 0);
    TEST_ASSERT_TRUE(layout_widgets_parse(DOC, 0, NULL, 4) < 0);
    TEST_ASSERT_TRUE(layout_widgets_parse(DOC, 0, w, 0) < 0);
    TEST_ASSERT_TRUE(layout_widgets_parse(DOC, -1, w, LAYOUT_MAX_FIELDS) < 0);
}

/* A TWO-PAGE DOCUMENT where the same entity is bound on both pages, plus sources only one page
 * uses. This is the shape the tick resolves every page of, and the shape that caught the
 * "only the drawn page previews" defect. */
static const char *TWO_PAGES =
"{"
  "\"schemaVersion\":1,"
  "\"pages\":["
    "{\"name\":\"A\",\"refreshSeconds\":900,\"widgets\":["
      "{\"id\":\"a_temp\",\"x\":0,\"y\":0,\"w\":100,\"h\":50,\"role\":\"dynamic\","
       "\"binding\":{\"kind\":\"owm-current\",\"owmField\":\"temp\"}},"
      "{\"id\":\"a_hall\",\"x\":0,\"y\":60,\"w\":100,\"h\":50,\"role\":\"dynamic\","
       "\"binding\":{\"kind\":\"ha\",\"entityId\":\"sensor.hallway\"}},"
      "{\"id\":\"a_art\",\"x\":0,\"y\":120,\"w\":920,\"h\":2,\"role\":\"static\","
       "\"binding\":{\"kind\":\"owm-alert\"}}"
    "]},"
    "{\"name\":\"B\",\"refreshSeconds\":900,\"widgets\":["
      "{\"id\":\"b_hall\",\"x\":0,\"y\":0,\"w\":100,\"h\":50,\"role\":\"dynamic\","
       "\"binding\":{\"kind\":\"ha\",\"entityId\":\"sensor.hallway\"}},"
      "{\"id\":\"b_day\",\"x\":0,\"y\":60,\"w\":100,\"h\":50,\"role\":\"dynamic\","
       "\"binding\":{\"kind\":\"owm-daily\",\"dayIndex\":2,\"owmField\":\"max\"}},"
      "{\"id\":\"b_bar\",\"x\":0,\"y\":120,\"w\":840,\"h\":80,\"role\":\"dynamic\","
       "\"binding\":{\"kind\":\"owm-alert\"}}"
    "]}"
  "]"
"}";

/* Reading a page out of an ALREADY-PARSED root must give the identical widgets to parsing the
 * whole document — it is the same code path, and that is the point: the tick parses the document
 * once and reads every page out of it, so the two must not be able to drift. */
static void test_from_root_matches_a_full_parse(void)
{
    layout_widget_t a1[LAYOUT_MAX_FIELDS], a2[LAYOUT_MAX_FIELDS];
    const int n1 = layout_widgets_parse(TWO_PAGES, 1, a1, LAYOUT_MAX_FIELDS);

    cJSON *root = cJSON_Parse(TWO_PAGES);
    TEST_ASSERT_NOT_NULL(root);
    const int n2 = layout_widgets_from_root(root, 1, a2, LAYOUT_MAX_FIELDS);
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_INT(n1, n2);
    TEST_ASSERT_EQUAL_INT(3, n2);
    for (int i = 0; i < n2; i++) {
        TEST_ASSERT_EQUAL_STRING(a1[i].id, a2[i].id);
        TEST_ASSERT_EQUAL_INT(a1[i].binding.kind, a2[i].binding.kind);
        TEST_ASSERT_EQUAL_INT(a1[i].binding.day_index, a2[i].binding.day_index);
    }
    TEST_ASSERT_EQUAL_STRING("b_hall", a2[0].id);
}

/* The whole-document need scan is what lets ONE set of fetches serve EVERY page. A page that is
 * not being drawn still needs its sources fetched, or the editor's preview of it reads "--". */
static void test_scan_covers_every_page(void)
{
    cJSON *root = cJSON_Parse(TWO_PAGES);
    TEST_ASSERT_NOT_NULL(root);
    int cur = 0, day = 0, alert = 0, ha = 0, maxday = 0, n_ha = 0;
    char ids[LAYOUT_MAX_FIELDS][48];
    layout_scan_all_pages(root, 2, &cur, &day, &alert, &ha, &maxday, ids, LAYOUT_MAX_FIELDS, &n_ha);
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_INT(1, cur);      /* page A binds current */
    TEST_ASSERT_EQUAL_INT(1, day);      /* page B binds a daily field */
    TEST_ASSERT_EQUAL_INT(1, alert);    /* page B binds the alert bar */
    TEST_ASSERT_EQUAL_INT(1, ha);
    TEST_ASSERT_EQUAL_INT(2, maxday);   /* page B asks for day 2, not page A's day 0 */
    TEST_ASSERT_EQUAL_INT(1, n_ha);     /* the SAME entity on both pages is requested once */
    TEST_ASSERT_EQUAL_STRING("sensor.hallway", ids[0]);
}

/* A STATIC widget's binding must not make the tick fetch. The default layout draws its dividers
 * as static widgets, and a static box is baked into the bitmap — fetching an alert document for
 * one would be a request whose result is never drawn. */
static void test_scan_ignores_static_widgets(void)
{
    cJSON *root = cJSON_Parse(TWO_PAGES);
    TEST_ASSERT_NOT_NULL(root);
    int cur = 0, day = 0, alert = 0, ha = 0, maxday = 0, n_ha = 0;
    char ids[LAYOUT_MAX_FIELDS][48];
    /* One page only: page A's lone owm-alert binding is on a STATIC widget, so nothing about the
     * alert bar may be requested. */
    layout_scan_all_pages(root, 1, &cur, &day, &alert, &ha, &maxday, ids, LAYOUT_MAX_FIELDS, &n_ha);
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_INT(1, cur);
    TEST_ASSERT_EQUAL_INT(0, alert);
    TEST_ASSERT_EQUAL_INT(0, day);
    TEST_ASSERT_EQUAL_INT(1, ha);
}

/* The HA entity list is bounded by its capacity: a document binding more entities than the
 * template buffer can carry must report the ones it kept and not run past the array. */
static void test_scan_bounds_the_ha_list(void)
{
    cJSON *root = cJSON_Parse(TWO_PAGES);
    TEST_ASSERT_NOT_NULL(root);
    int cur = 0, day = 0, alert = 0, ha = 0, maxday = 0, n_ha = 0;
    char ids[1][48];
    layout_scan_all_pages(root, 2, &cur, &day, &alert, &ha, &maxday, ids, 1, &n_ha);
    cJSON_Delete(root);

    TEST_ASSERT_EQUAL_INT(1, n_ha);     /* capacity respected */
    TEST_ASSERT_EQUAL_INT(1, ha);       /* and the need is still reported */
}

static void test_scan_of_a_null_root_is_all_zero(void)
{
    int cur = 1, day = 1, alert = 1, ha = 1, maxday = 9, n_ha = 9;
    char ids[4][48];
    layout_scan_all_pages(NULL, 3, &cur, &day, &alert, &ha, &maxday, ids, 4, &n_ha);
    TEST_ASSERT_EQUAL_INT(0, cur);
    TEST_ASSERT_EQUAL_INT(0, day);
    TEST_ASSERT_EQUAL_INT(0, alert);
    TEST_ASSERT_EQUAL_INT(0, ha);
    TEST_ASSERT_EQUAL_INT(0, maxday);
    TEST_ASSERT_EQUAL_INT(0, n_ha);

    /* A zero page count likewise asks nothing of the fetcher. */
    cJSON *root = cJSON_Parse(TWO_PAGES);
    layout_scan_all_pages(root, 0, &cur, &day, &alert, &ha, &maxday, ids, 4, &n_ha);
    cJSON_Delete(root);
    TEST_ASSERT_EQUAL_INT(0, ha);
}

/* A PREFIX LONGER THAN THE OLD 5-CHARACTER LIMIT SURVIVES INTACT.
 *
 * The affixes were char[6], so "layers " was cut to "layer" and the box drew "layer0" where the
 * user had configured "layers " — reported from the bench exactly that way. The struct now holds
 * 15 characters, and this pins both the value that caused the report and the exact boundary, since
 * an off-by-one in a copy_str() capacity is invisible until a string sits right on it. */
static void test_long_affixes_are_not_truncated(void)
{
    static const char *doc =
        "{\"schemaVersion\":1,\"pages\":[{\"name\":\"P\",\"widgets\":["
          "{\"id\":\"val2\",\"x\":40,\"y\":100,\"w\":300,\"h\":60,\"role\":\"dynamic\","
           "\"binding\":{\"kind\":\"ha\",\"entityId\":\"sensor.cc1_current_layer\"},"
           "\"format\":{\"decimals\":0,\"prefix\":\"layers \",\"suffix\":\" done\"}},"
          "{\"id\":\"max\",\"x\":40,\"y\":200,\"w\":300,\"h\":60,\"role\":\"dynamic\","
           "\"binding\":{\"kind\":\"owm-current\",\"owmField\":\"temp\"},"
           "\"format\":{\"prefix\":\"123456789012345\",\"suffix\":\"abcdefghijklmno\"}}"
        "]}]}";

    layout_widget_t w[LAYOUT_MAX_FIELDS];
    const int n = layout_widgets_parse(doc, 0, w, LAYOUT_MAX_FIELDS);
    TEST_ASSERT_EQUAL_INT(2, n);

    /* The reported case: prefix keeps its trailing space and its final 's'. */
    TEST_ASSERT_EQUAL_STRING("layers ", w[0].format.prefix);
    TEST_ASSERT_EQUAL_STRING(" done", w[0].format.suffix);

    /* The boundary: exactly 15 characters fit, and the space for that 15th character plus the
     * terminator is what the old char[6] did not have. */
    TEST_ASSERT_EQUAL_STRING("123456789012345", w[1].format.prefix);
    TEST_ASSERT_EQUAL_STRING("abcdefghijklmno", w[1].format.suffix);
    TEST_ASSERT_EQUAL_INT(15, (int)strlen(w[1].format.prefix));
}

/* AN OVER-LONG AFFIX IS TRUNCATED, NOT AN OVERFLOW. A hand-edited document (or one from a newer
 * app) can carry more than the struct holds; the parse must clip it and stay terminating rather
 * than running past the array. */
static void test_over_long_affixes_are_clipped_safely(void)
{
    static const char *doc =
        "{\"schemaVersion\":1,\"pages\":[{\"name\":\"P\",\"widgets\":["
          "{\"id\":\"w\",\"role\":\"dynamic\","
           "\"binding\":{\"kind\":\"owm-current\",\"owmField\":\"temp\"},"
           "\"format\":{\"prefix\":\"0123456789abcdefghijABCDEFGHIJ\"}}"
        "]}]}";

    layout_widget_t w[LAYOUT_MAX_FIELDS];
    TEST_ASSERT_EQUAL_INT(1, layout_widgets_parse(doc, 0, w, LAYOUT_MAX_FIELDS));
    TEST_ASSERT_EQUAL_INT((int)sizeof(w[0].format.prefix) - 1, (int)strlen(w[0].format.prefix));
    TEST_ASSERT_EQUAL_MEMORY("0123456789abcde", w[0].format.prefix, 16);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_parses_every_widget_in_order);
    RUN_TEST(test_geometry_binding_and_font_round_trip);
    RUN_TEST(test_small_size_selects_the_body_face);
    RUN_TEST(test_ha_entity_id_is_read);
    RUN_TEST(test_empty_fallback_survives);
    RUN_TEST(test_static_role_is_marked);
    RUN_TEST(test_missing_page_yields_no_widgets);
    RUN_TEST(test_no_widgets_key_yields_none);
    RUN_TEST(test_out_of_range_geometry_is_clamped);
    RUN_TEST(test_unknown_binding_is_none);
    RUN_TEST(test_unfireable_rules_are_dropped);
    RUN_TEST(test_cap_is_respected);
    RUN_TEST(test_garbage_and_empty_input);
    RUN_TEST(test_from_root_matches_a_full_parse);
    RUN_TEST(test_scan_covers_every_page);
    RUN_TEST(test_scan_ignores_static_widgets);
    RUN_TEST(test_scan_bounds_the_ha_list);
    RUN_TEST(test_scan_of_a_null_root_is_all_zero);
    RUN_TEST(test_long_affixes_are_not_truncated);
    RUN_TEST(test_over_long_affixes_are_clipped_safely);
    return UNITY_END();
}
