#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "widgets.h"
#include "fonts.h"
#include "alerts.h"

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
    return UNITY_END();
}
