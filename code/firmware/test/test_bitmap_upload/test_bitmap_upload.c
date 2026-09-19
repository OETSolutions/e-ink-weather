#include <string.h>
#include "unity.h"
#include "bitmap_upload.h"

void setUp(void) {}
void tearDown(void) {}

/* Known CRC-32 vectors — these pin the algorithm to the standard reflected CRC-32, so a
 * web app using zlib/JS crc32 computes the same value. A device-only checksum variant
 * would reject every honest upload. */
static void test_crc32_matches_known_vectors(void)
{
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, upload_crc32(0, (const uint8_t *)"", 0));
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, upload_crc32(0, (const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0x414FA339u, upload_crc32(0, (const uint8_t *)"The quick brown fox jumps over the lazy dog", 43));
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, upload_crc32(0, NULL, 10));
}

/* Incremental CRC must equal one-shot, or chunked uploads would never verify. */
static void test_crc32_is_incremental(void)
{
    const char *msg = "The quick brown fox jumps over the lazy dog";
    uint32_t one = upload_crc32(0, (const uint8_t *)msg, 43);
    uint32_t inc = 0;
    for (int i = 0; i < 43; i++) inc = upload_crc32(inc, (const uint8_t *)&msg[i], 1);
    TEST_ASSERT_EQUAL_HEX32(one, inc);

    uint32_t inc2 = 0;
    inc2 = upload_crc32(inc2, (const uint8_t *)msg, 7);
    inc2 = upload_crc32(inc2, (const uint8_t *)msg + 7, 36);
    TEST_ASSERT_EQUAL_HEX32(one, inc2);
}

/* A wrong-sized upload must be refused at the FIRST request, not after 78 KB of traffic. */
static void test_begin_rejects_wrong_total(void)
{
    upload_session_t s = {0};
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_TOTAL, upload_begin(&s, 0));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_TOTAL, upload_begin(&s, 78199));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_TOTAL, upload_begin(&s, 78201));
    TEST_ASSERT_EQUAL_INT(0, s.active);
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_begin(&s, BITMAP_UPLOAD_TOTAL));
}

/* Two concurrent sessions would interleave into a corrupt bitmap. */
static void test_begin_refuses_second_session(void)
{
    upload_session_t s = {0};
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_begin(&s, BITMAP_UPLOAD_TOTAL));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_ALREADY_ACTIVE,
                          upload_begin(&s, BITMAP_UPLOAD_TOTAL));
}

static void test_chunk_without_session_is_refused(void)
{
    upload_session_t s = {0};
    uint8_t buf[16] = {0};
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_chunk(&s, 0, buf, 16));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_commit(&s, 0));
}

/* The ordering rule: only an exact continuation is accepted. This one comparison is what
 * rejects out-of-order, duplicate, overlapping AND gapped chunks. */
static void test_out_of_order_and_gapped_chunks_are_rejected(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK];
    memset(buf, 0xAA, sizeof(buf));
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);

    /* A gap: skipping ahead. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_OFFSET, upload_chunk(&s, 4096, buf, 4096));
    /* Backwards: a duplicate / replay. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, 0, buf, 4096));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_OFFSET, upload_chunk(&s, 0, buf, 4096));
    /* Overlapping: starts inside what we already have. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_OFFSET, upload_chunk(&s, 2048, buf, 4096));
    /* Correct continuation still works. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, 4096, buf, 4096));
    TEST_ASSERT_EQUAL_UINT32(8192, s.received);
}

/* A malformed final chunk must not run past the end of the bitmap. */
static void test_chunk_cannot_run_past_the_end(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK];
    memset(buf, 0, sizeof(buf));
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);

    /* Walk to the last full-chunk boundary. 78,200 is not a multiple of 4,096, so this
     * leaves a tail — compute it rather than assuming a round number. */
    uint32_t off = 0;
    while (off + BITMAP_UPLOAD_MAX_CHUNK <= BITMAP_UPLOAD_TOTAL) {
        TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, off, buf, BITMAP_UPLOAD_MAX_CHUNK));
        off += BITMAP_UPLOAD_MAX_CHUNK;
    }
    uint32_t tail = BITMAP_UPLOAD_TOTAL - off;
    TEST_ASSERT_GREATER_THAN_UINT32(0, tail);
    TEST_ASSERT_EQUAL_UINT32(BITMAP_UPLOAD_TOTAL - tail, s.received);

    /* One byte past the tail must overshoot. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_OFFSET, upload_chunk(&s, off, buf, tail + 1));
    TEST_ASSERT_EQUAL_UINT32(BITMAP_UPLOAD_TOTAL - tail, s.received);
    /* The exact tail is fine, and completes the upload. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, off, buf, tail));
    TEST_ASSERT_EQUAL_UINT32(BITMAP_UPLOAD_TOTAL, s.received);
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_commit(&s, s.crc));
}

static void test_bad_chunk_lengths_are_rejected(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK + 1];
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);

    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_LENGTH, upload_chunk(&s, 0, NULL, 16));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_LENGTH, upload_chunk(&s, 0, buf, 0));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_BAD_LENGTH,
                          upload_chunk(&s, 0, buf, BITMAP_UPLOAD_MAX_CHUNK + 1));
    TEST_ASSERT_EQUAL_UINT32(0, s.received);   /* nothing was counted */
}

