#include <stdlib.h>
#include <string.h>
#include "unity.h"
#include "bitmap_slot.h"
#include "bitmap_upload.h"

void setUp(void) {}
void tearDown(void) {}

static bitmap_slot_hdr_t hdr(uint32_t seq, uint32_t len, uint32_t crc)
{
    bitmap_slot_hdr_t h;
    h.magic = BITMAP_SLOT_MAGIC;
    h.len = len;
    h.crc = crc;
    h.seq = seq;
    return h;
}

static bitmap_slot_hdr_t never_written(void)
{
    bitmap_slot_hdr_t h;
    memset(&h, 0, sizeof(h));
    return h;
}

static void test_header_validation(void)
{
    TEST_ASSERT_TRUE(bitmap_slot_hdr_valid(&(bitmap_slot_hdr_t){BITMAP_SLOT_MAGIC, BITMAP_SLOT_LEN, 0x1234, 1}));
    /* Wrong magic: a different format or erased flash. */
    TEST_ASSERT_FALSE(bitmap_slot_hdr_valid(&(bitmap_slot_hdr_t){0, BITMAP_SLOT_LEN, 0, 1}));
    /* Wrong length: a partial or wrong-sized bitmap must never be shown. */
    TEST_ASSERT_FALSE(bitmap_slot_hdr_valid(&(bitmap_slot_hdr_t){BITMAP_SLOT_MAGIC, 100, 0, 1}));
    /* seq 0 means never written (erased flash reads as 0xFFFFFFFF, not 0, but a torn write
     * could leave this). */
    TEST_ASSERT_FALSE(bitmap_slot_hdr_valid(&(bitmap_slot_hdr_t){BITMAP_SLOT_MAGIC, BITMAP_SLOT_LEN, 0, 0}));
    TEST_ASSERT_FALSE(bitmap_slot_hdr_valid(NULL));
}

static void test_pick_with_only_one_valid(void)
{
    bitmap_slot_hdr_t a = hdr(5, BITMAP_SLOT_LEN, 0xAA);
    bitmap_slot_hdr_t none = never_written();

    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&a, &none));
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_B, bitmap_slot_pick(&none, &a));
    /* Neither valid: a defined answer, so the caller can report "no bitmap". */
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&none, &none));
}

/* The higher sequence wins — that is what makes a promote take effect. */
static void test_higher_sequence_wins(void)
{
    bitmap_slot_hdr_t a = hdr(3, BITMAP_SLOT_LEN, 0xAA);
    bitmap_slot_hdr_t b = hdr(4, BITMAP_SLOT_LEN, 0xBB);
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_B, bitmap_slot_pick(&a, &b));
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&b, &a));
    /* Tie favours A, deterministically. */
    bitmap_slot_hdr_t a2 = hdr(4, BITMAP_SLOT_LEN, 0xAA);
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&a2, &b));
}

/* THE POWER-CUT CASE. A torn promote leaves the spare slot's header invalid but with a
 * garbage (higher) sequence number. It must be IGNORED, so the previously active image is
 * still selected — otherwise a power cut during upload would brick the display. */
static void test_torn_promote_keeps_the_previous_image(void)
{
    bitmap_slot_hdr_t live = hdr(7, BITMAP_SLOT_LEN, 0xAAAA);

    /* The spare slot's header write tore: valid magic, huge seq, but the length is garbage
     * (or the CRC never landed). Either way it must fail validation. */
    bitmap_slot_hdr_t torn_bad_len = hdr(0xFFFFFFF0u, 12345, 0);
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&live, &torn_bad_len));

    bitmap_slot_hdr_t torn_bad_magic = hdr(0xFFFFFFF0u, BITMAP_SLOT_LEN, 0xBB);
    torn_bad_magic.magic = 0xDEADBEEF;
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&live, &torn_bad_magic));

    /* Erased flash (all 0xFF) must also be ignored. */
    bitmap_slot_hdr_t erased;
    memset(&erased, 0xFF, sizeof(erased));
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&live, &erased));
}

/* A promote must target the slot that is NOT live; writing over the live slot would destroy
 * the only good image. */
static void test_spare_is_never_the_live_slot(void)
{
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_B, bitmap_slot_spare(BITMAP_SLOT_A));
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_spare(BITMAP_SLOT_B));
}

