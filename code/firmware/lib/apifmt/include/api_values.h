#pragma once

#include <stddef.h>

/* /api/values response building (FR-27).
 *
 * FR-27 requires the editor's preview to be rendered "with real fetched data before pushing to
 * the device", and this is where that data comes from. The device is the only thing that
 * already knows it: it fetches the OWM and HA documents and runs every widget through
 * value_format_widget(), which is the SAME code path that puts the string on the glass. So the
 * endpoint reports what the device resolved, and the editor renders that.
 *
 * WHY THIS RATHER THAN LETTING THE APP FETCH AND FORMAT ITSELF: a second implementation of
 * value resolution in TypeScript would drift from the firmware's — the fallback rules, the
 * decimals default, the alert replacement and the HA slot indexing are all subtle, and the
 * preview's whole purpose is to be trustworthy about what the panel will show (NFR-4). It also
 * cannot work in the embedded case: the app is served BY the device, so the browser and the
 * device share an origin but the browser has no OWM key and, on the setup network, no internet.
 *
 * WHY PURE AND HOST-TESTED: the same reason as api_status_json() — this is a field-diagnostic
 * style body whose escaping has to be right, and it must never emit a truncated document. A
 * widget's value is arbitrary text (a condition word, a place name), so it is escaped, not
 * assumed safe. */

#define API_VALUES_MAX      24
#define API_VALUES_ID_LEN   24
#define API_VALUES_TEXT_LEN 40

typedef struct {
    const char *id;                 /* the widget's id from the document; may be NULL */
    const char *text;               /* the resolved string, exactly as the panel draws it */
    /* 1 when the widget resolved to a real reading, 0 when it fell back (no data, no entity,
     * fetch failed). The editor shows the fallback either way — that is what the panel does —
     * but it can mark which boxes are previews and which are live. */
    int         has_value;
} api_value_t;

typedef struct {
    const api_value_t *items;
    int                count;
    /* The page these values belong to, and whether the schedule has a page at all. Lets the
     * editor say "showing page 2" rather than silently previewing a different page than the
     * one being edited. */
    int                page;
    int                page_count;
    /* The device's own unix time at resolution, 0 if it has none. The device's clock is
     * seconds-since-boot, so this is not wall-clock — but it is monotone and lets the editor
     * show how stale the preview is. */
    long               resolved_at;
} api_values_t;

/* Serialise to `out` (NUL-terminated). Returns bytes written excluding the terminator, or -1
 * if the buffer is too small or the input is unusable — never a truncated document, because a
 * truncated JSON body is unparseable and reads as a device fault rather than a size problem. */
int api_values_json(const api_values_t *v, char *out, size_t outlen);
