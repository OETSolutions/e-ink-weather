#include "artwork.h"
#include <string.h>

/* The pure half of the per-page artwork store: validation, slot selection and table lookup.
 * All logic, no flash and no ROM, so it is host-testable (NFR-6) — and it is the half that
 * decides whether a torn promote can put a wrong picture on the glass. */

int artwork_hdr_valid(const artwork_hdr_t *h)
{
    if (!h) return 0;
    if (h->magic != ARTWORK_MAGIC) return 0;
    if (h->seq == 0) return 0;                       /* never written */
    if (h->page_count == 0 || h->page_count > ARTWORK_MAX_PAGES) return 0;
    return 1;
}

int artwork_spare_slot(int live)
{
    /* Writing over the live slot would destroy the only good artwork, so the spare is always
     * the other one. An unknown/absent live slot resolves to A, which matches artwork_pick_slot's
     * "neither valid -> A" convention. */
    return (live == 0) ? 1 : 0;
}

int artwork_pick_slot(const artwork_hdr_t *a, const artwork_hdr_t *b)
{
    const int va = artwork_hdr_valid(a);
    const int vb = artwork_hdr_valid(b);
    if (!va && !vb) return -1;                       /* nothing stored yet */
    if (va && !vb) return 0;
    if (vb && !va) return 1;
    /* Both valid: the HIGHER sequence wins, so a promote is what makes the new set live. */
    return (b->seq > a->seq) ? 1 : 0;
}

int artwork_entry_at(const artwork_hdr_t *h, const artwork_entry_t *entries,
                     uint32_t page, artwork_entry_t *out)
{
    if (!h || !entries || !out) return -1;
    if (page >= h->page_count || page >= ARTWORK_MAX_PAGES) return -1;
    *out = entries[page];
    /* ONE size check covers both failure modes, and that is deliberate rather than a saving.
     *
     *   raw_len == 0          "this page has no artwork" — the web app pushed artwork for fewer
     *                         pages than the config has.
     *   raw_len != 78200      a wrong-sized layer, which would inflate to garbage.
     *
     * Both must refuse to render. Falling back to ANOTHER page's layer is the specific bug this
     * module exists to prevent: the device used to have one shared layer and rotate pages, so a
     * page's readings were stamped onto a different page's labels — seen on hardware. A blank
     * layer is a missing picture; the wrong layer is a lie about which page you are looking at.
     *
     * A separate `raw_len == 0` branch was written first and then removed: it could never be
     * reached, because 0 != ARTWORK_RAW_LEN. An unreachable branch is not defensive, it is a
     * line no test can cover and the next reader has to prove dead. */
    if (out->raw_len != ARTWORK_RAW_LEN) return -1;
    if (out->comp_len == 0 || out->comp_len > ARTWORK_MAX_COMP) return -1;
    return 0;
}

size_t artwork_blob_offset(void)
{
    return sizeof(artwork_hdr_t) + sizeof(artwork_entry_t) * ARTWORK_MAX_PAGES;
}

void artwork_chunk_placement(uint32_t offset, uint32_t len,
                             uint32_t *to_hdr, uint32_t *hdr_off, uint32_t *flash_off)
{
    const uint32_t hdr_len = (uint32_t)sizeof(artwork_hdr_t);
    uint32_t h = 0;
    uint32_t ho = 0;
    uint32_t fo = 0;

    if (offset < hdr_len) {
        /* The chunk starts inside the header. Divert the bytes that fall within the header to the
         * RAM copy, positioned at their true offset so a chunk straddling the boundary splices
         * correctly. */
        const uint32_t room = hdr_len - offset;
        h = (len < room) ? len : room;
        ho = offset;
    }
    if (h < len) {
        /* Everything else goes to flash at its literal slot offset — no shift, because the client
         * already places the table at artwork_blob_offset()'s base. */
        fo = offset + h;
    }

    if (to_hdr) *to_hdr = h;
    if (hdr_off) *hdr_off = ho;
    if (flash_off) *flash_off = fo;
}

int artwork_make_hdr(const artwork_entry_t *entries, uint32_t n_pages,
                     const uint8_t *blob, uint32_t blob_len,
                     uint32_t live_seq, artwork_hdr_t *out)
{
    if (!entries || !out) return -1;
    if (n_pages == 0 || n_pages > ARTWORK_MAX_PAGES) return -1;

    /* Refuse rather than truncate: a stream clipped at the boundary inflates to garbage, and
     * garbage on the panel is worse than no artwork at all. */
    for (uint32_t i = 0; i < n_pages; i++) {
        if (entries[i].comp_len > ARTWORK_MAX_COMP) return -1;
        if (entries[i].raw_len != 0 && entries[i].raw_len != ARTWORK_RAW_LEN) return -1;
        if (entries[i].offset + entries[i].comp_len > blob_len) return -1;
    }

    out->magic = ARTWORK_MAGIC;
    out->page_count = n_pages;
    out->seq = live_seq + 1;                         /* wins on the next boot */
    /* CRC over the table AND the blob together, so a torn write to either is caught. The
     * continuing form avoids staging both into one buffer: the blob is already laid out in the
     * order it will be written, and building a copy would cost up to 4 KB x pages of RAM on a
     * part that has none to spare (NFR-2). */
    uint32_t crc = artwork_crc32_cont(0xFFFFFFFFu,
                                      (const uint8_t *)entries,
                                      sizeof(artwork_entry_t) * n_pages);
    if (blob && blob_len) crc = artwork_crc32_cont(crc, blob, blob_len);
    out->crc = artwork_crc32_finish(crc);
    return 0;
}

uint32_t artwork_crc32(const uint8_t *data, size_t len)
{
    return artwork_crc32_finish(artwork_crc32_cont(0xFFFFFFFFu, data, len));
}

/* Continuing form, so a checksum can span the table and the blob without a staging buffer. */
uint32_t artwork_crc32_cont(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc & 1u) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
    }
    return crc;
}

uint32_t artwork_crc32_finish(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}
