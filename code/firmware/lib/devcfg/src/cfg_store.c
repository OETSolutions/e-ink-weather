#include "cfg_store.h"
#include "devcfg.h"
#include "layout_model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* cJSON is available on both the host env and the device (lib_deps / component). */
#include "cJSON.h"

#define CFG_KEY "config"

/* FR-11: full refresh after 5 partials. Kept in sync with the fallbacks in
 * api_partial_limit() and app_boot.c — three copies of a default is two too many, but they
 * must at least agree. */
static const char *DEFAULT_JSON =
    "{\"schemaVersion\":1,\"updateSeconds\":900,\"partialRefreshLimit\":5,"
    "\"pages\":[{\"name\":\"default\",\"refreshSeconds\":900,\"weight\":1}]}";

const char *cfg_store_default_json(void) { return DEFAULT_JSON; }

/* Pull a numeric schemaVersion out. A document without one cannot be migrated, so it is
 * refused rather than assumed to be the current version — guessing would silently accept a
 * future document as if it were v1 and store a layout the firmware cannot honour. */
static int read_schema_version(const char *json, int *out)
{
    cJSON *root = cJSON_Parse(json);
    if (!root) return -1;
    cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "schemaVersion");
    int rc = -1;
    if (cJSON_IsNumber(v) && v->valuedouble == (double)(int)v->valuedouble) {
        *out = (int)v->valuedouble;
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

int cfg_store_put(const cfg_store_t *store, const char *json)
{
    if (!store || !store->write || !json) return -1;

    /* 1. must be JSON with a numeric schemaVersion */
    int ver = 0;
    if (read_schema_version(json, &ver) != 0) return -2;

    /* 2. migrate to current. devcfg_migrate returns a malloc'd string; a version it does
     *    not know is a refusal, not a pass-through. */
    char *migrated = NULL;
    if (devcfg_migrate(ver, DEVCFG_SCHEMA_VERSION, json, &migrated) != 0) return -3;
    if (!migrated) return -3;

    /* 3. the layout parser is the real gate: if the firmware cannot act on this document,
     *    storing it would brick the next boot. */
    layout_config_t cfg;
    if (layout_config_parse(migrated, &cfg) != 0) {
        free(migrated);
        return -4;
    }

    /* Only now is anything persisted. */
    int rc = store->write(store->ctx, CFG_KEY, migrated, strlen(migrated) + 1);
    free(migrated);
    return rc == 0 ? 0 : -5;
}

int cfg_store_get(const cfg_store_t *store, char **out_json)
{
    if (!store || !store->read || !out_json) return -1;
    *out_json = NULL;

    size_t max = 4096;
    char *buf = malloc(max);
    if (!buf) return -1;
    size_t len = 0;
    if (store->read(store->ctx, CFG_KEY, buf, max, &len) != 0) {
        /* Nothing stored yet: hand back the default so a fresh device boots configured. */
        free(buf);
        const char *d = cfg_store_default_json();
        char *copy = malloc(strlen(d) + 1);
        if (!copy) return -1;
        memcpy(copy, d, strlen(d) + 1);
        *out_json = copy;
        return 0;
    }

    /* A stored document that no longer parses means the invariant was violated at write
     * time (or flash rotted). Return it anyway — the caller can report the error — but do
     * NOT silently substitute the default, which would hide the fault. */
    *out_json = buf;
    return 0;
}
