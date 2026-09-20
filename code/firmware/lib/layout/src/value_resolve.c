#include "value_resolve.h"
#include "alerts.h"
#include "ha.h"
#include "owm.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

int value_scan_needs(const layout_widget_t *w, int n, value_needs_t *out)
{
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!w || n <= 0) return 0;

    for (int i = 0; i < n; i++) {
        if (w[i].role != 'd') continue;     /* static widgets are baked into the bitmap */
        switch (w[i].binding.kind) {
            case BIND_OWM_CURRENT: out->need_owm_current = 1; break;
            case BIND_OWM_DAILY:
                out->need_owm_daily = 1;
                if (w[i].binding.day_index > out->max_day_index) {
                    out->max_day_index = w[i].binding.day_index;
                }
                break;
            case BIND_OWM_ALERT:   out->need_owm_alert = 1; break;
            case BIND_HA:          if (w[i].binding.entity_id[0]) out->need_ha = 1; break;
            default: break;
        }
    }
    return 0;
}

int value_collect_ha_entities(const layout_widget_t *w, int n,
                              char (*out)[48], int cap, int *needed)
{
    int used = 0, want = 0;
    if (needed) *needed = 0;
    if (!w || n <= 0 || !out || cap <= 0) return 0;

    for (int i = 0; i < n; i++) {
        if (w[i].role != 'd' || w[i].binding.kind != BIND_HA) continue;
        const char *id = w[i].binding.entity_id;
        if (!id[0]) continue;
        want++;

        /* DEDUPLICATE. The default layout binds the same hallway entity on two pages, and a
         * page may legitimately show one entity in two places. Requesting it twice would
         * return the value twice — harmless — but it inflates the template line and burns the
         * 512-byte response budget for nothing. */
        int seen = 0;
        for (int k = 0; k < used; k++) {
            if (strcmp(out[k], id) == 0) { seen = 1; break; }
        }
        if (seen) continue;
        if (used < cap) {
            strncpy(out[used], id, 47);
            out[used][47] = '\0';
            used++;
        }
    }
    if (needed) *needed = want;
    return used;
}

/* Which slot of the HA response this widget's entity occupies. It is the order the entity was
 * ADDED to the template, so the answer is an identity lookup in `ids`, not a hash. */
static int ha_slot_of(const layout_widget_t *w, char (*ids)[48], int n_ids)
{
    for (int i = 0; i < n_ids; i++) {
        if (strcmp(ids[i], w->binding.entity_id) == 0) return i;
    }
    return -1;
}

/* Resolve a widget's binding to a datasrc value. A widget whose source is missing (not fetched,
 * or the fetch failed) becomes UNAVAILABLE, never zero. */
static datasrc_value_t resolve_value(const layout_widget_t *w, const value_sources_t *src,
                                     char (*ids)[48], int n_ids, long now_unix)
{
    datasrc_value_t v;
    memset(&v, 0, sizeof(v));
    v.status = DATASRC_ERR_UNAVAILABLE;
    v.is_numeric = 1;

    switch (w->binding.kind) {
        case BIND_OWM_CURRENT:
            if (!src->owm_current) return v;
            return owm_parse_current_field(src->owm_current, (int)w->binding.owm_field, now_unix);

        case BIND_OWM_DAILY:
            if (!src->owm_daily) return v;
            /* Only min/max have a daily meaning. A widget bound to, say, daily humidity is a
             * modelling mistake by the app, not something to guess at: an invented number on
             * the glass is worse than the fallback. */
            if (w->binding.owm_field == OWM_F_MIN) {
                return owm_parse_daily_min(src->owm_daily, w->binding.day_index, now_unix);
            }
            if (w->binding.owm_field == OWM_F_MAX) {
                return owm_parse_daily_max(src->owm_daily, w->binding.day_index, now_unix);
            }
            v.status = DATASRC_ERR_NOT_FOUND;
            return v;

        case BIND_OWM_ALERT: {
            if (!src->owm_daily && !src->owm_current) return v;
            /* The alert mechanism looks across BOTH documents: with One Call 3.0 the alerts
             * ride along with the forecast, while on the free tier neither has any. */
            const char *docs[2] = { src->owm_daily, src->owm_current };
            for (int i = 0; i < 2; i++) {
                if (docs[i] && owm_has_alerts(docs[i])) {
                    v.status = DATASRC_OK;
                    v.is_numeric = 0;
                    snprintf(v.text, sizeof(v.text), "WEATHER ALERT");
                    v.observed_at = now_unix;
                    return v;
                }
            }
            /* No alerts is a legitimate reading of "quiet weather", not a failure — but the
             * widget's own fallback (the alert bar ships with "") is what should show, so this
             * stays UNAVAILABLE and the caller prints the fallback. */
            v.status = DATASRC_ERR_UNAVAILABLE;
            return v;
        }

        case BIND_HA: {
            if (!src->ha_line) return v;
            const int slot = ha_slot_of(w, ids, n_ids);
            if (slot < 0) return v;
            /* Parse the WHOLE line, then take THIS widget's token. Passing slot+1 as the count
             * asks for only the prefix, but ha_parse_template_line() writes token i into
             * out[i] — so reading out[0] would return the FIRST entity's value for every HA
             * widget on the page. The arrays must be indexed by the slot, not by 0. */
            double vals[LAYOUT_MAX_FIELDS];
            datasrc_status_t sts[LAYOUT_MAX_FIELDS];
            const int got = ha_parse_template_line(src->ha_line, vals, sts, slot + 1);
            if (got < slot + 1) return v;      /* the response was shorter than this widget's slot */
            v.status = sts[slot];
            v.value = vals[slot];
            v.is_numeric = 1;
            v.observed_at = now_unix;
            return v;
        }

        default:
            v.status = DATASRC_ERR_UNAVAILABLE;
            return v;
    }
}

