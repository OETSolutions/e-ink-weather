#include "api_store.h"
#include "bitmap_slot.h"
#include "bitmap_upload.h"   /* BITMAP_UPLOAD_MAX_CHUNK and the shared upload_crc32 */
#include "artwork.h"
#include "miniz.h"          /* the ROM's inflater — see artwork_inflate() below */
#include "esp_log.h"
#include "esp_partition.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "api_store";

/* ---------------------------------------------------------------- NVS config store --- */

#define CFG_NAMESPACE "devcfg"
#define CFG_KEY       "config"

static int nvs_read(void *ctx, const char *key, void *out, size_t max, size_t *len)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return -1;

    size_t sz = max;
    esp_err_t e = nvs_get_blob(h, key, out, &sz);
    nvs_close(h);
    if (e != ESP_OK) return -1;

    /* NUL-terminate so the result is a C string, as cfg_store_get() promises. A blob that
     * exactly fills the buffer cannot be terminated and is refused rather than handed back
     * unterminated. */
    if (sz >= max) return -1;
    ((char *)out)[sz] = '\0';
    if (len) *len = sz;
    return 0;
}

/* How many bytes the stored blob is, without reading it. 0 means "not stored" or "unknown",
 * and cfg_store_get() then falls back to the maximum. nvs_get_blob with a NULL out pointer and
 * a zeroed size returns ESP_ERR_NVS_INVALID_LENGTH while filling `sz` with the real length —
 * that is the documented way to ask, and it costs no allocation. */
static size_t nvs_size(void *ctx, const char *key)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;
    size_t sz = 0;
    const esp_err_t e = nvs_get_blob(h, key, NULL, &sz);
    nvs_close(h);
    return e == ESP_OK ? sz : 0;
}

static int nvs_write(void *ctx, const char *key, const void *data, size_t len)
{
    (void)ctx;
    nvs_handle_t h;
    if (nvs_open(CFG_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed");
        return -1;
    }
    /* nvs_set_blob is atomic with nvs_commit: a power cut either leaves the old value or
     * the new one, never a torn write. That is why the config store does not need the
     * two-slot dance the bitmap does. */
    esp_err_t e = nvs_set_blob(h, key, data, len);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs write failed: %s", esp_err_to_name(e));
        return -1;
    }
    return 0;
}

static const cfg_store_t s_nvs = { .read = nvs_read, .write = nvs_write, .size = nvs_size,
                                   .ctx = NULL };

const cfg_store_t *cfg_store_nvs(void)
{
    return &s_nvs;
}

/* ------------------------------------------------------------------ bitmap slots --- */

static const esp_partition_t *slot_part(bitmap_slot_id_t id)
{
    const char *label = (id == BITMAP_SLOT_A) ? "bitmap_a" : "bitmap_b";
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, label);
}

/* Read one slot's header. Returns 0 on success, -1 if the partition is missing or the read
 * failed. A failed header read leaves `out` zeroed, which bitmap_slot_hdr_valid() rejects —
 * so an unreadable slot can never be selected as live. */
static int read_hdr(bitmap_slot_id_t id, bitmap_slot_hdr_t *out)
{
    memset(out, 0, sizeof(*out));
    const esp_partition_t *p = slot_part(id);
    if (!p) return -1;
    return esp_partition_read(p, 0, out, sizeof(*out)) == ESP_OK ? 0 : -1;
}

static int bitmap_store_live_slot_internal(uint32_t *seq_out)
{
    bitmap_slot_hdr_t a, b;
    if (read_hdr(BITMAP_SLOT_A, &a) != 0) memset(&a, 0, sizeof(a));
    if (read_hdr(BITMAP_SLOT_B, &b) != 0) memset(&b, 0, sizeof(b));

    const bitmap_slot_id_t live = bitmap_slot_pick(&a, &b);
    const bitmap_slot_hdr_t *h = (live == BITMAP_SLOT_A) ? &a : &b;
    if (seq_out) *seq_out = h->seq;
    return bitmap_slot_hdr_valid(h) ? (int)live : -1;
}

int bitmap_store_live_slot(void)
{
    return bitmap_store_live_slot_internal(NULL);
}

