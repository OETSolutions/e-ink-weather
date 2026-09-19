#pragma once

#include <stddef.h>
#include <stdint.h>

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
 * without letting a hostile or buggy client allocate the device to death. */
#define API_CONFIG_MAX_LEN 16384

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
