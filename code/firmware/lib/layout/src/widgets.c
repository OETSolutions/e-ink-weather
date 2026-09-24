#include "widgets.h"
#include "fonts.h"
#include "cJSON.h"
#include <string.h>
#include <stdio.h>

/* The panel, for geometry clamping. Duplicated from canvas.h deliberately: this parser is
 * host-tested and must not drag the framebuffer code in with it. */
#define WGT_PANEL_W 920
#define WGT_PANEL_H 680

/* Mirrors webapp/src/canvas/face.ts:fontIdFor — ONE rule for the whole project, so a widget
 * cannot preview in one face and be stamped in another. The rule used to be a fixed 48 px
 * threshold between the two faces that existed; it is now NEAREST ladder entry by ratio, which
 * is the same function font_face_for_px() the renderer would use. Keying it here on a threshold
 * again would let the device and the preview disagree the moment the ladder changes. */

static int clamp_int(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void copy_str(char *dst, size_t cap, const char *src)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

static char align_h_of(const char *s)
{
    if (s && (s[0] == 'c' || s[0] == 'C')) return 'C';
    if (s && (s[0] == 'r' || s[0] == 'R')) return 'R';
    return 'L';
}

static char align_v_of(const char *s)
{
    if (s && (s[0] == 'm' || s[0] == 'M')) return 'M';
    if (s && (s[0] == 'b' || s[0] == 'B')) return 'B';
    return 'T';
}

static owm_field_t owm_field_of(const char *s)
{
    if (!s) return OWM_F_TEMP;
    if (strcmp(s, "min") == 0) return OWM_F_MIN;
    if (strcmp(s, "max") == 0) return OWM_F_MAX;
    if (strcmp(s, "wind") == 0) return OWM_F_WIND;
    if (strcmp(s, "humidity") == 0) return OWM_F_HUMIDITY;
    if (strcmp(s, "condition") == 0) return OWM_F_CONDITION;
    if (strcmp(s, "icon") == 0) return OWM_F_ICON;
    if (strcmp(s, "city") == 0) return OWM_F_CITY;
    if (strcmp(s, "time") == 0) return OWM_F_TIME;
    return OWM_F_TEMP;
}

static alert_op_t alert_op_of(const char *s)
{
    if (!s) return ALERT_OP_GT;
    if (strcmp(s, "gte") == 0) return ALERT_OP_GTE;
    if (strcmp(s, "lt") == 0)  return ALERT_OP_LT;
    if (strcmp(s, "lte") == 0) return ALERT_OP_LTE;
    if (strcmp(s, "eq") == 0)  return ALERT_OP_EQ;
    if (strcmp(s, "ne") == 0)  return ALERT_OP_NE;
    return ALERT_OP_GT;
}

/* A level that is absent or unrecognised becomes ALERT_NONE, which alerts_eval() never returns
 * as a firing result — so a rule from a newer schema cannot invent an alarm. */
static alert_level_t alert_level_of(const char *s)
{
    if (!s) return ALERT_NONE;
    if (strcmp(s, "advisory") == 0) return ALERT_ADVISORY;
    if (strcmp(s, "warning") == 0)  return ALERT_WARNING;
    if (strcmp(s, "severe") == 0)   return ALERT_SEVERE;
    return ALERT_NONE;
}

static void parse_binding(cJSON *b, binding_t *out)
{
    out->kind = BIND_NONE;
    out->owm_field = OWM_F_TEMP;
    out->day_index = 0;
    out->entity_id[0] = '\0';
    if (!cJSON_IsObject(b)) return;

    cJSON *k = cJSON_GetObjectItemCaseSensitive(b, "kind");
    if (!cJSON_IsString(k) || !k->valuestring) return;
    const char *ks = k->valuestring;

    if (strcmp(ks, "owm-current") == 0) {
        out->kind = BIND_OWM_CURRENT;
        /* `owmField` is spelled exactly as the schema spells it — see webapp
         * model/config.ts:DataBinding. */
        cJSON *f = cJSON_GetObjectItemCaseSensitive(b, "owmField");
        if (cJSON_IsString(f)) out->owm_field = owm_field_of(f->valuestring);
    } else if (strcmp(ks, "owm-daily") == 0) {
        out->kind = BIND_OWM_DAILY;
        cJSON *f = cJSON_GetObjectItemCaseSensitive(b, "owmField");
        if (cJSON_IsString(f)) out->owm_field = owm_field_of(f->valuestring);
        else out->owm_field = OWM_F_MAX;    /* the schema's own default for a daily binding */
        cJSON *d = cJSON_GetObjectItemCaseSensitive(b, "dayIndex");
        if (cJSON_IsNumber(d) && d->valuedouble >= 0) out->day_index = (int)d->valuedouble;
    } else if (strcmp(ks, "owm-alert") == 0) {
        out->kind = BIND_OWM_ALERT;
    } else if (strcmp(ks, "ha") == 0) {
        out->kind = BIND_HA;
        cJSON *e = cJSON_GetObjectItemCaseSensitive(b, "entityId");
        if (cJSON_IsString(e)) copy_str(out->entity_id, sizeof(out->entity_id), e->valuestring);
    }
    /* An unrecognised kind stays BIND_NONE: the widget renders its fallback rather than
     * guessing at a binding that may mean something different in a newer schema. */
}

static void parse_format(cJSON *f, widget_format_t *out)
{
    out->decimals = -1;
    out->prefix[0] = out->suffix[0] = '\0';
    copy_str(out->fallback, sizeof(out->fallback), "--");   /* matches the firmware's own default */
    if (!cJSON_IsObject(f)) return;

    cJSON *d = cJSON_GetObjectItemCaseSensitive(f, "decimals");
    /* Bounded to 0..6 exactly as the web app's formatValue() bounds it. A negative or absurd
     * decimals value is a serialisation bug, and honouring it would print a nonsensical
     * number — so it falls back to unset. */
    if (cJSON_IsNumber(d)) {
        const int v = (int)d->valuedouble;
        if (v >= 0 && v <= 6) out->decimals = v;
    }
    cJSON *p = cJSON_GetObjectItemCaseSensitive(f, "prefix");
    if (cJSON_IsString(p)) copy_str(out->prefix, sizeof(out->prefix), p->valuestring);
    cJSON *s = cJSON_GetObjectItemCaseSensitive(f, "suffix");
    if (cJSON_IsString(s)) copy_str(out->suffix, sizeof(out->suffix), s->valuestring);
    cJSON *fb = cJSON_GetObjectItemCaseSensitive(f, "fallback");
    /* An explicit "" is meaningful: the alert bar uses it so an absent alert renders as an
     * empty box rather than two dashes across the panel. Only a NON-string is ignored. */
    if (cJSON_IsString(fb)) copy_str(out->fallback, sizeof(out->fallback), fb->valuestring);
}

static int parse_rules(cJSON *arr, layout_widget_t *w)
{
    w->n_rules = 0;
    if (!cJSON_IsArray(arr)) return 0;
    const int n = cJSON_GetArraySize(arr);
    for (int i = 0; i < n && w->n_rules < LAYOUT_MAX_RULES; i++) {
        cJSON *r = cJSON_GetArrayItem(arr, i);
        if (!cJSON_IsObject(r)) continue;
        cJSON *op = cJSON_GetObjectItemCaseSensitive(r, "op");
        cJSON *th = cJSON_GetObjectItemCaseSensitive(r, "threshold");
        cJSON *lv = cJSON_GetObjectItemCaseSensitive(r, "level");
        if (!cJSON_IsNumber(th)) continue;              /* a rule with no number cannot fire */
        alert_rule_t *dst = &w->rules[w->n_rules];
        dst->op = alert_op_of(cJSON_IsString(op) ? op->valuestring : NULL);
        dst->threshold = th->valuedouble;
        dst->level = alert_level_of(cJSON_IsString(lv) ? lv->valuestring : NULL);
        /* A rule whose level resolves to NONE can never fire, so keeping it would only consume
         * one of the six slots this widget has. */
        if (dst->level != ALERT_NONE) w->n_rules++;
    }
    return w->n_rules;
}

static int parse_one(cJSON *o, layout_widget_t *w)
{
    memset(w, 0, sizeof(*w));
    w->role = 'd';
    w->align_h = 'L';
    w->align_v = 'T';
    w->font_id = FONT_BODY;
    parse_format(NULL, &w->format);     /* seed the defaults, then let the document override */

    cJSON *role = cJSON_GetObjectItemCaseSensitive(o, "role");
    if (cJSON_IsString(role) && role->valuestring && role->valuestring[0] == 's') {
        w->role = 's';                  /* baked into the static layer; the device skips it */
    }

    /* The id, for /api/values (FR-27). Absent is legal — the device renders such a widget
     * fine — so it stays empty and the endpoint simply cannot report a value for it. */
    cJSON *id = cJSON_GetObjectItemCaseSensitive(o, "id");
    if (cJSON_IsString(id)) copy_str(w->id, sizeof(w->id), id->valuestring);

    /* Geometry. Absent numbers keep 0 and the widget is then a zero-size box, which the
     * renderer clips to nothing — a silent no-op rather than a scribble across the panel.
     * Coordinates are ints in the schema; a fractional value is truncated, not rounded, so two
     * widgets authored as x=10.4 and x=10.6 cannot overlap after quantisation. */
    cJSON *x = cJSON_GetObjectItemCaseSensitive(o, "x");
    cJSON *y = cJSON_GetObjectItemCaseSensitive(o, "y");
    cJSON *ww = cJSON_GetObjectItemCaseSensitive(o, "w");
    cJSON *hh = cJSON_GetObjectItemCaseSensitive(o, "h");
    if (cJSON_IsNumber(x))  w->x = (int)x->valuedouble;
    if (cJSON_IsNumber(y))  w->y = (int)y->valuedouble;
    if (cJSON_IsNumber(ww)) w->w = (int)ww->valuedouble;
    if (cJSON_IsNumber(hh)) w->h = (int)hh->valuedouble;

    /* Clamp to the panel. The renderer already clips to the box AND to the panel, so this is
     * belt-and-braces — but it keeps a malformed document from producing a field whose
     * intersection with the panel is empty while looking correct in the config. */
    if (w->w < 0) w->w = 0;
    if (w->h < 0) w->h = 0;
    w->x = clamp_int(w->x, 0, WGT_PANEL_W);
    w->y = clamp_int(w->y, 0, WGT_PANEL_H);
    if (w->x + w->w > WGT_PANEL_W) w->w = WGT_PANEL_W - w->x;
    if (w->y + w->h > WGT_PANEL_H) w->h = WGT_PANEL_H - w->y;

    cJSON *font = cJSON_GetObjectItemCaseSensitive(o, "font");
    if (cJSON_IsObject(font)) {
        cJSON *size = cJSON_GetObjectItemCaseSensitive(font, "size");
        if (cJSON_IsNumber(size)) {
            w->font_id = font_face_for_px(size->valuedouble);
        }
        cJSON *ah = cJSON_GetObjectItemCaseSensitive(font, "align");
        if (cJSON_IsString(ah)) w->align_h = align_h_of(ah->valuestring);
        cJSON *av = cJSON_GetObjectItemCaseSensitive(font, "valign");
        if (cJSON_IsString(av)) w->align_v = align_v_of(av->valuestring);
    }

    cJSON *fmt = cJSON_GetObjectItemCaseSensitive(o, "format");
    if (cJSON_IsObject(fmt)) parse_format(fmt, &w->format);

    parse_binding(cJSON_GetObjectItemCaseSensitive(o, "binding"), &w->binding);
    parse_rules(cJSON_GetObjectItemCaseSensitive(o, "alerts"), w);

    /* A dynamic widget with no usable binding has nothing to stamp. It is kept rather than
     * dropped because the caller still wants a value for it (the fallback), and dropping it
     * would shift every later index — the arrays the caller builds are positional. */
    return 0;
}

int layout_widgets_from_root(void *root_v, int page_index,
                             layout_widget_t *out, int cap)
{
    if (!root_v || !out || cap <= 0) return -1;
    if (page_index < 0) return -1;

    cJSON *root = (cJSON *)root_v;
    if (!cJSON_IsObject(root)) return -3;

    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    cJSON *page = cJSON_IsArray(pages) ? cJSON_GetArrayItem(pages, page_index) : NULL;
    if (!cJSON_IsObject(page)) {
        /* No such page. This is NOT an error: a device whose config has one page is asked for
         * page 0, and a caller probing past the end should get "no widgets", not a failure that
         * blanks the panel. */
        return 0;
    }

    cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
    if (!cJSON_IsArray(widgets)) return 0;

    const int n = cJSON_GetArraySize(widgets);
    int used = 0;
    for (int i = 0; i < n && used < cap; i++) {
        cJSON *o = cJSON_GetArrayItem(widgets, i);
        if (!cJSON_IsObject(o)) continue;
        parse_one(o, &out[used]);
        used++;
    }
    return used;
}

int layout_widgets_parse(const char *json, int page_index,
                         layout_widget_t *out, int cap)
{
    if (!json || !out || cap <= 0) return -1;
    if (page_index < 0) return -1;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -2;

    const int r = layout_widgets_from_root(root, page_index, out, cap);
    cJSON_Delete(root);
    return r;
}

void layout_scan_all_pages(void *root_v, int npages,
                           int *need_current, int *need_daily, int *need_alert,
                           int *need_ha, int *max_day,
                           char (*ha_out)[48], int ha_cap, int *ha_n)
{
    if (need_current) *need_current = 0;
    if (need_daily)   *need_daily = 0;
    if (need_alert)   *need_alert = 0;
    if (need_ha)      *need_ha = 0;
    if (max_day)      *max_day = 0;
    if (ha_n)         *ha_n = 0;

    cJSON *root = (cJSON *)root_v;
    if (!cJSON_IsObject(root) || npages <= 0) return;

    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (!cJSON_IsArray(pages)) return;

    for (int p = 0; p < npages; p++) {
        cJSON *page = cJSON_GetArrayItem(pages, p);
        if (!cJSON_IsObject(page)) continue;
        cJSON *widgets = cJSON_GetObjectItemCaseSensitive(page, "widgets");
        if (!cJSON_IsArray(widgets)) continue;

        const int n = cJSON_GetArraySize(widgets);
        for (int i = 0; i < n; i++) {
            cJSON *o = cJSON_GetArrayItem(widgets, i);
            if (!cJSON_IsObject(o)) continue;
            /* A static widget is baked into the bitmap and stamping nothing — it must not make
             * the tick fetch a source no dynamic box reads. Matches value_scan_needs(). */
            cJSON *role = cJSON_GetObjectItemCaseSensitive(o, "role");
            if (cJSON_IsString(role) && role->valuestring && role->valuestring[0] == 's') continue;

            binding_t b;
            parse_binding(cJSON_GetObjectItemCaseSensitive(o, "binding"), &b);
            switch (b.kind) {
            case BIND_OWM_CURRENT: if (need_current) *need_current = 1; break;
            case BIND_OWM_DAILY:
                if (need_daily) *need_daily = 1;
                if (max_day && b.day_index > *max_day) *max_day = b.day_index;
                break;
            case BIND_OWM_ALERT:   if (need_alert) *need_alert = 1; break;
            case BIND_HA:
                if (!b.entity_id[0]) break;
                if (need_ha) *need_ha = 1;
                {
                    /* Deduplicate across EVERY page, not within one: the shipped layout binds the
                     * same hallway entity on two pages, and requesting it twice would inflate the
                     * template line and burn the bounded response budget for nothing. The order is
                     * first-seen, which is what the template and the slot lookup must agree on. */
                    const int have = ha_n ? *ha_n : 0;
                    int seen = 0;
                    for (int k = 0; k < have; k++) {
                        if (ha_out && strcmp(ha_out[k], b.entity_id) == 0) { seen = 1; break; }
                    }
                    if (!seen && ha_out && ha_n && have < ha_cap) {
                        copy_str(ha_out[have], 48, b.entity_id);
                        (*ha_n)++;
                    }
                }
                break;
            default: break;
            }
        }
    }
}
