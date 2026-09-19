#pragma once

#include <stddef.h>
#include <stdint.h>

/* Chunked static-bitmap upload state machine (IF-2a).
 *
 * WHY THIS IS A PURE MODULE: the four rules that make an upload safe are ordering, size,
 * checksum and completeness — all of which are logic, not hardware. Keeping them here means
 * they are host-testable (NFR-6), and the HTTP handler in components/api becomes a thin
 * adapter that parses a request and forwards bytes. A bug in ordering validation would
 * otherwise only be findable by driving a real device over the network.
 *
 * The atomic promote is deliberately NOT here: swapping which slot is active is a flash
 * operation, and the caller owns it. This module only decides whether an upload has EARNED
 * a promote. */

/* 78,200 bytes: 920x680 at 1 bpp (HW-6). The upload is validated against exactly this. */
#define BITMAP_UPLOAD_TOTAL 78200u

/* Largest chunk the device will accept in one request. 4 KB matches the flash sector size,
 * so a chunk maps to whole erase units and the web app never has to reason about alignment.
 * (Resolves spec Q9's chunk size; the storage target is a pair of raw partitions, see
 * bitmap_slot.h.) */
#define BITMAP_UPLOAD_MAX_CHUNK 4096u

typedef enum {
    UPLOAD_OK = 0,
    UPLOAD_ERR_NO_SESSION,   /* a chunk or commit arrived with no upload in progress */
    UPLOAD_ERR_ALREADY_ACTIVE, /* begin() called while a session is open */
    UPLOAD_ERR_BAD_TOTAL,    /* declared total is not BITMAP_UPLOAD_TOTAL */
    UPLOAD_ERR_BAD_OFFSET,   /* out of order, overlapping, or past the end */
    UPLOAD_ERR_BAD_LENGTH,   /* zero, or larger than BITMAP_UPLOAD_MAX_CHUNK */
    UPLOAD_ERR_INCOMPLETE,   /* commit before every byte arrived */
    UPLOAD_ERR_CHECKSUM,     /* commit's checksum does not match the received bytes */
} upload_result_t;

typedef struct {
    int      active;         /* a session is open */
    uint32_t total;          /* declared total for this session */
    uint32_t received;       /* bytes accepted so far */
    uint32_t crc;            /* running CRC-32 over the accepted bytes */
} upload_session_t;

/* Start a session. `total` must be exactly BITMAP_UPLOAD_TOTAL: a mismatch is refused here
 * rather than at the end, so a wrong-sized upload fails on its first request instead of
 * after 78 KB of traffic. */
upload_result_t upload_begin(upload_session_t *s, uint32_t total);

/* Accept one chunk at `offset`.
 *
 * `offset` must equal the number of bytes accepted so far. That single rule rejects
 * out-of-order, duplicate, overlapping and gapped chunks at once — there is no legitimate
 * upload that sends them in any other order, because the device never asks for a retry of a
 * specific range. */
upload_result_t upload_chunk(upload_session_t *s, uint32_t offset,
                             const uint8_t *data, uint32_t len);

/* Finish a session. Succeeds only if every byte arrived AND `crc` matches what was actually
 * received. On success the session is closed and the caller may promote the slot; on
 * failure the session is also closed, so a bad upload cannot be resumed into a good one. */
upload_result_t upload_commit(upload_session_t *s, uint32_t crc);

/* Abandon a session (client disconnect, timeout). The caller must leave the previously
 * active bitmap untouched — that is the whole point of writing to a spare slot. */
void upload_abort(upload_session_t *s);

/* CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320) — the same value zlib's crc32() and any
 * JS CRC-32 implementation produce, so the web app can compute it without sharing code.
 * Implemented here rather than using esp_crc.h so it is identical on the host and the
 * device; a checksum that differs between the two is worse than no checksum. */
uint32_t upload_crc32(uint32_t crc, const uint8_t *data, size_t len);