int bitmap_store_load(uint8_t *out)
{
    if (!out) return -1;

    const int live = bitmap_store_live_slot_internal(NULL);
    if (live < 0) {
        ESP_LOGI(TAG, "no valid bitmap in either slot");
        return -1;
    }

    bitmap_slot_hdr_t h;
    read_hdr((bitmap_slot_id_t)live, &h);
    const esp_partition_t *p = slot_part((bitmap_slot_id_t)live);
    if (!p) return -1;

    if (esp_partition_read(p, sizeof(h), out, BITMAP_SLOT_LEN) != ESP_OK) {
        ESP_LOGE(TAG, "slot %d read failed", live);
        return -1;
    }
    /* Re-verify the CRC over what was actually read. The header was validated earlier, so a
     * mismatch here means the partition itself is damaged — the image must not be shown. */
    if (bitmap_slot_crc32(out, BITMAP_SLOT_LEN) != h.crc) {
        ESP_LOGE(TAG, "slot %d CRC mismatch on read", live);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------- bitmap slots: streaming ---- */

/* The in-flight upload. Only one at a time, owned by the HTTP server task. */
static struct {
    int                 active;
    bitmap_slot_id_t    slot;
    const esp_partition_t *part;
    uint32_t            written;
    uint32_t            live_seq;
} s_up;

static int write_hdr(const esp_partition_t *p, const bitmap_slot_hdr_t *h)
{
    if (esp_partition_write(p, 0, h, sizeof(*h)) != ESP_OK) return -1;
    return 0;
}

static int invalidate_hdr(const esp_partition_t *p)
{
    bitmap_slot_hdr_t zero;
    memset(&zero, 0, sizeof(zero));
    /* This is the one write that can fail and still leave the device safe: a header that is
     * already zero stays zero, and any other outcome is caught by the CRC check on read. */
    return write_hdr(p, &zero);
}

int bitmap_store_begin_upload(void)
{
    memset(&s_up, 0, sizeof(s_up));

    uint32_t live_seq = 0;
    const int live = bitmap_store_live_slot_internal(&live_seq);

    /* No live image means slot A is "live" by convention with seq 0, so the spare is B and
     * the new image gets seq 1. Nothing is lost: A holds nothing valid. */
    const bitmap_slot_id_t spare =
        bitmap_slot_spare(live < 0 ? BITMAP_SLOT_A : (bitmap_slot_id_t)live);

    const esp_partition_t *p = slot_part(spare);
    if (!p) {
        ESP_LOGE(TAG, "partition for spare slot %d not found", (int)spare);
        return -1;
    }
    if (p->size < sizeof(bitmap_slot_hdr_t) + BITMAP_SLOT_LEN) {
        ESP_LOGE(TAG, "slot %d too small (%u bytes)", (int)spare, (unsigned)p->size);
        return -1;
    }

    /* The whole partition is erased up front, not per chunk: esp_partition_erase_range
     * needs a sector-aligned size and 78,216 is not one, and erasing 5 sectors once is
     * cheaper than aligning per chunk. Zeroing the header here is also what makes an
     * interrupted upload safe — the slot is INVALID from this moment until the final header
     * write, so bitmap_slot_pick() ignores it no matter where the transfer stops. */
    if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) {
        ESP_LOGE(TAG, "erase of slot %d failed", (int)spare);
        return -1;
    }

    s_up.active = 1;
    s_up.slot = spare;
    s_up.part = p;
    s_up.written = 0;
    s_up.live_seq = live_seq;
    return 0;
}

int bitmap_store_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (!s_up.active || !data) return -1;
    if (len == 0 || len > BITMAP_UPLOAD_MAX_CHUNK) return -1;
    /* Defence in depth: the upload state machine already rejects a non-consecutive offset,
     * but writing a chunk at the wrong place would corrupt the image silently, so the
     * condition is checked again at the point of the write. */
    if (offset != s_up.written) return -1;
    if ((uint64_t)offset + len > BITMAP_SLOT_LEN) return -1;

    const size_t base = sizeof(bitmap_slot_hdr_t) + offset;
    if (esp_partition_write(s_up.part, base, data, len) != ESP_OK) {
        ESP_LOGE(TAG, "write at %u failed", (unsigned)offset);
        return -1;
    }
    s_up.written += len;
    return 0;
}

