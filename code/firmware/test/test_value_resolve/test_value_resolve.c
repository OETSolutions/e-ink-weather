#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "value_resolve.h"
#include "widgets.h"
#include "alerts.h"
#include "fonts.h"

/* Resolving a widget to the exact string on the glass. This is the half of the render path that
 * is pure logic, and the half that was MISSING: the firmware used to stamp only a temperature,
 * so every forecast, Home Assistant entity and alert widget was blank. */

void setUp(void) {}
void tearDown(void) {}

/* Shaped like a real 2.5/weather response (verified live: 521 bytes, temp 72.28). */
static const char *CURRENT =
"{\"coord\":{\"lat\":41.78,\"lon\":-111.81},"
 "\"weather\":[{\"id\":802,\"main\":\"Clouds\",\"description\":\"scattered clouds\",\"icon\":\"03d\"}],"
 "\"main\":{\"temp\":72.28,\"feels_like\":71.1,\"temp_min\":69.67,\"temp_max\":74.08,"
          "\"pressure\":1019,\"humidity\":40},"
 "\"wind\":{\"speed\":6.91,\"deg\":330},\"dt\":1758300000,"
 "\"name\":\"North Logan\"}";

/* 2.5/forecast: "list" of 3-hour blocks with temp_min/temp_max FLAT under "main". */
static const char *FORECAST =
"{\"city\":{\"timezone\":-21600},"
 "\"list\":["
  "{\"dt\":1758300000,\"main\":{\"temp\":70.1,\"temp_min\":69.0,\"temp_max\":71.0}},"
  "{\"dt\":1758303600,\"main\":{\"temp\":72.0,\"temp_min\":71.0,\"temp_max\":73.5}},"
  "{\"dt\":1758310800,\"main\":{\"temp\":66.0,\"temp_min\":64.0,\"temp_max\":68.0}},"
  "{\"dt\":1758390000,\"main\":{\"temp\":60.0,\"temp_min\":55.0,\"temp_max\":62.0}}"
 "]}";

static layout_widget_t mk(bind_kind_t kind)
{
    layout_widget_t w;
    memset(&w, 0, sizeof(w));
    w.role = 'd';
    w.align_h = 'L';
    w.align_v = 'T';
    w.font_id = FONT_BODY;
    w.format.decimals = -1;
    w.format.fallback[0] = '\0';
    strcpy(w.format.fallback, "--");
    w.binding.kind = kind;
    return w;
}

static void test_current_temperature_formats_with_suffix(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.format.decimals = 1;
    strcpy(w.format.suffix, "\xc2\xb0""F");

    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; int n_ids = 0;
    char buf[64];
    datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, n_ids, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("72.3\xc2\xb0""F", buf);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
}

/* A WHOLE-DEGREE widget must not show a tenth: decimals=0 is a real user choice. */
static void test_decimals_zero_rounds(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.format.decimals = 0;
    strcpy(w.format.suffix, " mph");

    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("72 mph", buf);
}

/* The wind and humidity fields live in DIFFERENT places in the document than the temperature —
 * "wind" is a top-level sibling of "main" in 2.5/weather. Reading them out of "main" would
 * return NOT_FOUND and the widget would silently show its fallback forever. */
static void test_wind_and_humidity_come_from_the_right_places(void)
{
    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64]; datasrc_value_t v;

    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_WIND;
    w.format.decimals = 0;
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("7", buf);

    w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_HUMIDITY;
    w.format.decimals = 0;
    strcpy(w.format.suffix, "%");
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("40%", buf);
}

/* A CONDITION IS TEXT, not a number. Printing its numeric field would put "0.0" on the glass
 * where the words belong. */
static void test_condition_renders_words_not_a_number(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_CONDITION;

    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("scattered clouds", buf);
    TEST_ASSERT_EQUAL_INT(0, v.is_numeric);
}

/* A daily widget with dayIndex 0 takes TODAY's extreme, and dayIndex 1 a different day — the
 * index must actually select, not be ignored. */
