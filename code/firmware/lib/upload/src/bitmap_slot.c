#include "bitmap_slot.h"
#include "bitmap_upload.h"   /* one CRC-32 implementation, shared with the upload path */

int bitmap_slot_hdr_valid(const bitmap_slot_hdr_t *h)
{
    if (!h) return 0;
    if (h->magic != BITMAP_SLOT_MAGIC) return 0;
    if (h->len != BITMAP_SLOT_LEN) return 0;
    if (h->seq == 0) return 0;      /* never written */
    return 1;
}

bitmap_slot_id_t bitmap_slot_pick(const bitmap_slot_hdr_t *a,
                                  const bitmap_slot_hdr_t *b)
{
    int va = bitmap_slot_hdr_valid(a);
    int vb = bitmap_slot_hdr_valid(b);

    if (!va && !vb) return BITMAP_SLOT_A;   /* defined answer; caller reports "no bitmap" */
    if (va && !vb) return BITMAP_SLOT_A;
    if (!va && vb) return BITMAP_SLOT_B;

    /* Both valid: higher sequence wins. The comparison is on uint32_t so a wrap is
     * well-defined; ties favour A, which keeps the choice deterministic. */
    return (b->seq > a->seq) ? BITMAP_SLOT_B : BITMAP_SLOT_A;
}

bitmap_slot_id_t bitmap_slot_spare(bitmap_slot_id_t live)
{
    return (live == BITMAP_SLOT_A) ? BITMAP_SLOT_B : BITMAP_SLOT_A;
}

int bitmap_slot_make_hdr(const uint8_t *data, uint32_t len, uint32_t live_seq,
                         bitmap_slot_hdr_t *out)
{
    if (!data || !out) return -1;
    if (len != BITMAP_SLOT_LEN) return -1;

    out->magic = BITMAP_SLOT_MAGIC;
    out->len = len;
    out->crc = bitmap_slot_crc32(data, len);
    /* +1 makes this header win the next `bitmap_slot_pick`. Wrapping from 0xFFFFFFFF to 0
     * would produce seq == 0, which `bitmap_slot_hdr_valid` rejects as "never written" — so
     * the promote would silently not take effect. Start the wrap at 1 instead. */
    uint32_t next = live_seq + 1;
    if (next == 0) next = 1;
    out->seq = next;
    return 0;
}

uint32_t bitmap_slot_crc32(const uint8_t *data, size_t len)
{
    return upload_crc32(0, data, len);
}
