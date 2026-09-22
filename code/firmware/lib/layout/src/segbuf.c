#include "segbuf.h"

#include <string.h>

int segbuf_segs_needed(size_t total, size_t seg_bytes)
{
    if (seg_bytes == 0) return -1;
    if (total == 0) return 0;
    /* Rounded UP: the last segment holds the remainder, which may be shorter than seg_bytes. */
    return (int)((total + seg_bytes - 1) / seg_bytes);
}

size_t segbuf_seg_len(size_t total, size_t seg_bytes, int i)
{
    if (seg_bytes == 0 || i < 0) return 0;
    const size_t start = (size_t)i * seg_bytes;
    if (start >= total) return 0;
    const size_t remain = total - start;
    return (remain < seg_bytes) ? remain : seg_bytes;
}

void segbuf_reset(segbuf_t *b)
{
    if (!b) return;
    /* The pointers are deliberately left in place: the caller owns them and frees them with the
     * allocator it used. Clearing them here would lose the only handle on the memory. */
    b->n = 0;
    b->seg_bytes = 0;
    b->total = 0;
}

int segbuf_read(const segbuf_t *b, size_t off, uint8_t *dst, size_t len)
{
    if (!b || !dst) return -1;
    if (len == 0) return 0;
    /* A read that runs past the logical end is refused rather than clipped: a caller that asked
     * for a band and got a short one would draw stale bytes and report success. */
    if (off > b->total || len > b->total - off) return -1;

    size_t done = 0;
    while (done < len) {
        const size_t cur = off + done;
        const int si = (int)(cur / b->seg_bytes);
        if (si < 0 || si >= b->n || !b->seg[si]) return -1;
        const size_t within = cur - (size_t)si * b->seg_bytes;
        const size_t seglen = segbuf_seg_len(b->total, b->seg_bytes, si);
        if (within >= seglen) return -1;
        size_t take = seglen - within;
        if (take > len - done) take = len - done;
        memcpy(dst + done, b->seg[si] + within, take);
        done += take;
    }
    return 0;
}

int segbuf_write(segbuf_t *b, size_t off, const uint8_t *src, size_t len)
{
    if (!b || !src) return -1;
    if (len == 0) return 0;
    if (off > b->total || len > b->total - off) return -1;

    size_t done = 0;
    while (done < len) {
        const size_t cur = off + done;
        const int si = (int)(cur / b->seg_bytes);
        if (si < 0 || si >= b->n || !b->seg[si]) return -1;
        const size_t within = cur - (size_t)si * b->seg_bytes;
        const size_t seglen = segbuf_seg_len(b->total, b->seg_bytes, si);
        if (within >= seglen) return -1;
        size_t take = seglen - within;
        if (take > len - done) take = len - done;
        memcpy(b->seg[si] + within, src + done, take);
        done += take;
    }
    return 0;
}

int segbuf_alloc(segbuf_t *b, size_t total, size_t seg_bytes, size_t seg_floor,
                 segbuf_alloc_fn alloc, segbuf_free_fn release)
{
    if (!b || !alloc || !release) return -1;
    if (total == 0 || seg_bytes == 0) return -1;
    if (seg_floor == 0) seg_floor = 1;

    /* Halve the segment size until a full set fits, or the floor is reached. The floor is checked
     * before the attempt, not after, so a seg_bytes below the floor is never tried at all. */
    size_t sz = seg_bytes;
    for (; sz >= seg_floor; sz /= 2) {
        const int need = segbuf_segs_needed(total, sz);
        if (need < 0 || need > SEGBUF_MAX_SEGS) {
            /* Too many pieces for the fixed array: a larger segment is required, which this loop
             * cannot produce (it only shrinks). Fail rather than overflow b->seg. */
            return -1;
        }

        segbuf_reset(b);
        b->seg_bytes = sz;
        b->total = total;
        int ok = 1;
        for (int i = 0; i < need; i++) {
            const size_t n = segbuf_seg_len(total, sz, i);
            b->seg[i] = (uint8_t *)alloc(n);
            if (!b->seg[i]) { ok = 0; break; }
            b->n = i + 1;
        }
        if (ok) return 0;

        /* This size did not fit. Give everything back before trying the next size down — a leak
         * here is exactly the memory the TLS handshake needs. */
        segbuf_free(b, release);
    }
    return -1;
}

void segbuf_free(segbuf_t *b, segbuf_free_fn release)
{
    if (!b || !release) return;
    for (int i = 0; i < b->n; i++) {
        if (b->seg[i]) {
            release(b->seg[i]);
            b->seg[i] = NULL;
        }
    }
    b->n = 0;
    b->seg_bytes = 0;
    b->total = 0;
}