static void test_daily_index_selects_the_day(void)
{
    value_sources_t src = { .owm_daily = FORECAST };
    char ids[1][48]; char buf[64]; datasrc_value_t v;

    layout_widget_t w = mk(BIND_OWM_DAILY);
    w.format.decimals = 0;
    w.binding.owm_field = OWM_F_MAX;
    w.binding.day_index = 0;
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("74", buf);      /* max over the three blocks of day 0 */

    w.binding.day_index = 1;
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("62", buf);      /* the single block of day 1 */
}

static void test_daily_min_uses_the_minimum(void)
{
    value_sources_t src = { .owm_daily = FORECAST };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    layout_widget_t w = mk(BIND_OWM_DAILY);
    w.format.decimals = 0;
    w.binding.owm_field = OWM_F_MIN;
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("64", buf);      /* min over day 0: 69.0, 64.0 */
}

/* A MISSING SOURCE SHOWS THE WIDGET'S FALLBACK — never 0.0, which is plausible weather. */
static void test_missing_source_uses_fallback_not_zero(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    value_sources_t src = { .owm_current = NULL };   /* the fetch failed */
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(0, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

/* The alert bar ships with an EMPTY fallback so quiet weather leaves a blank bar rather than two
 * dashes across the bottom of the panel.
 *
 * `owm_alerts_supported = 1` is One Call 3.0 — the product where an empty alert list really does
 * mean quiet weather. See test_alert_bar_says_so_when_the_product_cannot_alert for the free
 * tier, where the same empty list means something else entirely. */
static void test_alert_bar_empty_fallback_is_honoured(void)
{
    layout_widget_t w = mk(BIND_OWM_ALERT);
    strcpy(w.format.fallback, "");
    value_sources_t src = { .owm_current = CURRENT, .owm_daily = FORECAST,
                            .owm_alerts_supported = 1 };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(0, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

/* The alert bar DOES render words when the response carries an official alert. */
static void test_owm_alert_renders_when_present(void)
{
    const char *with_alert =
      "{\"alerts\":[{\"sender_name\":\"NWS\",\"event\":\"Red Flag Warning\"}],"
       "\"current\":{\"temp\":70,\"dt\":1758300000}}";
    layout_widget_t w = mk(BIND_OWM_ALERT);
    value_sources_t src = { .owm_current = with_alert, .owm_alerts_supported = 1 };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("WEATHER ALERT", buf);
}

/* FR-7: WHEN THE PRODUCT CANNOT CARRY ALERTS, SAY SO.
 *
 * The free 2.5 products have no "alerts" key at all, so an empty alert list is NOT evidence of
 * quiet weather — it is evidence that this product never has alerts. Rendering the widget's
 * shipped fallback (the empty string) there puts a blank bar on the glass that the user reads as
 * "no severe weather". That is the silent degradation FR-7 forbids, so this case must produce
 * words, and it must do so as a REAL reading (return 1), not as a fallback. */
static void test_alert_bar_says_so_when_the_product_cannot_alert(void)
{
    layout_widget_t w = mk(BIND_OWM_ALERT);
    strcpy(w.format.fallback, "");
    /* The free tier: a perfectly good 2.5 document, with no "alerts" key because it has none. */
    value_sources_t src = { .owm_current = CURRENT, .owm_daily = FORECAST,
                            .owm_alerts_supported = 0 };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Alerts unavailable on this product", buf);
    /* It is a known answer, not an unavailable source. */
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
}

/* THE MESSAGE MUST FIT THE RENDER PATH'S PER-WIDGET BUFFER.
 *
 * app_refresh.c copies each resolved string into a 40-byte slot (page_render_t.values) with a
 * plain snprintf, which truncates SILENTLY. An earlier, longer wording for this message reached
 * the glass cut mid-word — "Offici... on this pro" — with no error in any log, because every
 * layer between here and the panel did exactly what it was asked. The limit is real, so the text
 * is sized to it and this test keeps it that way. */
static void test_degradation_message_fits_the_render_buffer(void)
{
    layout_widget_t w = mk(BIND_OWM_ALERT);
    strcpy(w.format.fallback, "");
    value_sources_t src = { .owm_current = CURRENT, .owm_alerts_supported = 0 };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    (void)value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    /* 40 bytes including the terminator — the slot size in page_render_t.values. */
    TEST_ASSERT_LESS_THAN_UINT32(40, (uint32_t)strlen(buf) + 1);
}

/* The degradation must win even when an alert IS present in the document — a product that cannot
 * carry alerts cannot be trusted to have parsed one, and silently rendering "WEATHER ALERT" from
 * a document we have declared alert-less would be the same lie in the other direction. */
static void test_product_without_alerts_does_not_render_a_found_alert(void)
{
    const char *with_alert =
      "{\"alerts\":[{\"sender_name\":\"NWS\",\"event\":\"Red Flag Warning\"}],"
       "\"current\":{\"temp\":70,\"dt\":1758300000}}";
    layout_widget_t w = mk(BIND_OWM_ALERT);
    value_sources_t src = { .owm_current = with_alert, .owm_alerts_supported = 0 };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("Alerts unavailable on this product", buf);
}

/* THRESHOLD RULES ARE THE WHOLE POINT OF THE ALERT WIDGET. A reading past the threshold
 * replaces the number with the severity word. */
static void test_threshold_rule_replaces_the_reading(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.format.decimals = 1;
    w.n_rules = 1;
    w.rules[0].op = ALERT_OP_GTE;
    w.rules[0].threshold = 100;
    w.rules[0].level = ALERT_SEVERE;

    const char *hot = "{\"main\":{\"temp\":104.5,\"humidity\":10},\"wind\":{},\"dt\":1}";
    value_sources_t src = { .owm_current = hot };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("severe", buf);
}

/* AN UNAVAILABLE READING MUST NEVER RAISE AN ALARM. A "not equal to 0" rule is the trap: every
 * comparison against NaN is false except `!=`, so without the guard a missing sensor would
 * raise an alarm nobody can act on. */
static void test_unavailable_reading_never_raises_an_alert(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.n_rules = 1;
    w.rules[0].op = ALERT_OP_NE;
    w.rules[0].threshold = 0;
    w.rules[0].level = ALERT_SEVERE;

    value_sources_t src = { .owm_current = NULL };   /* no reading at all */
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(0, value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

/* The most severe firing rule wins, regardless of order. */
static void test_most_severe_rule_wins(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.n_rules = 3;
    w.rules[0].op = ALERT_OP_GTE; w.rules[0].threshold = 10; w.rules[0].level = ALERT_ADVISORY;
    w.rules[1].op = ALERT_OP_GTE; w.rules[1].threshold = 20; w.rules[1].level = ALERT_SEVERE;
    w.rules[2].op = ALERT_OP_GTE; w.rules[2].threshold = 15; w.rules[2].level = ALERT_WARNING;

    const char *t = "{\"main\":{\"temp\":25,\"humidity\":10},\"wind\":{},\"dt\":1}";
    value_sources_t src = { .owm_current = t };
    char ids[1][48]; char buf[64]; datasrc_value_t v;
    value_format_widget(&w, &src, ids, 0, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("severe", buf);
}

/* HA values come back as ONE '|'-separated line for the whole page, so the SECOND HA widget
 * reads the SECOND token. Using a widget-local list would make every HA widget show the same
 * entity's value. */
static void test_ha_widgets_read_their_own_token(void)
{
    layout_widget_t w[2];
    w[0] = mk(BIND_HA);
    strcpy(w[0].binding.entity_id, "sensor.upstairs_hallway_temperature");
    w[1] = mk(BIND_HA);
    strcpy(w[1].binding.entity_id, "sensor.64b708cfe0fc_sensor_2_temperature_f");

    char ids[LAYOUT_MAX_FIELDS][48];
    int needed = 0;
    const int n_ids = value_collect_ha_entities(w, 2, ids, LAYOUT_MAX_FIELDS, &needed);
    TEST_ASSERT_EQUAL_INT(2, n_ids);
    TEST_ASSERT_EQUAL_INT(2, needed);
    TEST_ASSERT_EQUAL_STRING("sensor.upstairs_hallway_temperature", ids[0]);
    TEST_ASSERT_EQUAL_STRING("sensor.64b708cfe0fc_sensor_2_temperature_f", ids[1]);

    value_sources_t src = { .ha_line = "68.4|41.2" };
    w[0].format.decimals = 1;
    w[1].format.decimals = 1;
    char buf[64];

    value_format_widget(&w[0], &src, ids, n_ids, 0, NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("68.4", buf);

    value_format_widget(&w[1], &src, ids, n_ids, 0, NULL, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("41.2", buf);
}

/* An HA sensor reporting "unavailable" must show the fallback, not 0.0. */
static void test_ha_unavailable_shows_fallback(void)
{
    layout_widget_t w = mk(BIND_HA);
    strcpy(w.binding.entity_id, "sensor.dead_node");
    char ids[LAYOUT_MAX_FIELDS][48];
    int needed = 0;
    const int n_ids = value_collect_ha_entities(&w, 1, ids, LAYOUT_MAX_FIELDS, &needed);

    value_sources_t src = { .ha_line = "unavailable" };
    char buf[64];
    TEST_ASSERT_EQUAL_INT(0, value_format_widget(&w, &src, ids, n_ids, 0, NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

/* A NON-NUMERIC HA state renders as its own WORD — the binary_sensor case.
 *
 * The numeric-only classifier rejected "off" as UNAVAILABLE, so a working door sensor showed the
 * widget's fallback. The state is a real reading and must reach the glass, and it must carry no
 * suffix: this widget's format is a temperature's, so appending it would draw "off°F". */
static void test_ha_text_state_renders_its_word_without_a_suffix(void)
{
    layout_widget_t w = mk(BIND_HA);
    strcpy(w.binding.entity_id, "binary_sensor.door_open");
    strcpy(w.format.suffix, "\xc2\xb0""F");      /* the temperature affix must NOT appear */
    char ids[LAYOUT_MAX_FIELDS][48];
    int needed = 0;
    const int n_ids = value_collect_ha_entities(&w, 1, ids, LAYOUT_MAX_FIELDS, &needed);

    value_sources_t src = { .ha_line = "off" };
    char buf[64];
    datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, n_ids, 0, &v, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("off", buf);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_INT(0, v.is_numeric);

    /* A threshold rule cannot fire on a word — there is no magnitude to compare. */
    w.n_rules = 1;
    w.rules[0].op = ALERT_OP_GT;
    w.rules[0].threshold = 0;
    w.rules[0].level = ALERT_SEVERE;
    value_format_widget(&w, &src, ids, n_ids, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("off", buf);      /* not "severe" */
}

/* A page that binds the same entity twice must ask HA for it ONCE — the template response is
 * one line, and duplicating the entity burns the 512-byte budget for nothing. */
static void test_duplicate_ha_entities_are_deduplicated(void)
{
    layout_widget_t w[3];
    w[0] = mk(BIND_HA); strcpy(w[0].binding.entity_id, "sensor.hall");
    w[1] = mk(BIND_HA); strcpy(w[1].binding.entity_id, "sensor.hall");
    w[2] = mk(BIND_HA); strcpy(w[2].binding.entity_id, "sensor.out");

    char ids[LAYOUT_MAX_FIELDS][48];
    int needed = 0;
    const int n_ids = value_collect_ha_entities(w, 3, ids, LAYOUT_MAX_FIELDS, &needed);
    TEST_ASSERT_EQUAL_INT(2, n_ids);
    TEST_ASSERT_EQUAL_INT(3, needed);      /* three widgets wanted one */
    TEST_ASSERT_EQUAL_STRING("sensor.hall", ids[0]);
    TEST_ASSERT_EQUAL_STRING("sensor.out", ids[1]);
}

/* A STATIC widget is baked into the bitmap and must not contribute a fetch. */
static void test_scan_needs_ignores_static_widgets(void)
{
    layout_widget_t w[2];
    w[0] = mk(BIND_NONE); w[0].role = 's';
    w[1] = mk(BIND_OWM_CURRENT);
    w[1].role = 'd';

    value_needs_t needs;
    value_scan_needs(w, 2, &needs);
    TEST_ASSERT_EQUAL_INT(1, needs.need_owm_current);
    TEST_ASSERT_EQUAL_INT(0, needs.need_owm_daily);
    TEST_ASSERT_EQUAL_INT(0, needs.need_ha);
}

/* The scan must report the HIGHEST day index any widget asks for, so the caller knows how much
 * forecast to fetch — and must report all four sources when all four are bound. */
static void test_scan_needs_reports_all_sources_and_max_day(void)
{
    layout_widget_t w[4];
    w[0] = mk(BIND_OWM_CURRENT);
    w[1] = mk(BIND_OWM_DAILY); w[1].binding.day_index = 3;
    w[2] = mk(BIND_OWM_ALERT);
    w[3] = mk(BIND_HA); strcpy(w[3].binding.entity_id, "sensor.x");

    value_needs_t needs;
    value_scan_needs(w, 4, &needs);
    TEST_ASSERT_EQUAL_INT(1, needs.need_owm_current);
    TEST_ASSERT_EQUAL_INT(1, needs.need_owm_daily);
    TEST_ASSERT_EQUAL_INT(1, needs.need_owm_alert);
    TEST_ASSERT_EQUAL_INT(1, needs.need_ha);
    TEST_ASSERT_EQUAL_INT(3, needs.max_day_index);
}

/* A daily widget bound to a field with no daily meaning is a modelling mistake: it shows the
 * fallback rather than an invented number. */
static void test_daily_binding_to_a_nonsense_field_falls_back(void)
{
    layout_widget_t w = mk(BIND_OWM_DAILY);
    w.binding.owm_field = OWM_F_CONDITION;
    value_sources_t src = { .owm_daily = FORECAST };
    char ids[1][48]; char buf[64];
    TEST_ASSERT_EQUAL_INT(0, value_format_widget(&w, &src, ids, 0, 0, NULL, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("--", buf);
}

/* ---- which branch drew the text: the flag GET /api/values reports (FR-27) ----
 *
 * THE EDITOR RE-FORMATS A READING LOCALLY after the user edits the decimals or affixes, so that a
 * format edit shows up without a save and a repaint. It may do that ONLY when the drawn text really
 * IS that reading's plain rendering — a firing alert shows a level word, a text reading shows a
 * condition or a sensor state, an icon binding shows the OWM code, and a failed fetch shows the
 * widget's fallback. Re-format in any of those and the editor draws a number over a box the panel
 * paints a word in. The firmware is the only party that knows which branch ran, so it reports it —
 * the editor could not infer it, since a fallback of "72.5" is indistinguishable from a reading.
 */

static void test_a_number_rendering_is_flagged_as_one(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.format.decimals = 1;
    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64];
    int rendered = -1;
    datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget_rendered(&w, &src, ids, 0, 0, &v, &rendered, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(1, rendered);
    /* The reading travels out with it, exactly as the panel drew it — 72.28 at one decimal. */
    TEST_ASSERT_TRUE(v.value > 72.27 && v.value < 72.29);
}

static void test_a_text_reading_is_not_flagged_as_a_number(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_CONDITION;
    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64];
    int rendered = -1;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget_rendered(&w, &src, ids, 0, 0, NULL, &rendered, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, rendered);
}

/* An OWM icon binding carries the raw code ("03d"), which the renderer turns into a picture. There
 * is no number behind it, so re-format would blank the icon. */
static void test_an_icon_code_is_not_flagged_as_a_number(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_ICON;
    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64];
    int rendered = -1;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget_rendered(&w, &src, ids, 0, 0, NULL, &rendered, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("03d", buf);
    TEST_ASSERT_EQUAL_INT(0, rendered);
}

/* A FIRING ALERT REPLACES THE READING WITH A WORD, so this is the case where a naive
 * "value is finite, re-format it" would draw a temperature over the alarm. */
static void test_an_alert_word_is_not_flagged_as_a_number(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.n_rules = 1;
    w.rules[0].op = ALERT_OP_GT;
    w.rules[0].threshold = 60;
    w.rules[0].level = ALERT_SEVERE;
    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; char buf[64];
    int rendered = -1;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget_rendered(&w, &src, ids, 0, 0, NULL, &rendered, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("severe", buf);
    TEST_ASSERT_EQUAL_INT(0, rendered);
}

/* A MISSING SOURCE SHOWS THE FALLBACK, which the user may have set to anything — including
 * something that looks like a number. Re-format must therefore not run here either; the flag is
 * what decides, not the shape of the string. */
static void test_a_fallback_is_not_flagged_as_a_number(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    strcpy(w.format.fallback, "72.5");
    value_sources_t src = { .owm_current = NULL };   /* the fetch failed */
    char ids[1][48]; char buf[64];
    int rendered = -1;
    TEST_ASSERT_EQUAL_INT(0, value_format_widget_rendered(&w, &src, ids, 0, 0, NULL, &rendered, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("72.5", buf);
    TEST_ASSERT_EQUAL_INT(0, rendered);
}

/* The plain entry point must behave EXACTLY as before — it is the path the panel draws through,
 * and the flag is a report about that path, not a change to it. */
static void test_the_flag_does_not_change_what_is_drawn(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TEMP;
    w.format.decimals = 1;
    strcpy(w.format.suffix, "\xc2\xb0""F");
    value_sources_t src = { .owm_current = CURRENT };
    char a[64], b[64];
    char ids[1][48];
    int rendered = 0;
    datasrc_value_t v1, v2;
    const int r1 = value_format_widget(&w, &src, ids, 0, 0, &v1, a, sizeof(a));
    const int r2 = value_format_widget_rendered(&w, &src, ids, 0, 0, &v2, &rendered, b, sizeof(b));
    TEST_ASSERT_EQUAL_INT(r1, r2);
    TEST_ASSERT_EQUAL_STRING(a, b);
    TEST_ASSERT_EQUAL_INT(v1.status, v2.status);
    TEST_ASSERT_TRUE(v1.value == v2.value);
}


/* ---- the "last updated" box on the glass ----
 *
 * THIS TEST EXISTS FOR THE DRAWN STRING, not for the parser: test_owm_parse proves the stamp's
 * TEXT, and this proves the RENDER PATH does not decorate it. A time box is almost always dropped
 * into a page whose neighbours are temperatures, so it inherits a format with a "°F" suffix the
 * moment the user copies a box — and the text branch of value_format_widget() is the only thing
 * standing between "Sep 18, 11:27 PM" and "Sep 18, 11:27 PM°F". The same class of bug shipped
 * once already as "off°F" on a door sensor.
 */
static void test_time_stamp_renders_as_a_bare_string(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TIME;
    w.format.decimals = 0;
    strcpy(w.format.prefix, "at ");
    strcpy(w.format.suffix, "\xc2\xb0""F");
    strcpy(w.format.fallback, "--");

    value_sources_t src = { .owm_current = CURRENT };   /* dt 1758300000; this fixture carries NO timezone, so the stamp is UTC */
    char ids[1][48]; int n_ids = 0;
    char buf[64];
    datasrc_value_t v;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget(&w, &src, ids, n_ids, 0, &v, buf, sizeof(buf)));
    /* No "at ", no "°F": the stamp is the reading. */
    TEST_ASSERT_EQUAL_STRING("Sep 19, 04:40 PM", buf);
    TEST_ASSERT_EQUAL_INT(DATASRC_OK, v.status);
    TEST_ASSERT_EQUAL_INT(0, v.is_numeric);
}

/* The editor re-formats a reading locally ONLY when the device says the drawn text is that
 * number's plain rendering. A stamp must say NO, or the editor would print a formatted number
 * over the time — so this asserts the flag as well as the string. */
static void test_time_stamp_is_not_reported_as_a_rendered_number(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TIME;
    w.format.decimals = 1;
    strcpy(w.format.suffix, "\xc2\xb0""F");
    value_sources_t src = { .owm_current = CURRENT };
    char ids[1][48]; int n_ids = 0;
    char buf[64];
    datasrc_value_t v;
    int rendered = 1;
    TEST_ASSERT_EQUAL_INT(1, value_format_widget_rendered(&w, &src, ids, n_ids, 0, &v, &rendered,
                                                          buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, rendered);
}

/* A page of stamps must not ask for a value it cannot draw: with no current document there is no
 * observation time, so the widget falls back rather than drawing a time invented from uptime. */
static void test_time_stamp_without_a_current_document_shows_the_fallback(void)
{
    layout_widget_t w = mk(BIND_OWM_CURRENT);
    w.binding.owm_field = OWM_F_TIME;
    strcpy(w.format.fallback, "--");
    value_sources_t src = { .owm_current = NULL };
    char ids[1][48]; int n_ids = 0;
    char buf[64];
    datasrc_value_t v;
    value_format_widget(&w, &src, ids, n_ids, 0, &v, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("--", buf);
    TEST_ASSERT_TRUE(v.status != DATASRC_OK);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_current_temperature_formats_with_suffix);
    RUN_TEST(test_decimals_zero_rounds);
    RUN_TEST(test_wind_and_humidity_come_from_the_right_places);
    RUN_TEST(test_condition_renders_words_not_a_number);
    RUN_TEST(test_daily_index_selects_the_day);
    RUN_TEST(test_daily_min_uses_the_minimum);
    RUN_TEST(test_missing_source_uses_fallback_not_zero);
    RUN_TEST(test_alert_bar_empty_fallback_is_honoured);
    RUN_TEST(test_owm_alert_renders_when_present);
    RUN_TEST(test_alert_bar_says_so_when_the_product_cannot_alert);
    RUN_TEST(test_product_without_alerts_does_not_render_a_found_alert);
    RUN_TEST(test_degradation_message_fits_the_render_buffer);
    RUN_TEST(test_threshold_rule_replaces_the_reading);
    RUN_TEST(test_unavailable_reading_never_raises_an_alert);
    RUN_TEST(test_most_severe_rule_wins);
    RUN_TEST(test_ha_widgets_read_their_own_token);
    RUN_TEST(test_ha_unavailable_shows_fallback);
    RUN_TEST(test_ha_text_state_renders_its_word_without_a_suffix);
    RUN_TEST(test_duplicate_ha_entities_are_deduplicated);
    RUN_TEST(test_scan_needs_ignores_static_widgets);
    RUN_TEST(test_scan_needs_reports_all_sources_and_max_day);
    RUN_TEST(test_daily_binding_to_a_nonsense_field_falls_back);
    RUN_TEST(test_a_number_rendering_is_flagged_as_one);
    RUN_TEST(test_a_text_reading_is_not_flagged_as_a_number);
    RUN_TEST(test_an_icon_code_is_not_flagged_as_a_number);
    RUN_TEST(test_an_alert_word_is_not_flagged_as_a_number);
    RUN_TEST(test_a_fallback_is_not_flagged_as_a_number);
    RUN_TEST(test_the_flag_does_not_change_what_is_drawn);
    RUN_TEST(test_time_stamp_renders_as_a_bare_string);
    RUN_TEST(test_time_stamp_is_not_reported_as_a_rendered_number);
    RUN_TEST(test_time_stamp_without_a_current_document_shows_the_fallback);
    return UNITY_END();
}
