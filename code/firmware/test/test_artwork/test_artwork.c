#include <string.h>
#include <stdio.h>
#include "unity.h"
#include "artwork.h"
#include "fixture_artwork.h"

/* The per-page artwork store. THE DEFECT THIS GUARDS: the device had ONE static layer and
 * rotated pages, so a page's readings were stamped onto another page's artwork — page 2's
 * labels sitting under page 1's numbers. Seen on hardware.
 *
 * The tests that matter most are the negative ones: a page with no artwork must NOT inherit a
 * neighbour's, and a torn promote must NOT become live. */

void setUp(void) {}
void tearDown(void) {}

static artwork_hdr_t hdr(uint32_t seq, uint32_t pages)
{
    artwork_hdr_t h;
    memset(&h, 0, sizeof(h));
    h.magic = ARTWORK_MAGIC;
    h.seq = seq;
    h.page_count = pages;
    return h;
}

static void test_a_valid_header_is_accepted(void)
{
    artwork_hdr_t h = hdr(1, 2);
    TEST_ASSERT_EQUAL_INT(1, artwork_hdr_valid(&h));
}

/* The magic is what stops an unwritten (all-zero) slot from being treated as artwork. */
static void test_unwritten_or_wrong_magic_is_invalid(void)
{
    artwork_hdr_t zero;
    memset(&zero, 0, sizeof(zero));
    TEST_ASSERT_EQUAL_INT(0, artwork_hdr_valid(&zero));

    artwork_hdr_t h = hdr(1, 1);
    h.magic = 0xDEADBEEF;
    TEST_ASSERT_EQUAL_INT(0, artwork_hdr_valid(&h));

    TEST_ASSERT_EQUAL_INT(0, artwork_hdr_valid(NULL));
}

/* seq == 0 means "never written", which is different from "written with sequence 0" — and the
 * distinction is what keeps a freshly-erased slot from winning a comparison. */
static void test_seq_zero_is_never_written(void)
{
    artwork_hdr_t h = hdr(0, 1);
    TEST_ASSERT_EQUAL_INT(0, artwork_hdr_valid(&h));
}

static void test_page_count_is_bounded(void)
{
    artwork_hdr_t z = hdr(1, 0);
    TEST_ASSERT_EQUAL_INT(0, artwork_hdr_valid(&z));
    artwork_hdr_t over = hdr(1, ARTWORK_MAX_PAGES + 1);
    TEST_ASSERT_EQUAL_INT(0, artwork_hdr_valid(&over));
    artwork_hdr_t max = hdr(1, ARTWORK_MAX_PAGES);
    TEST_ASSERT_EQUAL_INT(1, artwork_hdr_valid(&max));
}

/* A TORN PROMOTE MUST NOT WIN. The new sequence is higher, but its CRC/magic never landed, so
 * the previously active artwork stays live. This is the whole reason for two slots. */
static void test_invalid_higher_sequence_does_not_win(void)
{
    artwork_hdr_t a = hdr(3, 2);          /* live, valid */
    artwork_hdr_t b;
    memset(&b, 0, sizeof(b));
    b.seq = 99;                            /* torn: no magic */
    TEST_ASSERT_EQUAL_INT(0, artwork_pick_slot(&a, &b));
}

static void test_higher_sequence_wins_when_both_valid(void)
{
    artwork_hdr_t a = hdr(3, 2);
    artwork_hdr_t b = hdr(4, 2);
    TEST_ASSERT_EQUAL_INT(1, artwork_pick_slot(&a, &b));
    TEST_ASSERT_EQUAL_INT(0, artwork_pick_slot(&b, &a));
}

static void test_ties_go_to_a_and_empty_reports_none(void)
{
    artwork_hdr_t a = hdr(5, 2);
    artwork_hdr_t b = hdr(5, 2);
    TEST_ASSERT_EQUAL_INT(0, artwork_pick_slot(&a, &b));

    artwork_hdr_t z;
    memset(&z, 0, sizeof(z));
    TEST_ASSERT_EQUAL_INT(-1, artwork_pick_slot(&z, &z));   /* no artwork anywhere yet */
}

static void test_spare_is_never_the_live_slot(void)
{
    TEST_ASSERT_EQUAL_INT(1, artwork_spare_slot(0));
    TEST_ASSERT_EQUAL_INT(0, artwork_spare_slot(1));
}

