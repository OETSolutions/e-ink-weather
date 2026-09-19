#pragma once

#include "cfg_store.h"

/* Device backends for the two stores (IF-1, IF-2a).
 *
 * These are the thin adapters that make cfg_store.h's validate-before-store rule real on
 * the ESP32. All of the decisions worth testing are in the pure modules; what is here is
 * erase/write ordering, which is exactly the part that cannot be host-tested and therefore
 * the part to keep as small as possible. */

/* An NVS-backed key/value store. Opens the namespace on first use. The returned store's
 * `ctx` is static — no allocation, and no handle to leak.
 *
 * The config document is a few KB; NVS holds it comfortably (the partition is 24 KB). The
 * BITMAP does not go here: at 78,200 bytes it is three times the whole partition. */
const cfg_store_t *cfg_store_nvs(void);

/* Read the live static bitmap into `out` (BITMAP_SLOT_LEN bytes).
 *
 * Returns 0 on success, -1 if neither slot holds a valid image (a fresh device). The caller
 * owns `out`; this does not allocate. */
int bitmap_store_load(uint8_t *out);

/* Streaming promote (IF-2a). An upload is three calls:
 *
 *   bitmap_store_begin_upload();                  once, before the first chunk
 *   bitmap_store_write_chunk(off, data, len);     once per chunk, CONSECUTIVELY
 *   bitmap_store_finish_upload(crc);              once, after the last chunk
 *
 * WHY STREAM INSTEAD OF ASSEMBLING IN RAM: the bitmap is 78,200 bytes and this part has
 * 320 KB of RAM with no PSRAM (NFR-2). Buffering it would cost a quarter of the heap for
 * the duration of every upload, and the whole point of the two-slot design is that the
 * bytes can go straight to the SPARE partition without risking the live one.
 *
 * The sequence is safe against a power cut at every point: begin_upload erases the spare
 * partition, which also zeroes its header, so the slot is INVALID until finish_upload
 * writes a valid header. An interrupted upload therefore leaves the previous image live and
 * selected (FR-29) — it never leaves the panel blank.
 *
 * `offset` must equal the number of bytes already written; that is the caller's contract,
 * enforced here as a defence in depth against a chunking bug in the HTTP adapter.
 *
 * Returns 0 on success, -1 on any failure. A failure aborts the upload and leaves the
 * previously live image untouched. */
int bitmap_store_begin_upload(void);
int bitmap_store_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len);

/* Verify the written image against `crc` by reading it back, and only then write the header
 * that makes it live. Returns 0 on success. */
int bitmap_store_finish_upload(uint32_t crc);

/* Abandon an upload: zero the spare slot's header so a partial image can never be selected.
 * Safe to call with no upload in progress. */
void bitmap_store_abort_upload(void);

/* Which slot is live, or -1 if neither is valid. */
int bitmap_store_live_slot(void);
