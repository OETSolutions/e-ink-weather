#pragma once

#include <stddef.h>
#include <stdint.h>

/* The on-device store for the config document and the two static-bitmap slots (IF-1, IF-2a).
 *
 * WHY AN INTERFACE AND NOT DIRECT NVS CALLS: the validate-before-store rule (Task 14 Step 2)
 * is the thing that matters — a bad config must never be persisted, or the device boots into
 * an unparseable state and bricks itself. That rule is logic and is host-tested here; the
 * flash backend is a thin adapter.
 *
 * The bitmap does NOT live in NVS. NVS is 24 KB in this partition table and the bitmap is
 * 78,200 bytes, so it gets two dedicated raw partitions (see bitmap_slot.h). */

/* A backend the host tests can stub and the device backs with NVS. */
typedef struct {
    /* Read a blob. Returns 0 on success, non-zero if absent or on error. */
    int (*read)(void *ctx, const char *key, void *out, size_t max, size_t *len);
    /* Write a blob, replacing any previous value. Returns 0 on success. */
    int (*write)(void *ctx, const char *key, const void *data, size_t len);
    /* How many bytes the stored blob occupies, or 0 if nothing is stored. Optional: a NULL
     * here means "unknown" and the read falls back to the maximum, which is correct but
     * allocates CFG_JSON_MAX_LEN for every load.
     *
     * WHY THIS EXISTS: the read used to always allocate the 16,384-byte maximum, and on this
     * part that is a large enough request to FAIL on a heap whose largest free block is
     * momentarily 13-17 KB — which is the normal state under HTTP load. The shipped config is
     * ~4.6 KB, so the maximum was ~3.5x more than any document needs, and the over-request was
     * the difference between "the web app saved your layout" and a 500. */
    size_t (*size)(void *ctx, const char *key);
    void *ctx;
} cfg_store_t;

/* The largest document this store will read back, and the limit the API enforces on a write.
 *
 * ONE CONSTANT FOR BOTH ENDS, deliberately. These used to be 4096 (here) and 16384 (the API),
 * and the shipped default layout is 4,424 bytes — so a document in between was ACCEPTED and
 * stored, then refused on read (NVS returns INVALID_LENGTH rather than truncating), which made
 * cfg_store_get() fall back to the built-in default with no error anywhere. A user would save a
 * layout whose size happened to land in that window and watch the device quietly forget it. */
#define CFG_JSON_MAX_LEN 16384

/* Persist `json` only after validating it. Returns 0 on success.
 *
 * Validation order matters and is the whole point of this function:
 *   1. the document must be JSON and carry a numeric schemaVersion;
 *   2. `devcfg_migrate` must accept that version;
 *   3. `layout_config_parse` must accept the (migrated) document.
 * Only then is anything written. A document that fails any step is refused and the STORED
 * config is left exactly as it was — so a bad write cannot brick the device. */
int cfg_store_put(const cfg_store_t *store, const char *json);

/* Load the stored config. Returns 0 and a malloc'd string the caller frees, or non-zero if
 * nothing is stored. Returns the empty default document only when nothing has ever been
 * written — never as a fallback for a corrupt one. */
int cfg_store_get(const cfg_store_t *store, char **out_json);

/* The document used when the device has never been configured. */
const char *cfg_store_default_json(void);
