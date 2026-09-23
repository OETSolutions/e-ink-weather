#pragma once

#include <stddef.h>
#include <stdint.h>
#include "cfg_store.h"

/* Request framing for the device HTTP API (IF-4).
 *
 * WHY THE UPLOAD PUTS NUMBERS IN THE QUERY AND BYTES IN THE BODY: the chunk is up to 4 KB
 * of raw 1bpp data. Carrying it inside a JSON document would force base64, which inflates
 * every chunk by a third and requires a decoder on both sides — two more places for the
 * firmware and the web app to disagree about the same 78,200 bytes. Query parameters keep
 * the metadata out of the payload and the payload byte-exact.
 *
 * The parsing is pure and host-tested (NFR-6) because a wrong `offset` is the difference
 * between rejecting a bad chunk and corrupting the framebuffer with it. */

/* The largest config document PUT /api/config will accept.
 *
 * Bounded deliberately: the device must copy the body to NUL-terminate it before parsing,
 * and an unbounded body is a heap-exhaustion vector on a device with no PSRAM (NFR-2). A
 * real layout — 8 pages of widgets — measured well under 8 KB; 16 KB leaves headroom
 * without letting a hostile or buggy client allocate the device to death.
 *
 * ALIASED to CFG_JSON_MAX_LEN rather than being a second literal. These two were 4096 and 16384
 * once, and the gap between them was a real bug: a document the API accepted and stored could
 * be too large for cfg_store_get() to read back, which made the device quietly fall back to its
 * built-in default. The read limit and the write limit are the same document, so they are now
 * the same number — and it is not possible to change one without the other. */
#define API_CONFIG_MAX_LEN CFG_JSON_MAX_LEN

/* Read an unsigned integer query parameter.
 *
 * `query` is the part after '?' (a leading '?' or '&' is tolerated, as is a trailing one).
 * Returns 0 and sets *out when `key` is present and its value is a plain non-negative
 * decimal integer that fits in uint32_t. Returns -1 otherwise: absent, empty, signed,
 * non-numeric, carrying trailing junk, or overflowing.
 *
 * Values are NOT percent-decoded. That is deliberate — digits are never percent-encoded by
 * a correct client, and rejecting an encoded value fails closed rather than guessing at
 * the intended number. */
int api_query_u32(const char *query, const char *key, uint32_t *out);

/* Is (lat, lon) a location the user actually chose?
 *
 * Returns 0 (false) for the app's "not chosen yet" sentinel — both components exactly 0 —
 * and for anything out of range. Returns 1 for a usable coordinate.
 *
 * WHY THIS IS A FUNCTION AND NOT AN INLINE COMPARISON IN THE HANDLER: the app ships its
 * default document at (0, 0) and the device persists whatever the document carries on every
 * Save, while geo_ip_fill_if_unset() treats ANY stored coordinate as the user's own and will
 * never overwrite it. So the sentinel and the "real location" test have to agree exactly, or a
 * Save before placing a pin pins the device to the Gulf of Guinea for good and silently
 * disables the very autofill that exists to learn the real city — verified on the bench,
 * where the panel read "Globe" (OWM's name for 0,0) with nothing to indicate a problem. That
 * agreement is worth a test, and the HTTP handler it lives behind is not host-testable. */
int api_location_is_set(double lat, double lon);

/* Copy a plain string query parameter into `out` (NUL-terminated), rejecting anything that is
 * not a conservative token: lowercase letters, digits, '_', '.'. Returns 0 on success and
 * -1 when the key is absent, empty, over-long, or carries a character outside that set.
 *
 * WHY A STRICT WHITELIST RATHER THAN A GENERAL DECODER: the one caller is the Home Assistant
 * entity-id search, and the term is interpolated into a Jinja template the device asks HA to
 * render. A general decoder (or a "%" pass-through) would hand Jinja the exact characters that
 * mean something to it — a quote, a brace, a backslash — turning the query into template
 * injection. Entity ids only ever contain that set, so a term outside it cannot match one, and
 * rejecting fails closed instead of sanitising. */
int api_query_token(const char *query, const char *key, char *out, size_t outlen);


/* Send the config with the device's stored location spliced into it, WITHOUT allocating.
 *
 * WHY SLICES AND NOT AN EDITED COPY. The GET path has to hand back the device's stored location
 * rather than whatever the document happens to carry, and the obvious ways to do that both need a
 * second buffer of roughly the document's size:
 *
 *   - cJSON_Parse + swap + cJSON_Print wants ~4x the document in ONE piece of DRAM.
 *   - Editing the text in a copy wants ~1x, plus the copy the store already handed us.
 *
 * This part has 320 KB with no PSRAM, and the render path pins free heap at ~2 KB for about two
 * seconds while it holds the panel's static layer. During that window NO second buffer of any real
 * size can exist, so both approaches fail there — measured on the bench in the real flow (a save,
 * then reloading the editor): the endpoint answered with the document's own (0, 0), the app's "no
 * pin placed yet" sentinel, on 9 of 100 loads. Waiting the window out narrows that but cannot close
 * it, because the window outlasts the app's own request deadline.
 *
 * So this describes the answer as a list of spans instead: pieces OF THE CALLER'S EXISTING BUFFER,
 * plus two short numbers formatted onto the caller's STACK. Nothing is allocated, so there is no
 * window in which it can fail.
 *
 * Fills `out` with the spans in the order they must be sent and returns how many there are (1..5),
 * or -1 when there is nothing to splice and the caller should send `json` unchanged — a document
 * with no location object anywhere to insert into, or a result that will not fit the caller's
 * slices.
 *
 * `num_buf` holds the two formatted numbers; the spans point into it, so it must outlive them.
 * `api_slice_num_buf_len()` is the size it needs.
 *
 * Pure and host-tested (NFR-6): a textual edit fails by producing a corrupt document rather than an
 * obvious error, and the numbers must format EXACTLY as cJSON would or a spliced document would
 * differ byte-for-byte from one that went through a tree. */
typedef struct {
    const char *p;
    size_t      len;
} api_slice_t;

/* Bytes `num_buf` must have: two doubles at cJSON's worst-case 17-significant-digit width. */
#define API_SLICE_NUM_BUF_LEN 128
/* The most spans a splice can produce: head, number, middle, number, tail. */
#define API_SLICE_MAX 5

int api_location_slices(const char *json, double lat, double lon,
                        char *num_buf, size_t num_buf_len,
                        api_slice_t *out, int max_out);
