#include "api_store.h"
#include "bitmap_slot.h"
#include "bitmap_upload.h"   /* BITMAP_UPLOAD_MAX_CHUNK and the shared upload_crc32 */
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

static const cfg_store_t s_nvs = { .read = nvs_read, .write = nvs_write, .ctx = NULL };

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