/* A PAGE WITH NO ARTWORK MUST NOT INHERIT A NEIGHBOUR'S. raw_len == 0 is "none", and reporting
 * it as an error is what makes the renderer fall back to a blank layer instead of drawing the
 * wrong labels on the glass. */
static void test_page_without_artwork_reports_none(void)
{
    artwork_hdr_t h = hdr(1, 2);
    artwork_entry_t e[ARTWORK_MAX_PAGES];
    memset(e, 0, sizeof(e));
    e[0].comp_len = 100;
    e[0].raw_len = ARTWORK_RAW_LEN;
    /* e[1] left zeroed: page 1 exists in the table but has no artwork. */

    artwork_entry_t out;
    TEST_ASSERT_EQUAL_INT(0, artwork_entry_at(&h, e, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 1, &out));
    /* And a page past the count is refused, not clamped onto page 0. */
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 2, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 99999, &out));

    /* A page marked "no artwork" must be refused even when its other fields look perfectly
     * renderable. This is the shape a slot takes when the web app pushes artwork for fewer pages
     * than the config has, and it is the case that would otherwise draw page 0's picture as if it
     * were page 1's — the exact defect this module exists to prevent. */
    e[1].comp_len = 100;                    /* a plausible stream size... */
    e[1].raw_len = 0;                       /* ...but explicitly "no artwork" */
    e[1].offset = 0;
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 1, &out));
}

/* The same case at the boundary the guard actually defends: an entry whose raw_len is 0 must be
 * refused even though a NON-zero comp_len would let it past the other checks. Without the
 * raw_len guard this returns 0 and the renderer inflates nothing over a live frame. */
static void test_absent_page_is_refused_despite_a_valid_comp_len(void)
{
    artwork_hdr_t h = hdr(1, 3);
    artwork_entry_t e[ARTWORK_MAX_PAGES];
    memset(e, 0, sizeof(e));
    for (int i = 0; i < 3; i++) {
        e[i].comp_len = 200;
        e[i].raw_len = ARTWORK_RAW_LEN;
        e[i].offset = (uint32_t)i * 200;
    }
    e[1].raw_len = 0;                       /* page 1 pushed no artwork... */
    e[1].comp_len = 150;                    /* ...but its offset field still holds a stale length,
                                             * which only the raw_len guard can reject */

    artwork_entry_t out;
    TEST_ASSERT_EQUAL_INT(0, artwork_entry_at(&h, e, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 1, &out));
    TEST_ASSERT_EQUAL_INT(0, artwork_entry_at(&h, e, 2, &out));
}

/* A WRONG-SIZED raw_len would inflate to a layer of the wrong size and render garbage. */
static void test_wrong_raw_len_is_refused(void)
{
    artwork_hdr_t h = hdr(1, 1);
    artwork_entry_t e[ARTWORK_MAX_PAGES];
    memset(e, 0, sizeof(e));
    e[0].comp_len = 100;
    e[0].raw_len = ARTWORK_RAW_LEN - 1;
    artwork_entry_t out;
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 0, &out));
}

static void test_null_arguments_are_refused(void)
{
    artwork_hdr_t h = hdr(1, 1);
    artwork_entry_t e[1];
    memset(e, 0, sizeof(e));
    artwork_entry_t out;
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(NULL, e, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, NULL, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, e, 0, NULL));
}