/* Commit before every byte arrived must fail, and must not leave a resumable session. */
static void test_commit_rejects_incomplete_upload(void)
{
    upload_session_t s = {0};
    uint8_t buf[4096];
    memset(buf, 0x5A, sizeof(buf));
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);
    upload_chunk(&s, 0, buf, 4096);

    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_INCOMPLETE, upload_commit(&s, s.crc));
    TEST_ASSERT_EQUAL_INT(0, s.active);
    /* Cannot be resumed into a good one. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_chunk(&s, 4096, buf, 4096));
}

/* A checksum mismatch must be refused — this is what stops a truncated or corrupted
 * transfer from being promoted to the active bitmap. */
static void test_commit_rejects_bad_checksum(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK];
    memset(buf, 0x11, sizeof(buf));
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);

    uint32_t off = 0;
    while (off < BITMAP_UPLOAD_TOTAL) {
        uint32_t n = BITMAP_UPLOAD_TOTAL - off;
        if (n > BITMAP_UPLOAD_MAX_CHUNK) n = BITMAP_UPLOAD_MAX_CHUNK;
        TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, off, buf, n));
        off += n;
    }
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_CHECKSUM, upload_commit(&s, s.crc ^ 1u));
    TEST_ASSERT_EQUAL_INT(0, s.active);
}

/* The happy path: a full upload with the correct checksum commits. */
static void test_full_upload_commits(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK];
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);

    uint32_t off = 0;
    while (off < BITMAP_UPLOAD_TOTAL) {
        uint32_t n = BITMAP_UPLOAD_TOTAL - off;
        if (n > BITMAP_UPLOAD_MAX_CHUNK) n = BITMAP_UPLOAD_MAX_CHUNK;
        for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)((off + i) & 0xFF);
        TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, off, buf, n));
        off += n;
    }
    TEST_ASSERT_EQUAL_UINT32(BITMAP_UPLOAD_TOTAL, s.received);
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_commit(&s, s.crc));
    TEST_ASSERT_EQUAL_INT(0, s.active);
}

/* An interrupted upload must leave the PREVIOUS bitmap intact. This module's part of that
 * contract: after an abort the session is gone, so no later chunk can land in the slot, and
 * a commit cannot succeed. (The slot itself is never touched until commit returns OK.) */
static void test_interrupted_upload_cannot_later_commit(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK];
    memset(buf, 0x77, sizeof(buf));
    upload_begin(&s, BITMAP_UPLOAD_TOTAL);
    upload_chunk(&s, 0, buf, BITMAP_UPLOAD_MAX_CHUNK);
    upload_chunk(&s, BITMAP_UPLOAD_MAX_CHUNK, buf, BITMAP_UPLOAD_MAX_CHUNK);

    upload_abort(&s);
    TEST_ASSERT_EQUAL_INT(0, s.active);
    TEST_ASSERT_EQUAL_UINT32(0, s.received);
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_chunk(&s, 0, buf, 16));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_commit(&s, 0));
}

/* A new session must start clean, not inherit the previous session's byte count. */
static void test_new_session_after_abort_starts_clean(void)
{
    upload_session_t s = {0};
    uint8_t buf[BITMAP_UPLOAD_MAX_CHUNK];
    memset(buf, 0x33, sizeof(buf));

    upload_begin(&s, BITMAP_UPLOAD_TOTAL);
    upload_chunk(&s, 0, buf, BITMAP_UPLOAD_MAX_CHUNK);
    upload_abort(&s);

    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_begin(&s, BITMAP_UPLOAD_TOTAL));
    TEST_ASSERT_EQUAL_UINT32(0, s.received);
    /* Offset 0 must be accepted again — if `received` had survived the abort, this would
     * be rejected as out of order and the device would be stuck. */
    TEST_ASSERT_EQUAL_INT(UPLOAD_OK, upload_chunk(&s, 0, buf, BITMAP_UPLOAD_MAX_CHUNK));
}

static void test_null_session_is_handled(void)
{
    uint8_t buf[16] = {0};
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_begin(NULL, BITMAP_UPLOAD_TOTAL));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_chunk(NULL, 0, buf, 16));
    TEST_ASSERT_EQUAL_INT(UPLOAD_ERR_NO_SESSION, upload_commit(NULL, 0));
    upload_abort(NULL);   /* must not crash */
}

/* The declared size must match the framebuffer exactly, or a "successful" upload would
 * leave the panel showing part of an image and part of the previous one. */
static void test_upload_total_matches_the_framebuffer(void)
{
    TEST_ASSERT_EQUAL_UINT32(78200u, BITMAP_UPLOAD_TOTAL);
    TEST_ASSERT_EQUAL_UINT32(920u / 8 * 680, BITMAP_UPLOAD_TOTAL);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_matches_known_vectors);
    RUN_TEST(test_crc32_is_incremental);
    RUN_TEST(test_begin_rejects_wrong_total);
    RUN_TEST(test_begin_refuses_second_session);
    RUN_TEST(test_chunk_without_session_is_refused);
    RUN_TEST(test_out_of_order_and_gapped_chunks_are_rejected);
    RUN_TEST(test_chunk_cannot_run_past_the_end);
    RUN_TEST(test_bad_chunk_lengths_are_rejected);
    RUN_TEST(test_commit_rejects_incomplete_upload);
    RUN_TEST(test_commit_rejects_bad_checksum);
    RUN_TEST(test_full_upload_commits);
    RUN_TEST(test_interrupted_upload_cannot_later_commit);
    RUN_TEST(test_new_session_after_abort_starts_clean);
    RUN_TEST(test_null_session_is_handled);
    RUN_TEST(test_upload_total_matches_the_framebuffer);
    return UNITY_END();
}
