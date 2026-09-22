#pragma once

#include <stddef.h>
#include <stdint.h>

/* A byte buffer stored as a fixed array of segments.
 *
 * WHY THIS EXISTS. The static layer is 78,200 bytes and this part (no PSRAM) has exactly ONE
 * DRAM region large enough to hold it — measured, 113,840 bytes; every other region caps at
 * ~64 KB. When the HTTP API is live (USB, the mains deployment) that region is 92% free yet its
 * largest hole is persistently BELOW 78,200: a few hundred bytes of network-stack allocations
 * split it into pieces such as 18,564 + 30,436 + 55,456, and they never free, so no wait can
 * coalesce them. The result is that the layer cannot be allocated and the render fails while the
 * fetches keep working — the panel keeps a stale image and every save appears to do nothing.
 *
 * Held as segments instead, the same 78,200 bytes come from ~4 KB pieces, which are always
 * available however the region is fragmented. Measured on the bench at the exact moment a single
 * 78,200-byte malloc failed (largest free block 69,632), twenty 4 KB pieces allocated
 * successfully.
 *
 * The arithmetic is the part worth testing: an off-by-one here reads the wrong rows into a band,
 * which leaves a stripe of a previous picture on the glass and reports success — the same class
 * of silent failure the band renderer exists to avoid. So the offset math lives here, pure, and
 * is host-tested (NFR-6). */

/* Enough for the worst case: 78,200 bytes in 1 KB segments is 77 (+1 for rounding headroom).
 * The count is generous on purpose — the allocator below tries LARGE segments first and falls
 * back to smaller ones, and the smaller the segments the more of them are needed. */
#define SEGBUF_MAX_SEGS 80

typedef struct {
    uint8_t *seg[SEGBUF_MAX_SEGS];
    int      n;             /* how many segments are allocated and in use */
    size_t   seg_bytes;     /* the nominal size of each segment */
    size_t   total;         /* the logical length the segments cover */
} segbuf_t;

/* How many segments cover `total` bytes at `seg_bytes` each. */
int segbuf_segs_needed(size_t total, size_t seg_bytes);

/* The size of segment `i` for a buffer of `total` bytes at `seg_bytes` each: full for every
 * segment but the last, which holds the remainder. Returns 0 if `i` is out of range. */
size_t segbuf_seg_len(size_t total, size_t seg_bytes, int i);

/* Forget the segment layout. Does NOT free — the caller owns the segment pointers and must free
 * them (it may have allocated them with a capability-specific allocator). */
void segbuf_reset(segbuf_t *b);

/* Read `len` bytes at `off` into `dst`. Returns 0 on success, -1 if the range runs past the end
 * of the buffer, the layout does not cover it, or an argument is NULL. A partial read is never
 * performed: a caller relying on a band it did not fully receive must fail, not silently draw
 * stale bytes. */
int segbuf_read(const segbuf_t *b, size_t off, uint8_t *dst, size_t len);

/* Write `len` bytes from `src` at `off`. Same bounds contract as segbuf_read. */
int segbuf_write(segbuf_t *b, size_t off, const uint8_t *src, size_t len);

/* The allocator the buffer uses for its segments. Passed in so the sizing/fallback logic below is
 * host-testable with a plain malloc and the firmware can pass one that requests internal DRAM. */
typedef void *(*segbuf_alloc_fn)(size_t bytes);
typedef void  (*segbuf_free_fn)(void *p);

/* Allocate `total` bytes as a set of segments.
 *
 * SEGMENT SIZE IS FOUND BY TRIAL, halving from `seg_bytes` down to `seg_floor`, exactly as the
 * band buffers are. The reason is the same: this part's one large region is fragmented by the live
 * HTTP API into pieces of varying size, and a fixed segment size that happens to fit today may not
 * tomorrow. Smaller segments mean more of them (SEGBUF_MAX_SEGS bounds the count) but each is
 * easier to place, so the render degrades in speed rather than failing outright.
 *
 * On success 0 is returned and `b` describes the allocation. On failure -1 is returned and every
 * partially-taken segment is given back, so a failed call leaks nothing — a leak here would be
 * memory the next TLS handshake needs, which is the failure mode this whole module exists to
 * remove. */
int segbuf_alloc(segbuf_t *b, size_t total, size_t seg_bytes, size_t seg_floor,
                 segbuf_alloc_fn alloc, segbuf_free_fn release);

/* Return every segment to `release` and mark the buffer empty. */
void segbuf_free(segbuf_t *b, segbuf_free_fn release);
