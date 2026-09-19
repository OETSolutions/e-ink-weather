#include "owm.h"
#include "cJSON.h"
#include <string.h>

/* Distinguish "this response does not contain that field" from "this response is not
 * JSON". Both used to collapse into ERR_PARSE, which meant a caller could not tell a
 * wrong-product request (a programming error) from a garbled payload (a network error)
 * and would retry the wrong one. */
static cJSON *root_or_null(const char *json, datasrc_value_t *out)
{
    cJSON *r = cJSON_Parse(json);
    if (!r) {
        out->status = DATASRC_ERR_PARSE;
        return NULL;
    }
    return r;
}

static cJSON *obj_item(cJSON *o, const char *k)
{
    return o ? cJSON_GetObjectItemCaseSensitive(o, k) : NULL;
}

datasrc_value_t owm_parse_current_temp(const char *json, long now_unix)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;
    cJSON *root = root_or_null(json, &out);
    if (!root) return out;

    cJSON *t = NULL, *dt = NULL;
    cJSON *cur = obj_item(root, "current");
    if (cur) {
        /* One Call 3.0. */
        t  = obj_item(cur, "temp");
        dt = obj_item(cur, "dt");
    } else {
        /* Current Weather 2.5: top-level "main"."temp" and top-level "dt". Only take this
         * branch for the CURRENT-WEATHER shape. 2.5/forecast also has neither "current"
         * nor a top-level "main" (its numbers live under "list"), so it falls through and
         * reports NOT_FOUND — which is right, because a forecast block is not a current
         * reading. An earlier draft read "list"[0]."main"."temp" here and would have
         * reported a 3-hour forecast value as the current temperature. */
        cJSON *main = obj_item(root, "main");
        t = obj_item(main, "temp");
        if (cJSON_IsNumber(t)) dt = obj_item(root, "dt");
    }

    if (cJSON_IsNumber(t)) {
        out.status = DATASRC_OK;
        out.value = t->valuedouble;
        out.observed_at = cJSON_IsNumber(dt) ? (long)dt->valuedouble : now_unix;
    } else {
        out.status = DATASRC_ERR_NOT_FOUND;
    }
    cJSON_Delete(root);
    return out;
}

/* Free-tier 2.5 forecast: group the 3-hour "list" into LOCAL calendar days.
 *
 * Live data note: on a real response 37 of 40 blocks report temp_min == temp_max, so a
 * naive "reduce over every block" would still be correct but would look like it was
 * ignoring most of the input. It is not — those blocks genuinely carry no spread. */
static datasrc_value_t forecast25_field(cJSON *root, int day_index,
                                        const char *field, long now)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;

    cJSON *list = obj_item(root, "list");
    if (!cJSON_IsArray(list)) return out;

    /* A local day is (dt + tz) / 86400 — integer division, the same expression in both
     * passes, so the grouping cannot disagree with itself. */
    long tz = 0;
    cJSON *z = obj_item(obj_item(root, "city"), "timezone");
    if (cJSON_IsNumber(z)) tz = (long)z->valuedouble;

    /* Pass 1: the distinct local days, in time order (the list is already sorted). */
    long days[64]; int ndays = 0;
    int n = cJSON_GetArraySize(list);
    for (int i = 0; i < n && ndays < 64; i++) {
        cJSON *dt = obj_item(cJSON_GetArrayItem(list, i), "dt");
        if (!cJSON_IsNumber(dt)) continue;
        long day = ((long)dt->valuedouble + tz) / 86400;
        if (ndays == 0 || days[ndays - 1] != day) days[ndays++] = day;
    }
    if (day_index < 0 || day_index >= ndays) {
        out.status = DATASRC_ERR_NOT_FOUND;   /* index outside this response's horizon */
        return out;
    }
    long target = days[day_index];

    /* Pass 2: reduce the field across every block in that local day. */
    int found = 0; double acc = 0;
    int want_min = (strcmp(field, "temp_min") == 0);
    for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(list, i);
        cJSON *dt = obj_item(it, "dt");
        if (!cJSON_IsNumber(dt)) continue;
        if (((long)dt->valuedouble + tz) / 86400 != target) continue;
        cJSON *f = obj_item(obj_item(it, "main"), field);
        if (!cJSON_IsNumber(f)) continue;
        if (!found) { acc = f->valuedouble; found = 1; }
        else if (want_min) { if (f->valuedouble < acc) acc = f->valuedouble; }
        else               { if (f->valuedouble > acc) acc = f->valuedouble; }
    }
    if (found) { out.status = DATASRC_OK; out.value = acc; out.observed_at = now; }
    else       { out.status = DATASRC_ERR_NOT_FOUND; }
    return out;
}

static datasrc_value_t daily_field(const char *json, int idx, const char *field, long now)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;
    cJSON *root = root_or_null(json, &out);
    if (!root) return out;

    /* One Call 3.0: "daily"[i]."temp"."min"/"max". */
    cJSON *day  = cJSON_GetArrayItem(obj_item(root, "daily"), idx);
    cJSON *f    = obj_item(obj_item(day, "temp"), field);
    if (cJSON_IsNumber(f)) {
        out.status = DATASRC_OK; out.value = f->valuedouble; out.observed_at = now;
    } else if (cJSON_IsArray(obj_item(root, "list"))) {
        /* Free 2.5 forecast: no "daily" array; derive it from "list". */
        out = forecast25_field(root, idx,
                               (strcmp(field, "min") == 0) ? "temp_min" : "temp_max", now);
    } else if (cJSON_IsArray(obj_item(root, "daily"))) {
        out.status = DATASRC_ERR_NOT_FOUND;   /* right product, index out of range */
    } else {
        out.status = DATASRC_ERR_NOT_FOUND;   /* neither shape present */
    }
    cJSON_Delete(root);
    return out;
}

datasrc_value_t owm_parse_daily_min(const char *json, int i, long now)
{
    return daily_field(json, i, "min", now);
}

datasrc_value_t owm_parse_daily_max(const char *json, int i, long now)
{
    return daily_field(json, i, "max", now);
}

int owm_has_alerts(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return 0;
    cJSON *a = obj_item(root, "alerts");
    int has = cJSON_IsArray(a) && cJSON_GetArraySize(a) > 0;
    cJSON_Delete(root);
    return has;
}