static void test_make_hdr_builds_a_valid_header(void)
{
    artwork_hdr_t h = hdr(7, 0);            /* live seq 7 */
    artwork_entry_t e[2];
    memset(e, 0, sizeof(e));
    const uint8_t blob[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    e[0].offset = 0; e[0].comp_len = 4; e[0].raw_len = ARTWORK_RAW_LEN;
    e[1].offset = 4; e[1].comp_len = 4; e[1].raw_len = ARTWORK_RAW_LEN;

    artwork_hdr_t out;
    TEST_ASSERT_EQUAL_INT(0, artwork_make_hdr(e, 2, blob, sizeof(blob), h.seq, &out));
    TEST_ASSERT_EQUAL_INT(1, artwork_hdr_valid(&out));
    TEST_ASSERT_EQUAL_UINT(2, out.page_count);
    /* seq must EXCEED the live one, or the promote would not become live. */
    TEST_ASSERT_EQUAL_UINT(8, out.seq);
    /* Passed as slot B against the live slot A, the promoted header WINS — that is what makes a
     * promote take effect on the next boot, and asserting anything else here would have been
     * pinning the wrong behaviour. */
    TEST_ASSERT_EQUAL_INT(1, artwork_pick_slot(&h, &out));
    /* And in the other argument order it loses, so the result really is about seq and not order
     * of arguments. */
    TEST_ASSERT_EQUAL_INT(0, artwork_pick_slot(&out, &h));
}

/* A stream over the budget is REFUSED, never truncated: a clipped zlib stream inflates to
 * garbage, and garbage on the panel is worse than no artwork. */
static void test_over_budget_stream_is_refused(void)
{
    artwork_entry_t e[1];
    memset(e, 0, sizeof(e));
    e[0].comp_len = ARTWORK_MAX_COMP + 1;
    e[0].raw_len = ARTWORK_RAW_LEN;
    uint8_t blob[ARTWORK_MAX_COMP + 2];
    memset(blob, 0xAA, sizeof(blob));
    artwork_hdr_t out;
    TEST_ASSERT_EQUAL_INT(-1, artwork_make_hdr(e, 1, blob, sizeof(blob), 0, &out));
}

/* An entry pointing past the end of the blob would read whatever the next page's stream holds. */
static void test_entry_past_the_blob_end_is_refused(void)
{
    artwork_entry_t e[1];
    memset(e, 0, sizeof(e));
    e[0].offset = 100; e[0].comp_len = 50; e[0].raw_len = ARTWORK_RAW_LEN;
    uint8_t blob[120];
    memset(blob, 0, sizeof(blob));
    artwork_hdr_t out;
    TEST_ASSERT_EQUAL_INT(-1, artwork_make_hdr(e, 1, blob, sizeof(blob), 0, &out));
}

static void test_make_hdr_argument_checks(void)
{
    artwork_entry_t e[1];
    memset(e, 0, sizeof(e));
    artwork_hdr_t out;
    TEST_ASSERT_EQUAL_INT(-1, artwork_make_hdr(NULL, 1, NULL, 0, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_make_hdr(e, 0, NULL, 0, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_make_hdr(e, ARTWORK_MAX_PAGES + 1, NULL, 0, 0, &out));
    TEST_ASSERT_EQUAL_INT(-1, artwork_make_hdr(e, 1, NULL, 0, 0, NULL));
}

/* The CRC must match zlib's, because the web app computes the same value with Node's crc32
 * algorithm — two different answers would mean nothing ever promotes. */
static void test_crc32_matches_the_known_vector(void)
{
    const uint8_t data[] = "123456789";
    /* The standard CRC-32 check value for "123456789". */
    TEST_ASSERT_EQUAL_UINT32(0xCBF43926u, artwork_crc32(data, 9));
}

static void test_crc32_continuing_form_matches_the_one_shot(void)
{
    const uint8_t a[] = "1234";
    const uint8_t b[] = "56789";
    const uint32_t split = artwork_crc32_finish(
        artwork_crc32_cont(artwork_crc32_cont(0xFFFFFFFFu, a, 4), b, 5));
    const uint8_t whole[] = "123456789";
    TEST_ASSERT_EQUAL_UINT32(artwork_crc32(whole, 9), split);
}

/* The blob offset must be fixed, so an entry can be read without reading the whole table. */
static void test_blob_offset_accounts_for_the_full_table(void)
{
    TEST_ASSERT_EQUAL_UINT(sizeof(artwork_hdr_t) + sizeof(artwork_entry_t) * ARTWORK_MAX_PAGES,
                           artwork_blob_offset());
}

/* ===================================================================================
 * CROSS-LANGUAGE CONTRACT: parse a REAL blob produced by the web app.
 *
 * THE RISK THIS COVERS: the two sides are written in different languages with independently
 * hand-written struct definitions. Field ORDER, FIELD WIDTH and ENDIANNESS all have to agree,
 * and a mismatch is invisible in either side's own tests — the web app's tests check its output
 * against its own expectations, and these C tests would otherwise check hand-built bytes against
 * the C struct. Both could pass while the actual transfer fails.
 *
 * So this parses `fixture_artwork.h`, which is a byte-for-byte capture of what
 * webapp/src/transfer/artwork.ts emits for the shipped two-page layout.
 * =================================================================================== */

static void test_real_webapp_blob_has_a_valid_header(void)
{
    TEST_ASSERT_TRUE(FIXTURE_ART_LEN >= sizeof(artwork_hdr_t));

    artwork_hdr_t h;
    memcpy(&h, FIXTURE_ART, sizeof(h));
    /* The header the web app wrote must satisfy the device's validation — a magic or page-count
     * disagreement would reject EVERY artwork upload. Note `seq` is 0 in the blob by design: the
     * device assigns it at promote time, so hdr_valid() will reject this raw header until then. */
    TEST_ASSERT_EQUAL_UINT32(ARTWORK_MAGIC, h.magic);
    TEST_ASSERT_EQUAL_UINT32(2, h.page_count);
    TEST_ASSERT_EQUAL_UINT32(0, h.seq);
}

static void test_real_webapp_blob_crc_matches_the_device_algorithm(void)
{
    /* The checksum covers the table AND the blob, not the header. If the two sides disagree
     * about that region — the easiest possible mistake — the CRC matches neither and the device
     * refuses the upload with a message about a checksum, which points at the wrong thing. */
    const uint32_t want = artwork_crc32(FIXTURE_ART + sizeof(artwork_hdr_t),
                                        FIXTURE_ART_LEN - sizeof(artwork_hdr_t));
    TEST_ASSERT_EQUAL_UINT32(FIXTURE_ART_CRC, want);

    /* And the value the web app wrote INTO the header (offset 8) must be the same number. */
    uint32_t hdr_crc;
    memcpy(&hdr_crc, FIXTURE_ART + 8, sizeof(hdr_crc));
    TEST_ASSERT_EQUAL_UINT32(want, hdr_crc);
}

static void test_real_webapp_blob_entries_point_at_usable_streams(void)
{
    artwork_hdr_t h;
    memcpy(&h, FIXTURE_ART, sizeof(h));

    artwork_entry_t table[ARTWORK_MAX_PAGES];
    memcpy(table, FIXTURE_ART + sizeof(artwork_hdr_t), sizeof(table));

    for (uint32_t page = 0; page < h.page_count; page++) {
        artwork_entry_t e;
        TEST_ASSERT_EQUAL_INT(0, artwork_entry_at(&h, table, page, &e));
        /* The entry must sit inside the blob, or the device would read the next page's stream (or
         * past the end of the partition) and inflate garbage onto the panel. */
        TEST_ASSERT_TRUE(artwork_blob_offset() + e.offset + e.comp_len <= FIXTURE_ART_LEN);
    }
}

/* A page past the count must be refused, not served another page's picture. */
static void test_real_webapp_blob_refuses_a_page_it_does_not_contain(void)
{
    artwork_hdr_t h;
    memcpy(&h, FIXTURE_ART, sizeof(h));
    artwork_entry_t table[ARTWORK_MAX_PAGES];
    memcpy(table, FIXTURE_ART + sizeof(artwork_hdr_t), sizeof(table));

    artwork_entry_t e;
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, table, 2, &e));
    TEST_ASSERT_EQUAL_INT(-1, artwork_entry_at(&h, table, 7, &e));}

/* ===================================================================================
 * THE STRIP DECODE CONTRACT, decoded on the HOST with the system zlib.
 *
 * THE RISK THIS COVERS: the device decodes a page as ARTWORK_STRIP_COUNT INDEPENDENT zlib streams,
 * each with NO dictionary (the layer's 32 KB dictionary cannot coexist with the layer on the
 * fragmented device heap — see lib/upload/artwork.h). That only works if the web app really
 * compressed each strip on its own, with no back-reference across a strip boundary. If it did not,
 * the device's dictionary-free decode fails at some strip and the panel keeps its old picture —
 * which no amount of testing the two sides separately would reveal, because the encoder's output
 * is valid zlib either way.
 *
 * So this walks the fixture EXACTLY as the device does: inflate one strip from the current input
 * offset into an output buffer of exactly ARTWORK_STRIP_RAW bytes, take the decoder's consumed
 * input count as the next strip's offset, and assert every byte against the source layer. zlib's
 * raw-inflate reports that count, which is the same number the ROM's tinfl gives the firmware.
 * =================================================================================== */

/* Inflate ONE self-contained stream from `in`(len) into `out`(out_len) with NO dictionary, using
 * zlib's raw entry point. Returns the number of INPUT bytes consumed, or -1 on failure. This mirrors
 * the firmware's inflate_one(): a fresh inflater per strip, output buffer exactly the strip size. */
#include <zlib.h>
static int host_inflate_one(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_len)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    /* A raw windowBits of 15 with no dictionary: zlib reads the RFC1950 header itself. The device
     * uses TINFL_FLAG_PARSE_ZLIB_HEADER, which is the same thing. */
    if (inflateInit(&zs) != Z_OK) return -1;
    zs.next_in = (Bytef *)in;
    zs.avail_in = (uInt)in_len;
    zs.next_out = out;
    zs.avail_out = (uInt)out_len;
    const int rc = inflate(&zs, Z_FINISH);
    const size_t produced = out_len - zs.avail_out;
    const uLong used = zs.total_in;
    inflateEnd(&zs);
    if ((rc != Z_STREAM_END && produced != out_len) || produced != out_len) return -1;
    return (int)used;
}