alert_level_t value_widget_alert_level(const layout_widget_t *w, const datasrc_value_t *v)
{
    if (!w || w->n_rules <= 0) return ALERT_NONE;
    /* A text reading (a condition word) has no magnitude to compare, and a non-finite value is
     * exactly what an unavailable sensor produces. alerts_eval_all() returns NONE for the
     * latter already; the text case is checked here because the numeric field it would compare
     * is 0.0 for those, which is a real value a "below 32" rule would fire on. */
    if (!v || v->status != DATASRC_OK || !v->is_numeric) return ALERT_NONE;
    return alerts_eval_all(w->rules, w->n_rules, v->value);
}

int value_format_widget(const layout_widget_t *w, const value_sources_t *src,
                        char (*ids)[48], int n_ids, long now_unix,
                        datasrc_value_t *out_value, char *buf, size_t cap)
{
    if (!w || !buf || cap == 0) return 0;
    buf[0] = '\0';

    datasrc_value_t v = resolve_value(w, src, ids, n_ids, now_unix);
    if (out_value) *out_value = v;

    /* A FIRING ALERT REPLACES THE READING, and this is checked BEFORE the status guard: an alert
     * is by definition a statement about a value, so it can only fire when there is one, and
     * putting it after the fallback branch would make it unreachable. The alert word is what the
     * user needs to see — the number is still on the glass in the widget that raised it. */
    if (value_widget_alert_level(w, &v) != ALERT_NONE) {
        snprintf(buf, cap, "%s", alerts_level_name(value_widget_alert_level(w, &v)));
        return 1;
    }

    if (v.status != DATASRC_OK) {
        /* The widget's OWN fallback, which is why it is carried in the config: the alert bar
         * ships with "" so quiet weather leaves an empty bar rather than two dashes spanning
         * the panel. parse_format() seeds "--" as the default, so this is never unset. */
        snprintf(buf, cap, "%s", w->format.fallback);
        return 0;
    }

    if (!v.is_numeric) {
        /* A text reading (conditions). No decimals apply, and the affixes still do, so a
         * widget could render "scattered clouds" unchanged. */
        snprintf(buf, cap, "%s%s%s", w->format.prefix, v.text, w->format.suffix);
        return 1;
    }

    if (!isfinite(v.value)) {
        snprintf(buf, cap, "%s", w->format.fallback);
        return 0;
    }

    /* Decimals default to 1 (matching the web app and fmt_temp's "%.1f"): the hero numbers are
     * temperatures, and a whole-degree reading loses the tenth that makes it feel live. */
    const int decimals = (w->format.decimals >= 0 && w->format.decimals <= 6) ? w->format.decimals : 1;
    char body[64];
    snprintf(body, sizeof(body), "%.*f", decimals, v.value);
    snprintf(buf, cap, "%s%s%s", w->format.prefix, body, w->format.suffix);
    return 1;
}
