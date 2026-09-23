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

/* Find `"key"` in the text as a KEY (not a string VALUE that happens to contain the text), and
 * return a pointer just past the closing quote. NULL when there is no such key.
 *
 * A key is preceded by `{` or `,` — ignoring whitespace — which is what distinguishes it from a
 * value like `"generator":"schemaVersion"`, whose own quotes form a bare `"schemaVersion"` in the
 * raw bytes and which a plain strstr finds FIRST. The search continues past such a match rather
 * than giving up, because the real key may come later in the document. */
static const char *find_key(const char *json, const char *key)
{
    char pat[48];
    const int pn = snprintf(pat, sizeof(pat), "\"%s\"", key);
    if (pn <= 0 || (size_t)pn >= sizeof(pat)) return NULL;

    const char *k = strstr(json, pat);
    while (k) {
        const char *p = k;
        while (p > json && (p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r')) p--;
        if (p != json && (p[-1] == '{' || p[-1] == ',')) return k + pn;
        k = strstr(k + 1, pat);
    }
    return NULL;
}

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
    const char *k = find_key(json, "schemaVersion");
    if (!k) return -1;

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

/* Is the stored document already byte-for-byte this one? Returns 1 when it certainly is,
 * 0 otherwise (including "cannot tell" — see below).
 *
 * WHY THIS EXISTS: NVS APPENDS a fresh entry for a rewritten blob and reclaims the old one only
 * later, by garbage collection. So every save of the config costs a multi-page write to the flash
 * even when the document has not changed by a byte — and the config app's Save button, the
 * verification scripts and any retry all write the same document again. Pressing Save twice with
 * nothing edited doubles the write for no reason, and the reclaim path in the NVS backend exists
 * precisely because a long run of these fills the partition. Skipping the write entirely when the
 * content is identical removes that wear at the source; a no-op save then costs one read.
 *
 * FAILS OPEN, DELIBERATELY. Every uncertainty — a store with no size hook, a different stored
 * length, a failed read, a failed allocation — returns 0, which writes the document exactly as the
 * code did before. A missed dedup costs one write; a WRONG dedup would silently discard the user's
 * change, which is far worse, so nothing here may guess.
 *
 * THE ORDER OF THE TWO TESTS IS LOAD-BEARING. Comparing lengths first means a changed document
 * almost always answers in O(1) with no allocation at all — the common case is an edit, and this
 * must not make it more expensive than it was.
 *
 * NO PEAK MEMORY IS ADDED. The comparison buffer is allocated and freed BEFORE the parse, so the
 * peak while comparing is (body + buffer) and the peak while parsing is (body + tree); the two do
 * not stack. That matters on this part, where a second contiguous document-sized block alongside
 * cJSON's tree is exactly what makes a full-size config unparseable. */
static int stored_is_identical(const cfg_store_t *store, const char *json)
{
    if (!store->size || !store->read) return 0;

    /* `size` reports the stored blob's length, which INCLUDES the NUL the writer stored, and a
     * stored length of 0 means nothing is there. A document of a different length cannot be equal,
     * which is the answer for the common case (an edit) with no allocation at all. */
    const size_t len = store->size(store->ctx, CFG_KEY);
    if (len == 0 || len != strlen(json) + 1) return 0;

    /* ONE BYTE MORE THAN THE BLOB. The backends refuse a read whose buffer the blob exactly fills,
     * because they append the terminator themselves — so a comparison buffer sized to the bare
     * length could never be read back, and the dedup would silently never fire. */
    char *buf = malloc(len + 1);
    if (!buf) return 0;                    /* fail open: write it, as before */

    size_t got = 0;
    const int rc = store->read(store->ctx, CFG_KEY, buf, len + 1, &got);
    const int same = (rc == 0 && got == len && memcmp(buf, json, len) == 0);
    free(buf);
    return same;
}

int cfg_store_put(const cfg_store_t *store, const char *json)
{
    if (!store || !store->write || !json) return -1;

    /* 1. must be JSON with a numeric schemaVersion */
    int ver = 0;
    if (read_schema_version(json, &ver) != 0) return -2;

    /* 1b. AN UNCHANGED DOCUMENT IS NOT WRITTEN AGAIN — see stored_is_identical(). Checked here,
     * before the migrate and the parse, so a no-op save costs neither a cJSON tree nor a flash
     * write. It is safe AFTER the schema check (the document is known parseable-shaped), and its
     * equality implies the stored copy already passed every check below when it was first stored,
     * so skipping cannot admit anything that would have been refused. */
    if (stored_is_identical(store, json)) return 0;

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