/* Decode a whole page's strip run into `out` (ARTWORK_RAW_LEN bytes), exactly as the device does. */
static int host_decode_page(const uint8_t *page, size_t page_len, uint8_t *out)
{
    size_t at = 0;
    for (unsigned i = 0; i < ARTWORK_STRIP_COUNT; i++) {
        const int used = host_inflate_one(page + at, page_len - at,
                                          out + (size_t)i * ARTWORK_STRIP_RAW, ARTWORK_STRIP_RAW);
        if (used <= 0) return -1;
        at += (size_t)used;
    }
    return 0;
}

/* Every strip must decode ALONE. This is the direct proof that the encoder did not emit one
 * whole-layer stream: if it had, strips 1..n-1 would each fail here (their back-references point
 * before their own start), which is precisely what would break the device. */
static void test_each_fixture_strip_decodes_alone_with_no_dictionary(void)
{
    artwork_hdr_t h;
    memcpy(&h, FIXTURE_ART, sizeof(h));
    artwork_entry_t table[ARTWORK_MAX_PAGES];
    memcpy(table, FIXTURE_ART + sizeof(artwork_hdr_t), sizeof(table));

    uint8_t page[ARTWORK_RAW_LEN];
    for (uint32_t pg = 0; pg < h.page_count; pg++) {
        artwork_entry_t e;
        TEST_ASSERT_EQUAL_INT(0, artwork_entry_at(&h, table, pg, &e));
        const uint8_t *stream = FIXTURE_ART + artwork_blob_offset() + e.offset;

        /* The whole page decodes, strip by strip, to ARTWORK_RAW_LEN bytes. */
        TEST_ASSERT_EQUAL_INT(0, host_decode_page(stream, e.comp_len, page));

        /* AND each strip decodes from its own offset with a fresh inflater — the property that
         * distinguishes per-strip streams from one stream. */
        size_t at = 0;
        for (unsigned i = 0; i < ARTWORK_STRIP_COUNT; i++) {
            uint8_t one[ARTWORK_STRIP_RAW];
            const int used = host_inflate_one(stream + at, e.comp_len - at, one, ARTWORK_STRIP_RAW);
            TEST_ASSERT_GREATER_THAN_INT(0, used);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(page + (size_t)i * ARTWORK_STRIP_RAW, one,
                                          ARTWORK_STRIP_RAW);
            at += (size_t)used;
        }
    }
}