int bitmap_store_finish_upload(uint32_t crc)
{
    if (!s_up.active) return -1;
    const esp_partition_t *p = s_up.part;
    s_up.active = 0;

    if (s_up.written != BITMAP_SLOT_LEN) {
        ESP_LOGE(TAG, "promote with %u of %u bytes written",
                 (unsigned)s_up.written, (unsigned)BITMAP_SLOT_LEN);
        invalidate_hdr(p);
        return -1;
    }

    /* Read the image back and re-check it before advertising it. Flash writes can fail
     * silently, and promoting a damaged image is worse than refusing it: the panel would
     * show corruption with no way to recover except a re-upload.
     *
     * Read in windows rather than into one 78,200-byte buffer — the same RAM argument as
     * streaming the upload. BITMAP_SLOT_LEN (78,200) is 76 full 1024-byte windows plus a
     * 376-byte tail, so the last read MUST be clamped; an unclamped one would read past the
     * frame and fold trailing bytes into the CRC, rejecting every good upload. The CRC is
     * folded window by window through the shared running implementation, which produces the
     * same value the client computed over the whole image in one pass. */
    static uint8_t window[1024];
    uint32_t running = 0;
    for (size_t off = 0; off < BITMAP_SLOT_LEN; off += sizeof(window)) {
        size_t n = sizeof(window);
        if (off + n > BITMAP_SLOT_LEN) n = BITMAP_SLOT_LEN - off;
        if (esp_partition_read(p, sizeof(bitmap_slot_hdr_t) + off, window, n) != ESP_OK) {
            ESP_LOGE(TAG, "read-back failed at %u", (unsigned)off);
            invalidate_hdr(p);
            return -1;
        }
        running = upload_crc32(running, window, n);
    }
    if (running != crc) {
        ESP_LOGE(TAG, "read-back CRC %08x != client %08x; NOT promoting",
                 (unsigned)running, (unsigned)crc);
        invalidate_hdr(p);
        return -1;
    }

    /* Build the header from the CLIENT's CRC rather than re-computing it from the image.
     * They have just been proven equal, and using one value here means the stored CRC can
     * never disagree with the one that was verified. */
    bitmap_slot_hdr_t hdr = {
        .magic = BITMAP_SLOT_MAGIC,
        .len   = BITMAP_SLOT_LEN,
        .crc   = crc,
        /* +1 wins the next bitmap_slot_pick(); wrapping to 0 would look like "never written"
         * and silently fail to promote. */
        .seq   = (s_up.live_seq + 1 == 0) ? 1u : s_up.live_seq + 1,
    };
    if (write_hdr(p, &hdr) != 0) {
        ESP_LOGE(TAG, "header write failed");
        invalidate_hdr(p);
        return -1;
    }

    ESP_LOGI(TAG, "promoted slot %d seq %u crc %08x",
             (int)s_up.slot, (unsigned)hdr.seq, (unsigned)hdr.crc);
    return 0;
}

void bitmap_store_abort_upload(void)
{
    if (!s_up.active) return;
    invalidate_hdr(s_up.part);
    s_up.active = 0;
}

/* ============================================================ per-page artwork (FR-15) =====
 *
 * A SET of compressed static layers, one per page, stored in its OWN pair of raw partitions
 * (artwork_a/artwork_b — see partitions.csv). The pure decisions (validation, slot selection,
 * table lookup, checksum spanning) live in lib/upload/artwork.c and are host-tested; this is the
 * flash and ROM half.
 *
 * WHY ITS OWN PAIR, AND NOT THE BITMAP SLOTS: the two stores were briefly made to share
 * bitmap_a/bitmap_b on the reasoning that both need a ping-pong spare. They cannot. A promote
 * ERASES its spare slot before writing, so sharing a pair means an artwork upload erases the live
 * bitmap and a bitmap upload erases the artwork — each store destroying the other's only good
 * copy. On hardware that presented as artwork_pages: 0 after a push that reported success, with the
 * panel rendering its widgets over the factory boot mark.
 *
 * WHY IT IS HERE AND NOT IN lib/: lib/upload must build for the HOST, where there is no
 * esp_partition and no ROM miniz. Keeping the device half in this IDF component is the same
 * split the bitmap store already uses (api_store.c vs bitmap_slot.c). */

static const esp_partition_t *aw_part(int slot)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40,
                                    (slot == 0) ? "artwork_a" : "artwork_b");
}

static void aw_read_hdr(int slot, artwork_hdr_t *out)
{
    memset(out, 0, sizeof(*out));
    const esp_partition_t *p = aw_part(slot);
    /* A failed read leaves the header zeroed, which artwork_hdr_valid() rejects — so an
     * unreadable slot can never be selected as live. */
    if (p) esp_partition_read(p, 0, out, sizeof(*out));
}

/* The live slot, and its header. Returns -1 when no artwork is stored at all. */
static int aw_live(artwork_hdr_t *hdr_out)
{
    artwork_hdr_t a, b;
    aw_read_hdr(0, &a);
    aw_read_hdr(1, &b);
    const int live = artwork_pick_slot(&a, &b);
    if (live < 0) return -1;
    if (hdr_out) *hdr_out = (live == 0) ? a : b;
    return live;
}

