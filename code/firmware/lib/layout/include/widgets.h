#pragma once
#include "alerts.h"

/* The DEVICE's view of a layout widget (FR-2, IF-1).
 *
 * WHY THIS EXISTS AT ALL — the defect this file fixes: the firmware used to render with a
 * hard-coded two-box array and stamp only a temperature, so a layout pushed from the web app
 * was parsed for scheduling and then discarded. Every widget bound to anything but the current
 * temperature was blank on the glass, and it appeared in a box the user never authored.
 *
 * The device stays layout-INDEPENDENT (FR-1): it never decides where anything goes and never
 * draws a label. It reads the boxes the web app defined and stamps the current reading into
 * each. That is what lets a layout change ship with no firmware update.
 *
 * Parsing is SEPARATE from layout_config_t on purpose. layout_config_t is small and is
 * stack-allocated on the boot path; a page of widgets is not, and folding it in would have put
 * ~4 KB on the 3.5 KB app_main stack. The caller owns the array and its size. */

/* Bounded per page. The shipped default uses 9; 24 leaves room for a dense layout without
 * making the caller allocate an unbounded amount from a document it did not write. */
#define LAYOUT_MAX_FIELDS 24

/* Bounded per widget. The default layout attaches 4 temperature rules; 6 is headroom. */
#define LAYOUT_MAX_RULES 6

typedef enum {
    BIND_NONE = 0,      /* a dynamic widget with no binding: renders its fallback, not garbage */
    BIND_OWM_CURRENT,
    BIND_OWM_DAILY,
    BIND_OWM_ALERT,     /* OWM official severe-weather alerts (FR-14 mechanism 2) */
    BIND_HA             /* a Home Assistant entity, by id (FR-23) */
} bind_kind_t;

typedef enum {
    OWM_F_TEMP = 0,
    OWM_F_MIN,
    OWM_F_MAX,
    OWM_F_WIND,
    OWM_F_HUMIDITY,
    OWM_F_CONDITION,
    OWM_F_ICON,
    /* The place name OWM resolved the coordinates to (FR-17's "location/zip display"). Text,
     * like CONDITION — a widget bound to it draws words, not a number. */
    OWM_F_CITY,
    /* WHEN the current conditions were observed, as OWM reports it ("dt" in the current
     * response), rendered in the location's own local time.
     *
     * WHY THIS COMES FROM OWM AND NOT FROM A CLOCK: this board has no RTC and the firmware never
     * syncs one — the `now` threaded through the value path is esp_timer_get_time(), i.e. seconds
     * since boot. A local clock would therefore print a time near 1970. OWM's response already
     * carries a real Unix timestamp AND the location's UTC offset, so a genuine local date/time
     * needs no new hardware and no timezone setting: the device reads both from the document it
     * was already fetching. The value is the OBSERVATION time, which is the honest thing to put
     * under a heading like "updated" — it advances when OWM refreshes its reading, not merely when
     * this device last drew a frame. */
    OWM_F_TIME
} owm_field_t;

typedef struct {
    bind_kind_t kind;
    owm_field_t owm_field;
    int         day_index;      /* for BIND_OWM_DAILY: 0 = today */
    char        entity_id[48];  /* for BIND_HA */
} binding_t;

/* The text-formatting rules (mirrors webapp Format). Kept as fixed arrays rather than
 * pointers into the JSON: the document is freed long before the value is stamped.
 *
 * WHY THE AFFIXES ARE 16 BYTES, NOT 6. They were 6 — five characters plus the terminator — and a
 * prefix like "layers " was silently cut to "layer", so the box drew "layer0" where the user had
 * asked for "layers 0". The document carries no such limit, so the app happily stored the longer
 * string and only the glass disagreed; there was no error anywhere. 15 characters covers the real
 * cases ("layers ", "Current layer ", "Outdoor ", " mph", " (forecast)") with room to spare.
 *
 * THIS IS THE ONLY LIMIT, and the web app mirrors it (MAX_AFFIX in property-panel) so a field can
 * never accept text the device would then trim — a control offering more than the device can store
 * is the same defect as the truncation itself, just discovered later.
 *
 * The struct is embedded in layout_widget_t, which page_render_t holds 24 of, so every byte here
 * costs 24 in .bss — DRAM the heap never gets (see the render notes). 16 is the size that covers
 * the real cases without that multiplication turning into a heap problem. */
typedef struct {
    int  decimals;              /* -1 = unset, use the default */
    char prefix[16];
    char suffix[16];
    char fallback[8];
} widget_format_t;