/* ===================================================================================
 * UPLOAD CHUNK PLACEMENT: the client's header must NEVER reach the slot's offset 0.
 *
 * THE BUG THIS LOCKS DOWN: the client sends header + table + blob and the header carries seq 0
 * (it cannot know the live sequence). Writing it to offset 0 and then writing the real seq 1 over
 * it FAILS — NOR flash only clears bits, so 0 -> 1 cannot be programmed. The write returns OK, the
 * slot reads back as seq 0, artwork_hdr_valid() calls it "never written", and EVERY later lookup
 * reports no artwork while the upload reported success. Observed on hardware.
 * =================================================================================== */

static void test_chunk_inside_the_header_goes_to_ram_not_flash(void)
{
    uint32_t to_hdr, hdr_off, flash_off;
    /* A chunk wholly within the 16-byte header has no flash destination at all. */
    artwork_chunk_placement(0, 16, &to_hdr, &hdr_off, &flash_off);
    TEST_ASSERT_EQUAL_UINT32(16, to_hdr);
    TEST_ASSERT_EQUAL_UINT32(0, hdr_off);
    TEST_ASSERT_EQUAL_UINT32(0, flash_off);   /* nothing written to the slot */
}

static void test_chunk_past_the_header_goes_to_flash_at_its_own_offset(void)
{
    uint32_t to_hdr, hdr_off, flash_off;
    /* Fully past the header: all to flash, at the literal slot offset (the client already
     * places the table at artwork_blob_offset()'s base, so there is no shift). */
    artwork_chunk_placement(100, 50, &to_hdr, &hdr_off, &flash_off);
    TEST_ASSERT_EQUAL_UINT32(0, to_hdr);
    TEST_ASSERT_EQUAL_UINT32(100, flash_off);
}

