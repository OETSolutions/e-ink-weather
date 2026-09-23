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
/* MUST HOLD A WHOLE FORMATTED VALUE. This is the string the editor previews, and it is copied
 * from the same buffer the panel draws from — so it has to be at least as long as
 * LAYOUT_VALUE_BUF or the preview would show a truncated reading while the glass showed the whole
 * one, which is the preview/panel disagreement NFR-4 exists to prevent. Kept as its own macro so
 * this library does not have to include the layout headers, but tied to the same number. */
#define API_VALUES_TEXT_LEN 48

typedef struct {
    const char *id;                 /* the widget's id from the document; may be NULL */
    const char *text;               /* the resolved string, exactly as the panel draws it */
    /* 1 when the widget resolved to a real reading, 0 when it fell back (no data, no entity,
     * fetch failed). The editor shows the fallback either way — that is what the panel does —
     * but it can mark which boxes are previews and which are live. */
    int         has_value;
    /* THE RAW READING AND WHETHER `text` IS ITS PLAIN RENDERING, so the editor can re-format it
     * after a format edit instead of waiting for a save and a repaint.
     *
     * WHY `text` ALONE IS NOT ENOUGH. It is already formatted with the STORED prefix/suffix/
     * decimals, so it becomes useless the instant the user edits one of those: the box would keep
     * showing the old formatting until the config was saved and the device had drawn again —
     * reported as "the layout editor doesn't show the updated values until you first save and
     * refresh".
     *
     * WHY `rendered_number` IS REPORTED RATHER THAN INFERRED BY THE EDITOR. `text` is a plain
     * number rendering only in SOME of the cases that reach the glass: a firing alert replaces the
     * reading with a level word, a text reading (a condition, a binary sensor's "on"/"off") has no
     * number to format, a weather icon binding carries the raw OWM code, and a failed fetch shows
     * the widget's own fallback. Re-formatting `value` in any of those would put a number on screen
     * where the panel has a word — a preview that contradicts the glass, which is the failure NFR-4
     * exists to prevent. The firmware already knows which branch it took, so it says so, and the
     * editor re-formats when and only when the answer is `1`. Everything else is echoed verbatim,
     * which is always safe because `text` is by construction what the panel drew.
     *
     * `value` is meaningful only when `rendered_number` is 1. It is a DOUBLE, not a float: the
     * editor reproduces the device's digits via printf's exact rounding rule, and a float would
     * round-trip with less precision than the device held. */
    double      value;
    int         rendered_number;
} api_value_t;

typedef struct {
    const api_value_t *items;
    int                count;
    /* WHICH PAGE THESE VALUES ARE FOR. This is the page the CALLER asked about, which is not
     * necessarily the page on the glass: the editor edits one page while the device rotates
     * through them on its own, so the two are reported separately (see `drawn_page`). The
     * editor's boxes must fill in for the page being EDITED, which is why the device resolves
     * every page rather than only the one it drew. */
    int                page;
    /* The page currently drawn on the panel, or -1 while nothing has been drawn. Distinct from
     * `page` so the editor can say "the display is showing page 1" while previewing page 2. */
    int                drawn_page;
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

/* AN UPPER BOUND ON THE BODY api_values_json() CAN PRODUCE FOR A FULL PAGE, so the caller can
 * size its buffer from the format rather than from a guess.
 *
 * WHY THIS EXISTS RATHER THAN A HAND-PICKED NUMBER. The endpoint used a fixed 2048-byte buffer, and
 * a full page of 24 widgets had ALREADY outgrown it — 24 items at ~103 bytes each is ~2.5 KB, so a
 * page that reached the cap would have been answered with a 500 rather than with its values. It went
 * unnoticed only because real pages bind far fewer widgets than the cap allows. Adding the raw
 * reading widens each item further, turning a latent overflow into a certain one.
 *
 * THE BOUND ASSUMES HALF THE BYTES COULD NEED ESCAPING, which is far beyond anything a real reading
 * contains: a temperature, a condition word or a place name has no quotes and no control characters,
 * so the expansion is zero. Sizing for the true worst case (every byte a six-character \uXXXX escape)
 * would be a 12 KB buffer, and this is allocated while the editor polls — possibly during a render,
 * when free DRAM is at its 26 KB floor. A bound that the heap cannot always serve would fail in
 * exactly the case it is meant to fix, so it is sized for plausible input instead. Pathological
 * input is not silently truncated either way: api_values_json returns -1 and the endpoint answers a
 * clean 500 rather than a half-written document. */
#define API_VALUES_JSON_MAX \
    ((size_t)128 + (size_t)API_VALUES_MAX * \
        ((size_t)(API_VALUES_ID_LEN * 2) + (size_t)(API_VALUES_TEXT_LEN * 2) + 64))
