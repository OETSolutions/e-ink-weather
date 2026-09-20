#pragma once

#include <stddef.h>
#include <stdint.h>

/* /api/status response building (FR-33).
 *
 * WHY PURE: the value of /api/status is that it is TRUSTWORTHY remotely — it is what you
 * have when a device in the field misbehaves and you cannot attach a debugger. That means
 * the encoding has to be right, and JSON escaping/truncation bugs are exactly the kind that
 * only show up in the field. Keeping it free of hardware means it is host-tested (NFR-6);
 * the HTTP handler just fills the struct and writes the string. */

#define API_STATUS_MAX_ERRORS 5
#define API_STATUS_ERROR_LEN  64
#define API_STATUS_VERSION_LEN 32

typedef struct {
    const char *version;          /* firmware version string */
    uint32_t    uptime_s;
    uint32_t    free_heap;        /* current */
    uint32_t    free_heap_min;    /* watermark — the number that matters for a leak */
    int         rssi;             /* dBm; 0 means "not connected" */
    int         has_rssi;         /* separates a real 0 dBm from "no reading" */
    double      vbat;             /* volts */
    int         has_vbat;
    /* 0 unknown, 1 USB, 2 battery. A bare voltage is ambiguous — 4.29 V is either a full
     * Li-ion pack or USB — and FR-33 exists to diagnose a field failure, where "why did this
     * device stop waking" hinges on which of those it was. */
    int         vbat_source;
    int         partials_since_full;
    int         fulls_total;
    int         last_refresh_age_s;   /* -1 if never refreshed */
    /* Which bitmap slot is live, or -1 if neither holds a valid image. Without this, "the
     * panel is showing the wrong picture" is indistinguishable from "the upload never
     * landed" — and an interrupted upload is supposed to leave the OLD image live, so the
     * two cases are exactly what an operator needs told apart (IF-2a). */
    int         bitmap_slot;
    /* OWM calls made in the current calendar day, and whether that count is trustworthy yet
     * (spec §3.4). Surfaced so a bug cannot silently burn the provider's quota: the free tier
     * is 1,000/day against a 10-15 minute refresh, so a number approaching the cap means the
     * device is refreshing in a loop. `calls_known` is 0 until a reading has supplied a
     * timestamp — without it, "no calls yet" and "we cannot count" would report identically. */
    int         owm_day_calls;
    int         owm_calls_known;
    /* Most recent errors, newest first. Entries may be NULL (skipped). */
    const char *errors[API_STATUS_MAX_ERRORS];
    int         error_count;
} api_status_t;

/* Serialise to `out` (NUL-terminated). Returns the number of bytes written excluding the
 * terminator, or -1 if the buffer is too small — never a truncated document, because a
 * truncated JSON body is unparseable and would look like a device fault when the real
 * problem is this function. */
int api_status_json(const api_status_t *s, char *out, size_t outlen);

/* Append one error message to a ring, newest first, dropping the oldest. Keeps the field
 * small and bounded so a device stuck in an error loop cannot exhaust RAM building a
 * status response. Returns 0 on success. */
int api_status_push_error(char errors[API_STATUS_MAX_ERRORS][API_STATUS_ERROR_LEN],
                          int *count, const char *msg);