int artwork_store_page_count(void)
{
    artwork_hdr_t h;
    return (aw_live(&h) < 0) ? 0 : (int)h.page_count;
}

int artwork_store_load_page(int page, uint8_t *out)
{
    if (!out || page < 0) return -1;

    artwork_hdr_t h;
    const int live = aw_live(&h);
    if (live < 0) {
        ESP_LOGI(TAG, "no artwork stored; page %d renders on a blank layer", page);
        return -1;
    }
    const esp_partition_t *p = aw_part(live);
    if (!p) return -1;

    /* Read the WHOLE entry table (8 entries, ~96 bytes) and let the pure module apply the rules.
     * Reading only this page's entry and open-coding the checks here would put the same rules in
     * two places — and the one that matters is "a page with no artwork must not inherit a
     * neighbour's", which is exactly what the tests cover in artwork_entry_at(). */
    artwork_entry_t table[ARTWORK_MAX_PAGES];
    memset(table, 0, sizeof(table));
    const uint32_t n = (h.page_count > ARTWORK_MAX_PAGES) ? ARTWORK_MAX_PAGES : h.page_count;
    if (n == 0) return -1;
    if (esp_partition_read(p, sizeof(artwork_hdr_t), table,
                           sizeof(artwork_entry_t) * n) != ESP_OK) {
        ESP_LOGE(TAG, "artwork table read failed");
        return -1;
    }

    artwork_entry_t e;
    if (artwork_entry_at(&h, table, (uint32_t)page, &e) != 0) {
        ESP_LOGI(TAG, "page %d has no artwork of its own; rendering a blank layer", page);
        return -1;
    }

    /* Read the stream into a static scratch (measured 966 bytes for the shipped layout; the
     * budget is 4 KB) and inflate straight into the caller's 78,200-byte buffer, so this never
     * allocates a layer of its own. */
    static uint8_t comp[ARTWORK_MAX_COMP];
    if (esp_partition_read(p, artwork_blob_offset() + e.offset, comp, e.comp_len) != ESP_OK) {
        ESP_LOGE(TAG, "artwork stream read failed for page %d", page);
        return -1;
    }
    if (artwork_inflate(comp, e.comp_len, out) != (int)ARTWORK_RAW_LEN) {
        ESP_LOGE(TAG, "artwork inflate failed for page %d (page_count=%u comp_len=%u)",
                 page, (unsigned)h.page_count, (unsigned)e.comp_len);
        return -1;
    }
    return 0;
}

/* --- artwork upload: the same streaming, spare-slot, atomic-promote rule as the bitmap ---
 *
 * The client sends the WHOLE set as one stream — header, entry table, blob — because the header
 * carries the checksum and the table is what the checksum covers. Only the last chunk's request
 * has to match, and until it does the spare slot's header stays invalid, so the previously live
 * artwork remains selected. */

static struct {
    int      active;
    int      slot;
    uint32_t written;
    uint32_t live_seq;
    /* The client's header, held in RAM rather than written to the slot at offset 0.
     *
     * WHY IT MUST NOT GO TO FLASH AT OFFSET 0: flash cells only erase to 1 and program to 0, so
     * a location can be cleared but never SET. The client sends seq = 0 (it cannot know the live
     * sequence), and the promote then writes the real seq (1, 2, ...) to the same word — which is
     * 0x00000000 -> 0x00000001 for the first push, and that bit cannot be set. The write reports
     * ESP_OK, the header stays at seq 0, and artwork_hdr_valid() reads it as "never written", so
     * EVERY subsequent lookup says "no artwork stored" while the upload reports success. Seen on
     * hardware, with the slot read back showing TEGP plus a seq of 0.
     *
     * The same hazard applies to crc: a later set with a larger crc would need bits set. So the
     * header is never flashed by the client at all. Only the table and the blob are streamed to
     * flash, at their true offsets, and the device writes its own header once into the erased
     * first 16 bytes at promote — exactly the discipline the bitmap slot already uses. */
    artwork_hdr_t hdr;
} s_aw;

