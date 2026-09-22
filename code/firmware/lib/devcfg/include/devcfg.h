#pragma once

#define DEVCFG_SCHEMA_VERSION 1

/* Upgrade a config JSON document from `from` to `to`.
 * Returns 0 on success and sets *out to a malloc'd string the caller frees.
 * Returns non-zero and leaves *out NULL on any unsupported/unknown version. */
int devcfg_migrate(int from, int to, const char *in_json, char **out_json);

/* Validate and normalise a Home Assistant base URL into `out` (capacity `cap`).
 *
 * The device appends "/api/template" to this value, so it must be a bare base with no trailing
 * slash. Returns 0 on success, or a non-zero code:
 *   -1  NULL/empty input, or out too small
 *   -2  the value does not begin with http:// or https://
 *   -3  nothing is left after the scheme (no host)
 *
 * A pure function, kept out of the request handler so the boundary cases — "http://" alone,
 * "https:///" that would strip to "http:" — are host-testable rather than reachable only by
 * curling a live device. */
int devcfg_normalize_ha_url(const char *in, char *out, unsigned long cap);
