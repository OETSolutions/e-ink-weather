#pragma once

#include <stddef.h>
#include <stdint.h>
#include "cfg_store.h"
#include "artwork.h"
#include "segbuf.h"

/* Device backends for the stores (IF-1, IF-2a).
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

/* Load the live static layer into a SEGMENTED buffer (see segbuf.h for why segments exist: the
 * one DRAM region large enough for a contiguous 78,200-byte layer is fragmented below it by the
 * live API — the httpd and app task stacks alone split it — so a contiguous allocation fails there
 * intermittently while the render still needs the bytes). This is the path the render uses.
 *
 * `b` must already have been sized with segbuf_alloc(). Returns 0 on success and -1 for the same
 * reasons a contiguous load would (no artwork for the page, no valid bitmap, nothing stored).
 *
 * The artwork is stored as INDEPENDENT PER-STRIP zlib streams and inflated strip by strip (see
 * lib/upload/artwork.h), so this needs NO 32 KB LZ dictionary — an earlier single-stream version
 * needed a ~44 KB working set that could only fit the SAME large region the layer's segments need,
 * and the two together did not fit once WiFi was up. What remains is a ~11 KB decompressor state.
 *
 * THAT STATE IS RESERVED BY THE CALLER, BEFORE THE SEGMENTS, and freed once the layer is loaded.
 * Not for size — 11 KB coexists with the segments easily — but for PLACEMENT: allocated lazily
 * during the decode (when the segments already hold the large blocks) it intermittently found no
 * hole of its size class and the strip failed, which on the glass looks like the panel silently
 * keeping its old picture. Reserved first, it takes a block while the heap is still clean. So the
 * reservation is a SEPARATE, ORDERED STEP the caller performs before sizing the layer. */
typedef struct artwork_inflate artwork_inflate_t;

artwork_inflate_t *artwork_inflate_reserve(void);
void artwork_inflate_release(artwork_inflate_t *w);

int bitmap_store_load_seg(segbuf_t *b);

int artwork_store_load_page_seg(int page, segbuf_t *b, artwork_inflate_t *w);

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

/* ------------------------------------------------------------- per-page artwork (FR-15) --
 *
 * Its OWN pair of raw partitions (artwork_a/artwork_b) holds a SET of compressed static layers,
 * one per page, so page rotation draws each page on its own artwork instead of stamping its
 * readings onto the previous page's labels. Compressed with zlib and inflated from the ROM — see
 * lib/upload/artwork.h for why that is what makes the set fit.
 *
 * WHY ITS OWN PAIR AND NOT THE BITMAP SLOTS: a promote erases its spare slot before writing, so
 * two stores sharing one pair erase each other — pushing artwork wiped the bitmap and the panel
 * rendered its widgets over the factory boot mark. See partitions.csv.
 *
 * Upload is the same three-call streaming shape as the bitmap, into the SPARE slot, so an
 * interrupted upload leaves the live set intact:
 *
 *   artwork_store_begin_upload()
 *   artwork_store_write_chunk(off, data, len)    consecutively
 *   artwork_store_finish_upload(crc)
 *
 * The client sends header + entry table + blob; the header (which carries a client-side seq of 0
 * and crc) is diverted into RAM rather than flashed at offset 0, because NOR flash cannot set
 * bits — a promote writing seq 1 over a stored 0 would silently fail. See artwork_store_write_chunk.
 */

/* How many pages the live artwork set covers, or 0 when none is stored. */
int artwork_store_page_count(void);

/* The live set's identity, for deciding whether the picture on the glass is still the one a render
 * would produce: *page_count_out = how many pages it covers, *seq_out = its promotion sequence.
 * Both come from ONE read of the live header, because reading them separately could straddle a
 * promote and describe a set that never existed as a whole.
 *
 * WHY THE SEQUENCE IS NEEDED AND THE PAGE INDEX ALONE IS NOT: a partial refresh is only valid when
 * the frame on the glass and the frame about to be drawn are built from the SAME picture. A page
 * index cannot tell those apart across an upload — pushing a NEW layout for the page already on
 * display keeps the index (and any identity derived from it) unchanged while the picture changes
 * underneath. A partial would then diff the new layout against the old one and leave ghosted
 * fragments of the previous layout on the glass. Folding in the promotion sequence makes every
 * upload change the identity, which is what forces the full refresh that api_request_full_refresh()
 * already asks for. */
void artwork_store_identity(int *page_count_out, uint32_t *seq_out);

int artwork_store_begin_upload(void);
int artwork_store_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len);
int artwork_store_finish_upload(uint32_t crc);
void artwork_store_abort_upload(void);
