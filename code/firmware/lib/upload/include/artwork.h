#pragma once

#include <stddef.h>
#include <stdint.h>

/* PER-PAGE STATIC ARTWORK (FR-15, "upload per-page layouts").
 *
 * THE DEFECT THIS FIXES: the device had ONE static layer and rotated pages, so a rotating page's
 * readings were stamped onto a DIFFERENT page's artwork — page 2's "TOMORROW HIGH" label sitting
 * over page 1's temperature, with the divider rules in the wrong places. Seen on hardware.
 *
 * WHY COMPRESSED, AND WHY THAT IS THE WHOLE DESIGN: a 1 bpp 920x680 layer is 78,200 bytes raw,
 * so three would fill this flash. But a layout is nearly all white and measures 966 bytes as a
 * zlib stream (81x) — so a set of LAYOUT_MAX_PAGES layers occupies about 8 KB, which fits in the
 * 48 KB artwork slot pair (artwork_a/artwork_b) with room to spare. Storing them uncompressed
 * would not fit at all, and enlarging the partitions would cost the OTA headroom that FR-18's
 * embedded web app needs. The compressor is the web app's (the platform's CompressionStream,
 * 'deflate' = zlib); the decompressor is the ROM's tinfl, so neither costs this firmware any
 * flash.
 *
 * A PAGE IS A SET OF INDEPENDENT PER-STRIP STREAMS, NOT ONE STREAM. A single zlib stream for the
 * whole 78,200-byte layer needs a 32 KB LZ dictionary to decompress into — and that 44 KB working
 * set (dictionary + decompressor state) can only be placed in the ONE DRAM region large enough for
 * a contiguous layer, which the live HTTP API also needs for the layer itself. Measured with heap
 * tracing: the pair does not fit in EITHER order once WiFi is up, so saves silently stopped
 * reaching the panel. Splitting the layer into fixed ARTWORK_STRIP_RAW-byte strips, each its OWN
 * zlib stream with no cross-strip back-references, removes the dictionary entirely: a strip
 * inflates with an output buffer the size of the strip. The strip size divides the layer exactly,
 * so the strips tile it with no partial tail.
 *
 * THE FORMAT is one header per page plus a blob of concatenated streams; each page's entry covers
 * ARTWORK_STRIP_COUNT back-to-back strip streams:
 *
 *     artwork_hdr_t   magic, page_count, crc, seq
 *     entry[page]     offset, comp_len, raw_len
 *     blob            the concatenated zlib streams (per page: the strip streams, in order)
 *
 * An entry with raw_len == 0 means "this page has no artwork", which is distinct from "empty
 * artwork": the renderer then falls back to a blank layer rather than to another page's picture.
 * A page with no artwork must NOT inherit a neighbouring page's, or the wrong labels appear. */

/* "TEGP" little-endian — "PAGE" reversed. BUMPED to "PAGS" (reversed) when a page became a run of
 * independent per-strip streams: an older whole-layer blob is still zlib, still within the size
 * budget, and still passes the entry checks, so WITHOUT a magic change the device would accept it,
 * fail to decode it as strips, and log that failure every tick. Changing the magic makes the
 * incompatibility explicit — an old slot reads as "no artwork" and the render falls back cleanly
 * (FR-29) until the web app pushes a new set. The web app mirrors this value. */
#define ARTWORK_MAGIC       0x53474150u   /* "PAGS" little-endian */
#define ARTWORK_MAX_PAGES   8             /* matches LAYOUT_MAX_PAGES */
#define ARTWORK_RAW_LEN     78200u        /* one inflated layer (HW-6) */

/* THE STRIP GEOMETRY, and every number here is load-bearing (see the block comment above).
 *
 * 3,910 raw bytes is exactly 34 panel rows (34 x 115-byte pitch), so 20 strips tile the 680-row
 * layer EXACTLY — no partial tail strip, which a size that did not divide the layer would leave
 * and which the decoder would then have to special-case. Kept small so a strip's INFLATE OUTPUT
 * (the only buffer a strip stream needs, since it has no dictionary) is small enough to be placed
 * however the heap is fragmented. The compressor and the decompressor MUST agree on these, so they
 * live here next to the format and the web app mirrors them (webapp/src/transfer/artwork.ts). */
#define ARTWORK_STRIP_RAW   3910u
#define ARTWORK_STRIP_COUNT (ARTWORK_RAW_LEN / ARTWORK_STRIP_RAW)   /* 20 */
_Static_assert(ARTWORK_STRIP_RAW * ARTWORK_STRIP_COUNT == ARTWORK_RAW_LEN,
               "the strip size must divide the layer exactly");

/* The compressed budget per page. The measured default layer is 966 bytes; 4 KB allows a
 * genuinely detailed layout (a bitmap-heavy one) to still fit, and 8 pages x 4 KB is 32 KB —
 * comfortably inside the 48 KB artwork slot this lives in. A stream larger than this is refused
 * rather than truncated, because a truncated stream inflates to garbage. */
