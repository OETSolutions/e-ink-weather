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

/* -------------------------------------------------------------- which product (FR-6/FR-7) -- */

owm_product_t owm_product_from_string(const char *s)
{
    if (!s) return OWM_PRODUCT_AUTO;
    if (strcmp(s, "onecall3") == 0) return OWM_PRODUCT_ONECALL3;
    if (strcmp(s, "legacy") == 0)   return OWM_PRODUCT_LEGACY;
    /* "auto", "", and anything unrecognised all probe. An unknown value must not silently pin
     * the device to a product the user did not choose — the same rule power_mode_from_string()
     * follows. */
    return OWM_PRODUCT_AUTO;
}

int owm_product_has_alerts(owm_product_t p)
{
    /* Only One Call 3.0 carries the "alerts" array. AUTO cannot answer: it has not probed, so
     * claiming either way would be a guess presented as a fact. */
    return p == OWM_PRODUCT_ONECALL3;
}

owm_product_t owm_product_resolve(owm_product_t configured, int onecall_available)
{
    /* An explicit setting is the user telling us which product their key carries, so it wins
     * outright — including 'onecall3' on a key the probe would reject, because that is then a
     * misconfiguration the user needs to see fail rather than a silent downgrade to the free
     * tier they did not ask for. */
    if (configured == OWM_PRODUCT_ONECALL3 || configured == OWM_PRODUCT_LEGACY) return configured;

    /* AUTO. Only a definite "yes" from the probe selects One Call; an unresolved probe (-1)
     * falls through to the free pair, because a request has to go somewhere and 2.5 is the
     * product that answers for a key without the subscription. */
    return (onecall_available == 1) ? OWM_PRODUCT_ONECALL3 : OWM_PRODUCT_LEGACY;
}

unsigned owm_forecast_buf_bytes(int blocks)
{
    if (blocks <= 0) return 0;
    if (blocks > OWM_FORECAST_MAX_BLOCKS) blocks = OWM_FORECAST_MAX_BLOCKS;
    return (unsigned)OWM_FORECAST_BASE_BYTES +
           (unsigned)blocks * (unsigned)OWM_FORECAST_BYTES_PER_BLOCK;
}

/* The field codes from lib/layout/include/widgets.h. Duplicated as literals rather than
 * including that header: lib/datasrc is a lower layer than lib/layout and must not depend on
 * it, and the values are part of this function's contract either way. */
#define OWM_F_TEMP_      0
#define OWM_F_MIN_       1
#define OWM_F_MAX_       2
#define OWM_F_WIND_      3
#define OWM_F_HUMIDITY_  4
#define OWM_F_CONDITION_ 5
#define OWM_F_ICON_      6
#define OWM_F_CITY_      7

/* Copy `src` into out.text as a TEXT result. A string too long for the 64-byte field is
 * TRUNCATED rather than rejected: a long place name or condition is still worth showing, and
 * the renderer clips it to the widget box anyway. Returns the value unchanged. */
static datasrc_value_t text_result(datasrc_value_t out, const char *src, long now)
{
    if (!src || !*src) { out.status = DATASRC_ERR_NOT_FOUND; return out; }
    strncpy(out.text, src, sizeof(out.text) - 1);
    out.text[sizeof(out.text) - 1] = '\0';
    out.status = DATASRC_OK;
    out.is_numeric = 0;
    out.value = 0.0;
    out.observed_at = now;
    return out;
}

datasrc_value_t owm_parse_current_field(const char *json, int field, long now_unix)
{
    datasrc_value_t out; memset(&out, 0, sizeof(out));
    out.status = DATASRC_ERR_PARSE; out.is_numeric = 1;
    cJSON *root = root_or_null(json, &out);
    if (!root) return out;

    cJSON *cur = obj_item(root, "current");
    cJSON *main, *wind, *weather;
    if (cur) {
        /* One Call 3.0: everything is nested under "current". */
        main    = cur;
        wind    = cur;
        weather = cJSON_GetArrayItem(obj_item(cur, "weather"), 0);
    } else {
        /* Current Weather 2.5: "main" and "wind" are TOP-LEVEL siblings. 2.5/forecast has
         * neither (its numbers live under "list"), so every branch below reports NOT_FOUND for
         * it — which is correct, because a 3-hour block is not a current reading. */
        main    = obj_item(root, "main");
        wind    = obj_item(root, "wind");
        weather = cJSON_GetArrayItem(obj_item(root, "weather"), 0);
    }

    cJSON *dt = obj_item(cur ? cur : root, "dt");
    const long obs = cJSON_IsNumber(dt) ? (long)dt->valuedouble : now_unix;

    if (field == OWM_F_CONDITION_) {
        /* "description" is the human words ("scattered clouds"); "main" is the coarse class
         * ("Clouds"). A widget showing conditions wants the words. */
        cJSON *d = obj_item(weather, "description");
        out = text_result(out, cJSON_IsString(d) ? d->valuestring : NULL, obs);
        cJSON_Delete(root);
        return out;
    }
    if (field == OWM_F_ICON_) {
        cJSON *i = obj_item(weather, "icon");
        out = text_result(out, cJSON_IsString(i) ? i->valuestring : NULL, obs);
        cJSON_Delete(root);
        return out;
    }
    if (field == OWM_F_CITY_) {
        /* The place name OWM resolved the request's coordinates to (FR-17's "location/zip
         * display"). It is a TOP-LEVEL "name" on 2.5/weather — not under "main" or "sys" — and
         * One Call 3.0 does not carry it at all, so a One Call response reports NOT_FOUND and
         * the widget shows its fallback. That is correct rather than a bug: there is no name to
         * show, and inventing one (from the zip, say) would be a different, stale value. */
        cJSON *n = obj_item(root, "name");
        out = text_result(out, cJSON_IsString(n) ? n->valuestring : NULL, obs);
        cJSON_Delete(root);
        return out;
    }

    /* Numeric fields. `min`/`max` on the CURRENT reading are today's reported extremes
     * (2.5/weather's main.temp_min/temp_max); a daily forecast extreme comes from
     * owm_parse_daily_* instead, which is a genuinely different number. */
    cJSON *v = NULL;
    switch (field) {
        case OWM_F_TEMP_:     v = obj_item(main, "temp");     break;
        case OWM_F_MIN_:      v = obj_item(main, "temp_min"); break;
        case OWM_F_MAX_:      v = obj_item(main, "temp_max"); break;
        case OWM_F_HUMIDITY_: v = obj_item(main, "humidity"); break;
        case OWM_F_WIND_:     v = obj_item(wind, "speed");    break;
        default:              break;
    }

    if (cJSON_IsNumber(v)) {
        out.status = DATASRC_OK;
        out.value = v->valuedouble;
        out.is_numeric = 1;
        if (field == OWM_F_HUMIDITY_) out.value = v->valuedouble;   /* already 0..100 */
        out.observed_at = obs;
    } else {
        /* Right product, field absent — distinct from a garbled payload, so a caller can tell
         * a wrong-endpoint request from a network failure (see datasrc.h). */
        out.status = DATASRC_ERR_NOT_FOUND;
    }
    cJSON_Delete(root);
    return out;
}