int artwork_store_begin_upload(void)
{
    if (s_aw.active) return -1;

    artwork_hdr_t live_h;
    const int live = aw_live(&live_h);
    const int spare = artwork_spare_slot(live < 0 ? 0 : live);

    const esp_partition_t *p = aw_part(spare);
    if (!p) return -1;

    /* Erase the WHOLE spare slot, not just the region about to be written. A stale entry table
     * from an earlier, longer artwork set would otherwise still be readable, and the slot's
     * header must read as invalid until the promote writes it. */
    if (esp_partition_erase_range(p, 0, p->size) != ESP_OK) {
        ESP_LOGE(TAG, "artwork erase failed");
        return -1;
    }

    s_aw.active = 1;
    s_aw.slot = spare;
    s_aw.written = 0;
    s_aw.live_seq = (live < 0) ? 0 : live_h.seq;
    /* Cleared so the header assembled from the incoming chunks cannot inherit bytes from a
     * previously abandoned upload. */
    memset(&s_aw.hdr, 0, sizeof(s_aw.hdr));
    return 0;
}

int artwork_store_write_chunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if (!s_aw.active || !data || len == 0) return -1;
    /* Consecutive writes only, the same contract as the bitmap upload: a gap or an overlap would
     * place a stream's bytes at the wrong offset, and the entry table would then point at a
     * stream that is half of one picture and half of another. */
    if (offset != s_aw.written) return -1;

    const esp_partition_t *p = aw_part(s_aw.slot);
    if (!p) return -1;

    /* The client's stream is header + table + blob, laid out at the SAME offsets the slot uses,
     * but offset 0 of the slot must stay erased for the device's own header (see the note on
     * s_aw.hdr). artwork_chunk_placement() decides which bytes go to RAM and which to flash; it is
     * pure and host-tested, because getting it wrong is silent (the header lands in flash, the
     * promote's seq write cannot set a bit, and every later read says "never written"). */
    uint32_t to_hdr = 0, hdr_off = 0, flash_off = 0;
    artwork_chunk_placement(offset, len, &to_hdr, &hdr_off, &flash_off);
    if (to_hdr > 0) {
        memcpy((uint8_t *)&s_aw.hdr + hdr_off, data, to_hdr);
    }

    if (to_hdr < len) {
        const uint32_t n = len - to_hdr;
        if (flash_off + n > p->size) return -1;
        if (esp_partition_write(p, flash_off, data + to_hdr, n) != ESP_OK) {
            ESP_LOGE(TAG, "artwork write failed at slot %u", (unsigned)flash_off);
            return -1;
        }
    }
    s_aw.written = offset + len;
    return 0;
}

