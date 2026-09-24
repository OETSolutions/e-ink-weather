#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Parsing and comparing the release manifest the GitHub release publishes.
 *
 * This is a LIBRARY rather than living in the OTA handler because the two things that can go
 * wrong here are both silent and both worth testing on the host: a version comparison that
 * orders "0.10.0" against "0.9.0" as strings (it is NEWER, but string order says older, so the
 * device would never update) and a manifest whose shape drifted. Neither needs a radio or a
 * panel to catch, so neither should require one.
 */

/* The flat manifest published as a release asset:
 *   { "version": "0.1.0", "firmware": "firmware.bin", "sha256": "<64 hex>", "size": 1838224 }
 * Deliberately flat — the device parses this on a small stack, so a deeply nested document
 * would be the wrong shape regardless of what the API could provide. */
typedef struct {
    char version[32];       /* semver, the part compared for "is there an update"      */
    char firmware[128];     /* asset filename inside the release                       */
    char sha256[65];        /* lowercase hex, empty if the manifest omitted it          */
    long size;              /* bytes, or -1 if absent/unparseable                      */
} otarelease_manifest_t;

/* Parse `json` into `out`. Returns 0 on success. Fails (-1) on a null/empty document, a
 * missing or empty `version`, or a `firmware` that is not a bare filename — see the
 * traversal note in the implementation. `sha256` and `size` are optional. */
int otarelease_parse(const char *json, otarelease_manifest_t *out);

/* Compare two dotted decimal versions (1 to 4 components; a leading "v" is ignored). Returns
 * -1, 0 or +1. Missing components are treated as 0, so "1.2" == "1.2.0" and "1.2" < "1.2.1".
 * Any non-numeric component makes the comparison fall back to a plain strcmp of the originals,
 * so a malformed version cannot claim to be newer than a good one. */
int otarelease_version_cmp(const char *a, const char *b);

/* True when `latest` is strictly newer than `current`. */
bool otarelease_is_newer(const char *current, const char *latest);

/* Join a release base URL and the manifest's firmware filename into `out`.
 * base e.g. "https://github.com/oetsolutions/e-ink-weather/releases/latest/download"
 *   -> "<base>/firmware.bin". Returns 0 on success, -1 on truncation or a missing filename.
 * The base is NOT re-validated here: api_ota.c owns the https-only policy, and duplicating it
 * would let the two drift. */
int otarelease_firmware_url(const char *base, const otarelease_manifest_t *m,
                            char *out, size_t cap);
