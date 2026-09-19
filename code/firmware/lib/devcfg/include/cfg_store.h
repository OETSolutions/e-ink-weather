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
    void *ctx;
} cfg_store_t;

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