int artwork_store_finish_upload(uint32_t crc)
{
    if (!s_aw.active) return -1;
    const esp_partition_t *p = aw_part(s_aw.slot);
    if (!p) { s_aw.active = 0; return -1; }

    /* The client's header came in over the wire and is held in RAM (it was never written to the
     * slot — see s_aw.hdr). Sanity-check it before trusting its page_count: it is untrusted
     * input, and a bogus count would make the checksum walk run off the end of the written
     * region. */
    artwork_hdr_t h = s_aw.hdr;
    if (h.page_count == 0 || h.page_count > ARTWORK_MAX_PAGES ||
        s_aw.written < artwork_blob_offset()) {
        ESP_LOGE(TAG, "uploaded artwork header is not usable (pages %u, %u bytes written)",
                 (unsigned)h.page_count, (unsigned)s_aw.written);
        s_aw.active = 0;
        return -1;
    }

    /* Recompute the checksum over exactly what landed in flash, then require that it matches
     * BOTH what the client claimed and what its own header claims. The client's value catches a
     * corrupted transfer; the header's catches a mismatch between the table and the blob that
     * both arrived intact. Either failing means this set must not become live. */
    const uint32_t table_len = (uint32_t)(sizeof(artwork_entry_t) * h.page_count);
    if (sizeof(artwork_hdr_t) + table_len > s_aw.written) { s_aw.active = 0; return -1; }

    uint32_t running = 0xFFFFFFFFu;
    uint32_t got = 0;
    const uint32_t total = s_aw.written - (uint32_t)sizeof(artwork_hdr_t);
    uint8_t buf[512];
    while (got < total) {
        uint32_t n = total - got;
        if (n > sizeof(buf)) n = sizeof(buf);
        if (esp_partition_read(p, sizeof(artwork_hdr_t) + got, buf, n) != ESP_OK) {
            s_aw.active = 0;
            return -1;
        }
        running = artwork_crc32_cont(running, buf, n);
        got += n;
    }
    const uint32_t want = artwork_crc32_finish(running);

    if (want != crc || want != h.crc) {
        ESP_LOGE(TAG, "artwork checksum mismatch (client %08x, header %08x, flash %08x)",
                 (unsigned)crc, (unsigned)h.crc, (unsigned)want);
        /* The OLD artwork stays live for free: the slot's header region was erased at
         * begin_upload and never written, so it still reads as invalid and the previous set
         * remains selected by artwork_pick_slot(). That is the whole point of writing to the
         * spare slot rather than over the live one. */
        s_aw.active = 0;
        return -1;
    }

    /* Now — and only now — write the header that makes this set live. The sequence is one past
     * the live slot's, which is what makes artwork_pick_slot() select it, and it is assigned
     * HERE rather than trusted from the client. */
    h.magic = ARTWORK_MAGIC;
    h.crc = want;
    h.seq = (s_aw.live_seq + 1 == 0) ? 1u : s_aw.live_seq + 1;
    if (esp_partition_write(p, 0, &h, sizeof(h)) != ESP_OK) {
        ESP_LOGE(TAG, "artwork header write failed");
        s_aw.active = 0;
        return -1;
    }

    /* Read the header back and require it to be exactly what was written. The write reporting
     * ESP_OK is not evidence that the bytes are there, and this is the one write in the whole
     * store whose LOSS is silent: the header is what makes the set live, so a promotion that
     * did not land leaves a valid-looking blob that every subsequent read rejects as "never
     * written" — the upload reports success and the panel keeps its old picture. This exact
     * failure was seen on hardware, and it is why the client's header is no longer streamed to
     * offset 0 (a later seq needs bits set, which NOR flash cannot do). The caller notes the
     * failed promote; this log carries the numbers. */
    artwork_hdr_t back;
    if (esp_partition_read(p, 0, &back, sizeof(back)) != ESP_OK ||
        memcmp(&back, &h, sizeof(h)) != 0) {
        ESP_LOGE(TAG, "artwork header did not persist (slot %d): wrote seq %u, read back seq %u",
                 s_aw.slot, (unsigned)h.seq, (unsigned)back.seq);
        s_aw.active = 0;
        return -1;
    }

    ESP_LOGI(TAG, "promoted artwork slot %d seq %u pages %u crc %08x",
             s_aw.slot, (unsigned)h.seq, (unsigned)h.page_count, (unsigned)h.crc);
    s_aw.active = 0;
    return 0;
}

void artwork_store_abort_upload(void)
{
    if (!s_aw.active) return;
    /* No flash writes here, deliberately. The slot's header region was erased at begin_upload and
     * never written (the client's header went to RAM), so it already reads as invalid and the
     * PREVIOUS artwork stays live. Programming a zero header would be pointless and, being a
     * clear-bits-only operation, could not be undone by a later promote. */
    s_aw.active = 0;
}

/* ------------------------------------------------------------------ artwork inflate ------
 *
 * WHY THE ROM AND NOT A BUNDLED INFLATER: this firmware sits at ~80% of its OTA slot and FR-18
 * requires the whole web app be served from the same flash, so ~6 KB of inflate code is worth
 * avoiding. ESP32's ROM exports tinfl_decompress_mem_to_mem() (verified in
 * components/esp_rom/esp32/ld/esp32.rom.ld), the same miniz IDF itself uses, so this costs no
 * flash at all. The COMPRESSOR is the web app's (Node's zlib), so the only agreement needed
 * between the two sides is a format both already implement.
 *
 * The streams are ZLIB (RFC1950), not gzip: TINFL_FLAG_PARSE_ZLIB_HEADER expects a zlib header,
 * and a gzip stream's 1f 8b framing would be rejected. Node's deflateSync emits 78 da for this
 * data, which is what the flag wants — a gzipSync stream would fail here for a reason that has
 * nothing to do with the picture.
 *
 * TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF is passed because the whole 78,200-byte layer lands in
 * one caller-supplied buffer, so there is no ring to wrap and the dictionary is the output.
 * TINFL_FLAG_HAS_MORE_INPUT is deliberately NOT passed: the entire stream is on flash, so a
 * short read must fail outright rather than report "needs more input". */
int artwork_inflate(const uint8_t *comp, size_t comp_len, uint8_t *out)
{
    if (!comp || !out || comp_len == 0) return -1;
    if (comp_len > ARTWORK_MAX_COMP) return -1;

    const size_t got = tinfl_decompress_mem_to_mem(
        out, ARTWORK_RAW_LEN, comp, comp_len,
        TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (got != ARTWORK_RAW_LEN) return -1;
    return (int)got;
}