/* A chunk straddling the boundary is SPLIT: its first bytes splice into the RAM header at their
 * true offset, the rest continues to flash. Getting the split wrong would drop bytes and the blob
 * would no longer be contiguous with the table. */
static void test_a_straddling_chunk_is_split_at_the_header_boundary(void)
{
    uint32_t to_hdr, hdr_off, flash_off;
    artwork_chunk_placement(10, 100, &to_hdr, &hdr_off, &flash_off);
    TEST_ASSERT_EQUAL_UINT32(6, to_hdr);       /* bytes 10..15 of the 16-byte header */
    TEST_ASSERT_EQUAL_UINT32(10, hdr_off);
    TEST_ASSERT_EQUAL_UINT32(16, flash_off);   /* the remainder starts at the table offset */
}

/* The whole first chunk (the client usually sends 4 KB at a time) must not lose a byte: header
 * portion + flash portion must equal the chunk length. */
static void test_no_byte_of_a_full_first_chunk_is_lost(void)
{
    const uint32_t len = 4096;
    uint32_t to_hdr, hdr_off, flash_off;
    artwork_chunk_placement(0, len, &to_hdr, &hdr_off, &flash_off);
    TEST_ASSERT_EQUAL_UINT32(len, to_hdr + (len - to_hdr));
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(artwork_hdr_t), to_hdr);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof(artwork_hdr_t), flash_off);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_a_valid_header_is_accepted);
    RUN_TEST(test_unwritten_or_wrong_magic_is_invalid);
    RUN_TEST(test_seq_zero_is_never_written);
    RUN_TEST(test_page_count_is_bounded);
    RUN_TEST(test_invalid_higher_sequence_does_not_win);
    RUN_TEST(test_higher_sequence_wins_when_both_valid);
    RUN_TEST(test_ties_go_to_a_and_empty_reports_none);
    RUN_TEST(test_spare_is_never_the_live_slot);
    RUN_TEST(test_page_without_artwork_reports_none);
    RUN_TEST(test_absent_page_is_refused_despite_a_valid_comp_len);
    RUN_TEST(test_wrong_raw_len_is_refused);
    RUN_TEST(test_null_arguments_are_refused);
    RUN_TEST(test_make_hdr_builds_a_valid_header);
    RUN_TEST(test_over_budget_stream_is_refused);
    RUN_TEST(test_entry_past_the_blob_end_is_refused);
    RUN_TEST(test_make_hdr_argument_checks);
    RUN_TEST(test_crc32_matches_the_known_vector);
    RUN_TEST(test_crc32_continuing_form_matches_the_one_shot);
    RUN_TEST(test_blob_offset_accounts_for_the_full_table);
    RUN_TEST(test_real_webapp_blob_has_a_valid_header);
    RUN_TEST(test_real_webapp_blob_crc_matches_the_device_algorithm);
    RUN_TEST(test_real_webapp_blob_entries_point_at_usable_streams);
    RUN_TEST(test_real_webapp_blob_refuses_a_page_it_does_not_contain);
    RUN_TEST(test_each_fixture_strip_decodes_alone_with_no_dictionary);
    RUN_TEST(test_chunk_inside_the_header_goes_to_ram_not_flash);
    RUN_TEST(test_chunk_past_the_header_goes_to_flash_at_its_own_offset);
    RUN_TEST(test_a_straddling_chunk_is_split_at_the_header_boundary);
    RUN_TEST(test_no_byte_of_a_full_first_chunk_is_lost);
    return UNITY_END();
}
