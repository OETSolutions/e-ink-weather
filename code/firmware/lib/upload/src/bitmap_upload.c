#include "bitmap_upload.h"
#include <string.h>

uint32_t upload_crc32(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!data) return crc;
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int k = 0; k < 8; k++) {
            /* Reflected CRC-32: shift right, xor the reversed polynomial when the low bit
             * was set. This is what zlib crc32() and every JS implementation produce. */
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

upload_result_t upload_begin(upload_session_t *s, uint32_t total)
{
    if (!s) return UPLOAD_ERR_NO_SESSION;
    if (s->active) return UPLOAD_ERR_ALREADY_ACTIVE;
    if (total != BITMAP_UPLOAD_TOTAL) return UPLOAD_ERR_BAD_TOTAL;

    s->active = 1;
    s->total = total;
    s->received = 0;
    s->crc = 0;      /* upload_crc32() complements internally, so the seed is 0 */
    return UPLOAD_OK;
}

upload_result_t upload_chunk(upload_session_t *s, uint32_t offset,
                             const uint8_t *data, uint32_t len)
{
    if (!s || !s->active) return UPLOAD_ERR_NO_SESSION;
    if (!data || len == 0 || len > BITMAP_UPLOAD_MAX_CHUNK) return UPLOAD_ERR_BAD_LENGTH;

    /* Exact continuation only: rejects out-of-order, duplicate, overlapping and gapped
     * chunks with one comparison. */
    if (offset != s->received) return UPLOAD_ERR_BAD_OFFSET;
    /* Overflow guard: a chunk that would run past the end must not be written, or the last
     * slot bytes would be scribbled over by a malformed final chunk. */
    if ((uint64_t)offset + len > s->total) return UPLOAD_ERR_BAD_OFFSET;

    s->crc = upload_crc32(s->crc, data, len);
    s->received += len;
    return UPLOAD_OK;
}

upload_result_t upload_commit(upload_session_t *s, uint32_t crc)
{
    if (!s || !s->active) return UPLOAD_ERR_NO_SESSION;

    upload_result_t r = UPLOAD_OK;
    if (s->received != s->total) r = UPLOAD_ERR_INCOMPLETE;
    else if (s->crc != crc) r = UPLOAD_ERR_CHECKSUM;

    /* Close either way: a failed upload must not be resumed into a passing one. */
    s->active = 0;
    return r;
}

void upload_abort(upload_session_t *s)
{
    if (!s) return;
    s->active = 0;
    s->received = 0;
    s->crc = 0;
}
