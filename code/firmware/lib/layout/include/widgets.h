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
    OWM_F_CITY
} owm_field_t;

typedef struct {
    bind_kind_t kind;
    owm_field_t owm_field;
    int         day_index;      /* for BIND_OWM_DAILY: 0 = today */
    char        entity_id[48];  /* for BIND_HA */
} binding_t;

/* The text-formatting rules (mirrors webapp Format). Kept as fixed arrays rather than
 * pointers into the JSON: the document is freed long before the value is stamped. */
typedef struct {
    int  decimals;              /* -1 = unset, use the default */
    char prefix[6];
    char suffix[6];
    char fallback[6];
} widget_format_t;

typedef struct {
    int             x, y, w, h;
    char            align_h;        /* 'L' | 'C' | 'R' */
    char            align_v;        /* 'T' | 'M' | 'B' */
    int             font_id;        /* FONT_BODY or FONT_VALUE */
    char            role;           /* 's' static, 'd' dynamic */
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
