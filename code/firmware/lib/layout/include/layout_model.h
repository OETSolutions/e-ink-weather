#pragma once
#include <stddef.h>

/* The on-device view of a configuration document (IF-1). The web app owns the
 * full document; the firmware needs only the parts it must act on: the wake
 * interval, the partial-refresh limit, and the page rotation schedule. */

/* Must track webapp/src/model/config.ts:SCHEMA_VERSION. The parser refuses anything
 * newer, so a web app that bumps the schema without this following is a loud failure
 * rather than a silently wrong layout. */
#define LAYOUT_SCHEMA_VERSION 1

#define LAYOUT_MAX_PAGES 8

/* Bounds that exist to keep `elapsed_seconds % total` well defined. Every interval is
 * summed into a `long`, so an unclamped 2^31-second page would overflow the sum to a
 * NEGATIVE total; the modulo would then be undefined and the device would show an
 * arbitrary page. A week is far past any sane dwell, so clamping costs nothing real. */
#define LAYOUT_MIN_INTERVAL_SECONDS 30
#define LAYOUT_MAX_INTERVAL_SECONDS 604800

/* Bounds the partial-refresh counter (FR-11) so it cannot be configured into an
 * overflow, and so a mistyped limit cannot suppress full refreshes indefinitely. */
#define LAYOUT_MAX_PARTIAL_LIMIT 1000

typedef struct {
    char name[48];
    int  refresh_seconds;   /* how long this page stays before advancing */
    int  weight;            /* relative dwell when auto-rotating, >= 1 */
} layout_page_t;

typedef struct {
    int           schema_version;
    int           update_seconds;         /* base device wake interval, >= 30 */
    int           partial_refresh_limit;  /* full refresh after N partials (FR-11) */
    layout_page_t pages[LAYOUT_MAX_PAGES];
    int           page_count;
} layout_config_t;

/* Parse the subset of the config the firmware needs. Returns 0 on success, or
 * negative on malformed/unsupported input. Defaults cover absent fields, so a
 * minimal document is valid.
 *
 * A document whose `schemaVersion` is NEWER than LAYOUT_SCHEMA_VERSION is refused
 * (FR-26b) rather than parsed on a best-effort basis: the same field name may mean
 * something different in the next version, and guessing would put a wrong layout on
 * the glass with no error anywhere. */
int layout_config_parse(const char *json, layout_config_t *out);

/* Which page is shown `elapsed_seconds` into the rotation? Pure function of the
 * config, so it is fully host-testable (FR-15/FR-16). Returns 0 for a
 * single-page config at any elapsed time. */
int layout_page_at(const layout_config_t *cfg, long elapsed_seconds);