#define ARTWORK_MAX_COMP    4096u

typedef struct {
    uint32_t magic;
    uint32_t page_count;    /* how many entries follow */
    uint32_t crc;           /* CRC-32 over the entry table AND the blob */
    uint32_t seq;           /* monotonically increasing; 0 means "never written" */
} artwork_hdr_t;

typedef struct {
    uint32_t offset;        /* into the blob */
    uint32_t comp_len;      /* bytes of zlib stream */
    uint32_t raw_len;       /* expected inflated size; 0 = this page has no artwork */
} artwork_entry_t;

/* Is this header usable? A slot failing this must never be rendered — the same rule as
 * bitmap_slot_hdr_valid(), for the same reason: a torn promote must leave the old artwork live. */
int artwork_hdr_valid(const artwork_hdr_t *h);

/* The slot the next promote should write: the one that is NOT live. */
int artwork_spare_slot(int live);

/* Which slot is live, given both headers. The valid one with the higher `seq` wins; ties go to
 * slot A. An invalid header is ignored even with a higher sequence, so a torn write cannot win
 * over a good image. Returns the slot, or -1 when NEITHER is valid (no artwork yet). */
int artwork_pick_slot(const artwork_hdr_t *a, const artwork_hdr_t *b);

/* Find a page's entry in the table. Returns 0 on success, or -1 if the page is out of range or
 * has no artwork (raw_len == 0). */
int artwork_entry_at(const artwork_hdr_t *h, const artwork_entry_t *entries,
                     uint32_t page, artwork_entry_t *out);

/* Where an incoming upload chunk's bytes belong. THE INVARIANT: the client's header is NEVER
 * written to the slot's offset 0.
 *
 * The client streams header + table + blob laid out at the SAME offsets the slot uses, but offset 0
 * of the slot is reserved for the DEVICE's header — the one carrying the real sequence number and
 * the validated CRC. Writing the client's header there breaks the promote permanently: NOR flash
 * only clears bits, so the later write of seq 0 -> seq 1 requires SETTING a bit, fails silently,
 * and every subsequent lookup reads the slot as "never written". On hardware that looked like an
 * upload reporting success while the panel kept its old picture.
 *
 * So the first sizeof(artwork_hdr_t) bytes are diverted into a RAM buffer and everything after them
 * goes to flash at its literal offset (no shift: the client already places the table at
 * artwork_blob_offset()'s base). Called per chunk so a chunk straddling the boundary is split.
 *
 *   *to_hdr    bytes for the header buffer, at *hdr_off within it
 *   *flash_off offset in the slot for the remaining (len - *to_hdr) bytes
 *
 * Pure, so the "the header must not land in flash" rule is host-tested rather than trusted. */
void artwork_chunk_placement(uint32_t offset, uint32_t len,
                             uint32_t *to_hdr, uint32_t *hdr_off, uint32_t *flash_off);

/* Build the header for promoting a table of `n_pages` entries plus `blob_len` bytes into a slot.
 * Returns 0 on success, or -1 if the table is empty, over ARTWORK_MAX_PAGES, or a stream exceeds
 * ARTWORK_MAX_COMP. */
int artwork_make_hdr(const artwork_entry_t *entries, uint32_t n_pages,
                     const uint8_t *blob, uint32_t blob_len,
                     uint32_t live_seq, artwork_hdr_t *out);

/* Offset, within the slot, where the blob begins: header plus the full table. Fixed rather than
 * packed so an entry can be located without reading the whole table first. */
size_t artwork_blob_offset(void);

/* CRC-32 (IEEE 802.3) — the same value the upload's checksum uses, and the same zlib's crc32()
 * produces, so the web app can compute it without sharing code. */
uint32_t artwork_crc32(const uint8_t *data, size_t len);

/* Continuing form, so one checksum can span the entry table AND the blob without staging them
 * into a single buffer. Pass the running value (start with 0xFFFFFFFFu) and finish with
 * `artwork_crc32_finish()`. This exists because the two halves are separately stored on flash:
 * building a combined buffer would mean allocating up to ARTWORK_MAX_COMP * pages of RAM to
 * checksum data that is already on flash in the right order. */
uint32_t artwork_crc32_cont(uint32_t crc, const uint8_t *data, size_t len);

/* Final complement. Must be applied exactly once, at the end of a `_cont` chain. */
uint32_t artwork_crc32_finish(uint32_t crc);

/* ------------------------------------------------------------------ decompression ----
 *
 * DEFLATE IS NOT DECLARED HERE ON PURPOSE. The inflater is tinfl from the ESP32 ROM (see
 * components/api/api_store.c), which does not exist on the host this library must also build for.
 * Declaring it in this header would force every host test that includes artwork.h to carry a
 * device-only symbol. lib/upload is pure: validation, slot selection, table lookup and checksums —
 * all host-testable (NFR-6) — and the ROM half lives in the IDF component, the same split the
 * bitmap store uses. The strip geometry above is the contract the two halves share. */
