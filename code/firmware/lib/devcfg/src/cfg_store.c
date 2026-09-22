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
 * future document as if it were v1 and store a layout the firmware cannot honour.
 *
 * THIS SCANS THE TEXT; IT DOES NOT BUILD A cJSON TREE. It used to call cJSON_Parse, which
 * allocates a node for every token in the document — roughly twice the document's size — and
 * then layout_config_parse() immediately built the same tree AGAIN. On this part the largest
 * free block runs 11-17 KB and total free DRAM ~35 KB, so a full-size (8 KB, 24-widget) config
 * exhausted the heap inside the FIRST parse: cJSON_Parse returned NULL, this function returned
 * -1, and the PUT was rejected with "invalid config: needs numeric schemaVersion and a valid
 * layout" — a message about the DOCUMENT for what was a memory failure. Reading one integer
 * from an 8 KB string must not cost a whole parse. */
static int read_schema_version(const char *json, int *out)
{
    const char *k = strstr(json, "\"schemaVersion\"");
    if (!k) return -1;

    /* MUST BE A KEY, not a string VALUE that happens to read "schemaVersion" (a widget id or a
     * label could contain that text). A key is preceded by `{` or `,` — ignoring whitespace —
     * and followed by `:`; a value is preceded by `:` and followed by a quote or comma. */
    const char *p = k;
    while (p > json && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) p--;
    if (p == json || (p[-1] != '{' && p[-1] != ',')) return -1;

    k += strlen("\"schemaVersion\"");
    while (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r') k++;
    if (*k != ':') return -1;
    k++;
    while (*k == ' ' || *k == '\t' || *k == '\n' || *k == '\r') k++;

    if (*k < '0' || *k > '9') return -1;
    /* Bounded so an absurd run of digits cannot overflow the accumulator. A version larger than
     * the current one is refused by the migrate check anyway, so saturating is safe. */
    long v = 0;
    int digits = 0;
    while (*k >= '0' && *k <= '9') {
        if (v < 1000000) v = v * 10 + (*k - '0');
        k++;
        if (++digits > 9) break;
    }
    if (digits == 0) return -1;
    /* MUST BE AN INTEGER. read_schema_version's contract (and its tests) refuse a fractional
     * version like 1.5 — treating it as 1 would migrate a document whose actual version is
     * unknown. A decimal point or exponent after the digits is a refusal. */
    if (*k == '.' || *k == 'e' || *k == 'E') return -1;
    *out = (int)v;
    return 0;
}

int cfg_store_put(const cfg_store_t *store, const char *json)
{
    if (!store || !store->write || !json) return -1;

    /* 1. must be JSON with a numeric schemaVersion */
    int ver = 0;
    if (read_schema_version(json, &ver) != 0) return -2;

    /* 2. migrate to current — BUT ONLY WHEN A MIGRATION IS ACTUALLY NEEDED.
     *
     * devcfg_migrate duplicates the whole document. On this part that is a SECOND contiguous
     * allocation the size of the config, on top of the body buffer the caller already holds, and
     * at steady state the largest free DRAM block is only ~11 KB (the httpd and app task stacks
     * permanently split the one big region — see the render notes). So a config that fits in the
     * body buffer could still fail to duplicate, and the PUT was then rejected with
     * "invalid config: needs numeric schemaVersion and a valid layout" — a message that points at
     * the DOCUMENT while the real cause is memory. Observed on hardware: an 8 KB, 24-widget
     * config was refused intermittently while a 5 KB one saved fine.
     *
     * A v1 document needs no rewriting, so nothing is copied for it; the duplicate is made only
     * when migrate() will genuinely produce a different document. */
    const char *doc = json;
    char *migrated = NULL;
    if (ver != DEVCFG_SCHEMA_VERSION) {
        if (devcfg_migrate(ver, DEVCFG_SCHEMA_VERSION, json, &migrated) != 0) return -3;
        if (!migrated) return -3;
        doc = migrated;
    }

    /* 3. the layout parser is the real gate: if the firmware cannot act on this document,
     *    storing it would brick the next boot. */
    layout_config_t cfg;
    if (layout_config_parse(doc, &cfg) != 0) {
        free(migrated);
        return -4;
    }

    /* Only now is anything persisted. */
    int rc = store->write(store->ctx, CFG_KEY, doc, strlen(doc) + 1);
    free(migrated);
    return rc == 0 ? 0 : -5;
}

int cfg_store_get(const cfg_store_t *store, char **out_json)
{
    if (!store || !store->read || !out_json) return -1;
    *out_json = NULL;

    /* Sized from the SAME constant the API accepts, rather than a separate literal.
     *
     * WHY THIS MATTERS: this was 4096 while the API accepts up to API_CONFIG_MAX_LEN (16,384),
     * and the shipped default layout is 4,424 bytes. A document larger than the read buffer is
     * not truncated by NVS — nvs_get_blob returns ESP_ERR_NVS_INVALID_LENGTH, nvs_read() turns
     * that into -1, and cfg_store_get() then SILENTLY substituted the built-in default. So a
     * user whose layout exceeded the limit would see their config saved, then watch the device
     * revert to a minimal default with no error anywhere. The two ends of the same document must
     * be the same size. */
    /* SIZE THE ALLOCATION FROM THE STORED DOCUMENT, NOT FROM THE MAXIMUM.
     *
     * This allocated CFG_JSON_MAX_LEN (16,384) for every load, and the shipped default layout is
     * ~4,424 bytes — so every read asked for ~3.5x what it needed. On this part that is not
     * merely wasteful: the largest free block under HTTP load sits at 13-17 KB, so a 16 KB
     * request FAILS there, and cfg_store_get() is on the path of GET /api/config, PUT
     * /api/config and every refresh. The observed symptom was the web app's primary save
     * failing with HTTP 500 {"error":"oom"} on a device with 80 KB free.
     *
     * The backend's `size` hook gives the exact length when it can; without one (a stub) the
     * maximum is still correct, just larger than necessary. */
    size_t max = CFG_JSON_MAX_LEN;
    if (store->size) {
        const size_t have = store->size(store->ctx, CFG_KEY);
        /* +1, AND IT IS NOT COSMETIC: the read terminates the string in the buffer it is given,
         * so a document that EXACTLY fills the buffer is refused rather than handed back
         * unterminated. Allocating the bare stored length therefore fails every read — the
         * config silently reverts to the built-in default on a device that has one stored. */
        if (have > 0 && have + 1 <= CFG_JSON_MAX_LEN) max = have + 1;
    }
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
