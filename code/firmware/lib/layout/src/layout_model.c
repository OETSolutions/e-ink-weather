#include "layout_model.h"
#include "cJSON.h"
#include "power.h"
#include <stdio.h>
#include <string.h>

/* The web app's document is much richer than this (widgets, bindings, alerts). Only the
 * scheduling subset is read here; everything else is the renderer's business (Task 13+).
 *
 * Design rule for every field: a value that is present but OUT OF RANGE falls back to the
 * default, exactly as an absent one does. Half-honouring a nonsensical value (truncating
 * it, or storing it and hoping) is how a config typo becomes a device that refreshes
 * continuously or never. */

static int clamp_interval(int v, int fallback)
{
    if (v < LAYOUT_MIN_INTERVAL_SECONDS) return fallback;
    if (v > LAYOUT_MAX_INTERVAL_SECONDS) return LAYOUT_MAX_INTERVAL_SECONDS;
    return v;
}

int layout_config_parse(const char *json, layout_config_t *out)
{
    if (!json || !out) return -1;
    memset(out, 0, sizeof(*out));

    out->schema_version = LAYOUT_SCHEMA_VERSION;
    out->update_seconds = 900;          /* 15 min default */
    out->partial_refresh_limit = 5;     /* vendor demo's rule of thumb (FR-11) */
    out->page_count = 1;
    out->power_mode = POWER_MODE_AUTO;  /* trust the inference unless told otherwise */
    strcpy(out->pages[0].name, "Main");
    out->pages[0].refresh_seconds = 900;
    out->pages[0].weight = 1;

    cJSON *root = cJSON_Parse(json);
    if (!root) return -2;
    if (!cJSON_IsObject(root)) {        /* a bare array or scalar is not a config */
        cJSON_Delete(root);
        return -3;
    }

    /* FR-26b: refuse a newer schema instead of guessing at it. An ABSENT version is
     * treated as current — every pre-versioning document is v1 by definition. */
    cJSON *sv = cJSON_GetObjectItemCaseSensitive(root, "schemaVersion");
    if (cJSON_IsNumber(sv)) {
        int v = (int)sv->valuedouble;
        if (v > LAYOUT_SCHEMA_VERSION) {
            cJSON_Delete(root);
            return -4;
        }
        out->schema_version = v;
    } else if (sv != NULL && !cJSON_IsNull(sv)) {
        /* Present but not a number (e.g. "2", or an object) — we cannot tell whether it
         * means a future version, so we must not assume it does not. */
        cJSON_Delete(root);
        return -4;
    }

    cJSON *us = cJSON_GetObjectItemCaseSensitive(root, "updateSeconds");
    if (cJSON_IsNumber(us)) {
        int v = (int)us->valuedouble;
        out->update_seconds = clamp_interval(v, out->update_seconds);
    }

    cJSON *pr = cJSON_GetObjectItemCaseSensitive(root, "partialRefreshLimit");
    if (cJSON_IsNumber(pr)) {
        int v = (int)pr->valuedouble;
        if (v >= 1) {
            out->partial_refresh_limit = v > LAYOUT_MAX_PARTIAL_LIMIT
                                       ? LAYOUT_MAX_PARTIAL_LIMIT : v;
        }
    }

    /* The FR-8 power override. A non-string value falls back to 'auto' — power_mode_from_string
     * already treats "" and unknown text as auto, so this only has to guard the type. */
    cJSON *pm = cJSON_GetObjectItemCaseSensitive(root, "powerMode");
    if (cJSON_IsString(pm) && pm->valuestring) {
        out->power_mode = power_mode_from_string(pm->valuestring);
    }

    cJSON *pages = cJSON_GetObjectItemCaseSensitive(root, "pages");
    if (pages && !cJSON_IsArray(pages)) {
        /* Present but the wrong type is a SERIALIZATION BUG, not a user choice — e.g. the
         * web app sent the pages object where the schema says array. Falling through to the
         * default would silently discard the user's whole layout and show a generic "Main"
         * page with no error anywhere. An ABSENT `pages` is different and stays lenient: a
         * minimal document is documented as valid, so the default applies. */
        cJSON_Delete(root);
        return -1;
    }
    if (cJSON_IsArray(pages)) {
        int n = cJSON_GetArraySize(pages);
        if (n > LAYOUT_MAX_PAGES) n = LAYOUT_MAX_PAGES;
        int used = 0;
        for (int i = 0; i < n; i++) {
            cJSON *p = cJSON_GetArrayItem(pages, i);
            if (!cJSON_IsObject(p)) continue;
            layout_page_t *dst = &out->pages[used];
            memset(dst, 0, sizeof(*dst));

            cJSON *nm = cJSON_GetObjectItemCaseSensitive(p, "name");
            if (cJSON_IsString(nm) && nm->valuestring) {
                strncpy(dst->name, nm->valuestring, sizeof(dst->name) - 1);
                dst->name[sizeof(dst->name) - 1] = '\0';
            }
            if (dst->name[0] == '\0') {
                /* A nameless page would render as a blank header; give it something the
                 * user can recognise instead. */
                snprintf(dst->name, sizeof(dst->name), "Page %d", used + 1);
            }

            cJSON *rs = cJSON_GetObjectItemCaseSensitive(p, "refreshSeconds");
            dst->refresh_seconds = (cJSON_IsNumber(rs))
                                 ? clamp_interval((int)rs->valuedouble, out->update_seconds)
                                 : out->update_seconds;

            cJSON *w = cJSON_GetObjectItemCaseSensitive(p, "weight");
            dst->weight = (cJSON_IsNumber(w) && w->valuedouble >= 1) ? (int)w->valuedouble : 1;
            used++;
        }
        /* An empty (or entirely junk) array keeps the default page rather than leaving
         * page_count = 0, which would make layout_page_at meaningless. */
        if (used > 0) out->page_count = used;
    }

    cJSON_Delete(root);
    return 0;
}

int layout_page_at(const layout_config_t *cfg, long elapsed_seconds)
{
    if (!cfg || cfg->page_count <= 0) return 0;
    if (cfg->page_count == 1) return 0;
    if (elapsed_seconds < 0) elapsed_seconds = 0;

    long total = 0;
    for (int i = 0; i < cfg->page_count; i++) {
        total += cfg->pages[i].refresh_seconds;
    }
    if (total <= 0) return 0;        /* a zero total would divide by zero below */

    long pos = elapsed_seconds % total;
    long acc = 0;
    for (int i = 0; i < cfg->page_count; i++) {
        acc += cfg->pages[i].refresh_seconds;
        if (pos < acc) return i;
    }
    return cfg->page_count - 1;
}