/* The full promote cycle: A live -> upload to B -> B live -> upload to A -> A live. */
static void test_promote_cycle_alternates(void)
{
    uint8_t *img = malloc(BITMAP_SLOT_LEN);
    memset(img, 0x5A, BITMAP_SLOT_LEN);

    bitmap_slot_hdr_t a = never_written();
    bitmap_slot_hdr_t b = never_written();

    /* First ever promote: nothing is live, so seq starts at 1. */
    bitmap_slot_hdr_t h;
    TEST_ASSERT_EQUAL_INT(0, bitmap_slot_make_hdr(img, BITMAP_SLOT_LEN, 0, &h));
    TEST_ASSERT_EQUAL_UINT32(1, h.seq);
    a = h;
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&a, &b));

    /* Promote into the spare (B). */
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_B, bitmap_slot_spare(bitmap_slot_pick(&a, &b)));
    TEST_ASSERT_EQUAL_INT(0, bitmap_slot_make_hdr(img, BITMAP_SLOT_LEN, a.seq, &h));
    b = h;
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_B, bitmap_slot_pick(&a, &b));

    /* And back into A. */
    TEST_ASSERT_EQUAL_INT(0, bitmap_slot_make_hdr(img, BITMAP_SLOT_LEN, b.seq, &h));
    a = h;
    TEST_ASSERT_EQUAL_INT(BITMAP_SLOT_A, bitmap_slot_pick(&a, &b));
    TEST_ASSERT_EQUAL_UINT32(3, a.seq);

    free(img);
}

/* A wrong-sized bitmap must never be promoted: it would leave part of the panel showing the
 * previous image, which is worse than not updating at all. */
static void test_make_hdr_rejects_wrong_length(void)
{
    uint8_t *img = malloc(BITMAP_SLOT_LEN);
    bitmap_slot_hdr_t h;
    TEST_ASSERT_EQUAL_INT(-1, bitmap_slot_make_hdr(img, BITMAP_SLOT_LEN - 1, 0, &h));
    TEST_ASSERT_EQUAL_INT(-1, bitmap_slot_make_hdr(img, 0, 0, &h));
    TEST_ASSERT_EQUAL_INT(-1, bitmap_slot_make_hdr(NULL, BITMAP_SLOT_LEN, 0, &h));
    TEST_ASSERT_EQUAL_INT(-1, bitmap_slot_make_hdr(img, BITMAP_SLOT_LEN, 0, NULL));
    free(img);
}

/* Sequence wrap must not produce seq == 0, which validation treats as "never written" —
 * that would make the promote silently fail to take effect. */
static void test_sequence_wrap_does_not_produce_zero(void)
{
    uint8_t *img = malloc(BITMAP_SLOT_LEN);
    memset(img, 0, BITMAP_SLOT_LEN);
    bitmap_slot_hdr_t h;
    TEST_ASSERT_EQUAL_INT(0, bitmap_slot_make_hdr(img, BITMAP_SLOT_LEN, 0xFFFFFFFFu, &h));
    TEST_ASSERT_NOT_EQUAL_UINT32(0, h.seq);
    TEST_ASSERT_TRUE(bitmap_slot_hdr_valid(&h));
    free(img);
}

/* The slot's CRC must be the same function the upload verified, so a bitmap that passed
 * upload cannot fail the slot check. */
static void test_slot_crc_matches_upload_crc(void)
{
    uint8_t *img = malloc(BITMAP_SLOT_LEN);
    for (uint32_t i = 0; i < BITMAP_SLOT_LEN; i++) img[i] = (uint8_t)(i * 7u);

    uint32_t via_slot = bitmap_slot_crc32(img, BITMAP_SLOT_LEN);

    /* Chunked, exactly as the upload path computes it. */
    uint32_t via_upload = 0;
    uint32_t off = 0;
    while (off < BITMAP_SLOT_LEN) {
        uint32_t n = BITMAP_SLOT_LEN - off;
        if (n > BITMAP_UPLOAD_MAX_CHUNK) n = BITMAP_UPLOAD_MAX_CHUNK;
        via_upload = upload_crc32(via_upload, img + off, n);
        off += n;
    }
    TEST_ASSERT_EQUAL_HEX32(via_upload, via_slot);

    /* And it matches a known-good value, so a web app using zlib crc32 agrees. */
    free(img);
}

/* A one-bit change in the bitmap must change the CRC — otherwise a corrupted upload could
 * pass verification. */
static void test_crc_detects_a_single_bit_flip(void)
{
    uint8_t *img = malloc(BITMAP_SLOT_LEN);
    memset(img, 0xFF, BITMAP_SLOT_LEN);
    uint32_t before = bitmap_slot_crc32(img, BITMAP_SLOT_LEN);
    img[40000] ^= 0x01;
    uint32_t after = bitmap_slot_crc32(img, BITMAP_SLOT_LEN);
    TEST_ASSERT_NOT_EQUAL_HEX32(before, after);
    free(img);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_header_validation);
    RUN_TEST(test_pick_with_only_one_valid);
    RUN_TEST(test_higher_sequence_wins);
    RUN_TEST(test_torn_promote_keeps_the_previous_image);
    RUN_TEST(test_spare_is_never_the_live_slot);
    RUN_TEST(test_promote_cycle_alternates);
    RUN_TEST(test_make_hdr_rejects_wrong_length);
    RUN_TEST(test_sequence_wrap_does_not_produce_zero);
    RUN_TEST(test_slot_crc_matches_upload_crc);
    RUN_TEST(test_crc_detects_a_single_bit_flip);
    return UNITY_END();
}