/* HOW LONG A FORMATTED VALUE CAN BE, and why it is 48.
 *
 * A formatted value is prefix + the number + suffix, so the buffer that holds it must cover the
 * worst case of all three. The affixes are 15 characters each, which leaves 16 for the numeric
 * body: a sign, several integer digits, a point and up to six decimals (the format's own maximum)
 * come to "-12345.123456" — 13, with a little room spare.
 *
 * WHY A SHARED CONSTANT RATHER THAN A 40 SPELLED OUT AT EACH DECLARATION: the value buffer is
 * declared in several places (the live page, the previous-frame snapshot, the /api/values store)
 * and was 40 in all of them — enough when the affixes were five characters, too small the moment
 * they grew. A single constant, with the static assert below tying it to the struct, means the next
 * change to the affix size is a compile error rather than a silently truncated reading.
 *
 * IT IS DELIBERATELY NOT SIZED FOR AN ABSURD MAGNITUDE. A double holding 1e30 prints 30 integer
 * digits, which no affix budget can absorb; that case truncated before this change too and is not
 * made worse. The bound that matters is the one an actual reading can reach, and the assert below
 * makes the affix side of it impossible to get wrong. */
#define LAYOUT_VALUE_BUF 48

/* Compile-time proof that the value buffer can hold the worst case its own format allows. A
 * mismatch here would show up as a value truncated mid-number on the glass — legible-but-wrong,
 * the same failure class the affix size caused — so it is a build error, not a runtime check. */
typedef char layout_value_buf_must_cover_affixes[
    (LAYOUT_VALUE_BUF >= sizeof(((widget_format_t *)0)->prefix) +
                         sizeof(((widget_format_t *)0)->suffix) + 16) ? 1 : -1];

typedef struct {
    int             x, y, w, h;
    char            align_h;        /* 'L' | 'C' | 'R' */
    char            align_v;        /* 'T' | 'M' | 'B' */
    int             font_id;        /* a face on the ladder (font_id_t); see fonts.h */
    char            role;           /* 's' static, 'd' dynamic */
    /* The widget's id from the document. Kept because it is the ONLY key that ties a
     * resolved value back to the box the editor drew, which is what /api/values needs to
     * satisfy FR-27's preview: without it the device could report "68.4" but not which
     * widget it belongs to, and the app would have to re-derive the resolution to line
     * them up — a second implementation of exactly the logic this endpoint exists to share.
     * Sized to the app's own id bound; a longer id is truncated, which only risks two
     * widgets colliding in the preview, never on the glass. */
    char            id[24];
    binding_t       binding;
    widget_format_t format;
    alert_rule_t    rules[LAYOUT_MAX_RULES];
    int             n_rules;
} layout_widget_t;

/* Parse the widgets of page `page_index` out of a full config document.
 *
 * Returns the number of widgets written (0 or more) on success, or a negative value if the
 * document is unusable. A widget that cannot be understood is SKIPPED rather than aborting the
 * page: a config from a newer web app must still draw the fields it does understand, because a
 * blank panel is a far worse failure than a missing box.
 *
 * `out` must hold `cap` entries. Every parsed widget is normalised: a role that is not
 * 'dynamic' is left for the static layer, an out-of-range geometry is clamped to the panel, and
 * an unreadable font size selects the smaller face — the same "never render a broken layout"
 * rule layout_config_parse() applies to its own fields. */
int layout_widgets_parse(const char *json, int page_index,
                         layout_widget_t *out, int cap);

/* Parse the widgets of page `page_index` from an ALREADY-PARSED cJSON root (`void *` so cJSON
 * stays out of this header — the caller owns the tree and must keep it alive).
 *
 * WHY THIS EXISTS: the refresh tick resolves EVERY page so the editor can preview any of them
 * (FR-27), and that would otherwise mean parsing the whole document once per page. Parsing the
 * document is the expensive part on this part (cJSON's tree is several times the document), so
 * the tree is parsed ONCE and each page is read out of it. */
int layout_widgets_from_root(void *root, int page_index,
                             layout_widget_t *out, int cap);

/* Scan EVERY page of an already-parsed root and report what the whole document needs fetched, so
 * one set of fetches serves every page rather than the tick fetching per page.
 *
 * `ha_out`/`ha_cap`/`ha_n` collect the DISTINCT Home Assistant entity ids across all pages, in
 * first-seen order — the same order the template request and the per-widget slot lookup use, so a
 * page's widget still finds its token. Any output pointer may be NULL. */
void layout_scan_all_pages(void *root, int npages,
                           int *need_current, int *need_daily, int *need_alert,
                           int *need_ha, int *max_day,
                           char (*ha_out)[48], int ha_cap, int *ha_n);
