#include "unity.h"
#include "segbuf.h"
#include <string.h>
#include <stdlib.h>

/* The segmented layer buffer.
 *
 * THE DEFECT THIS GUARDS: the static layer is 78,200 bytes and this part has exactly ONE DRAM
 * region large enough for it, which the live HTTP API fragments below 78,200. Holding the layer
 * as segments (measured: twenty 4 KB pieces allocated where a single 78,200-byte malloc failed)
 * removes the contiguity requirement — and the offset math that maps a panel byte to its segment
 * is the part that must be exactly right. An off-by-one here reads the wrong rows into a band
 * and leaves a stripe of a previous picture on the glass while reporting success. */

void setUp(void) {}
void tearDown(void) {}

/* A segbuf backed by one heap block, so the tests exercise the real read/write paths. */
static uint8_t g_storage[SEGBUF_MAX_SEGS][4096];
static segbuf_t g_b;

static void setup_buf(size_t total, size_t seg_bytes)
{
    segbuf_reset(&g_b);
    g_b.seg_bytes = seg_bytes;
    g_b.total = total;
    g_b.n = segbuf_segs_needed(total, seg_bytes);
    TEST_ASSERT_TRUE(g_b.n <= SEGBUF_MAX_SEGS);
    for (int i = 0; i < g_b.n; i++) {
        g_b.seg[i] = g_storage[i];
        memset(g_storage[i], 0, sizeof(g_storage[i]));
    }
}

static void test_segs_needed_rounds_up(void)
{
    TEST_ASSERT_EQUAL_INT(0, segbuf_segs_needed(0, 4096));
    TEST_ASSERT_EQUAL_INT(1, segbuf_segs_needed(1, 4096));
    TEST_ASSERT_EQUAL_INT(1, segbuf_segs_needed(4096, 4096));
    TEST_ASSERT_EQUAL_INT(2, segbuf_segs_needed(4097, 4096));
    /* The real case: 78,200 bytes at 4 KB per segment needs 20 segments (19 full + 392 bytes). */
    TEST_ASSERT_EQUAL_INT(20, segbuf_segs_needed(78200u, 4096u));
}

static void test_seg_len_last_is_short(void)
{
    TEST_ASSERT_EQUAL_UINT(4096, segbuf_seg_len(78200u, 4096u, 0));
    TEST_ASSERT_EQUAL_UINT(4096, segbuf_seg_len(78200u, 4096u, 18));
    /* 78200 = 19*4096 + 376 */
    TEST_ASSERT_EQUAL_UINT(376, segbuf_seg_len(78200u, 4096u, 19));
    TEST_ASSERT_EQUAL_UINT(0, segbuf_seg_len(78200u, 4096u, 20));   /* past the end */
    TEST_ASSERT_EQUAL_UINT(0, segbuf_seg_len(78200u, 4096u, -1));
}

static void test_write_then_read_round_trips_across_segments(void)
{
    setup_buf(78200u, 4096u);
    /* A pattern that straddles two segment boundaries, so a naive single-segment copy would fail. */
    const size_t off = 4096u - 10u;
    uint8_t src[40];
    for (int i = 0; i < 40; i++) src[i] = (uint8_t)(i + 1);
    TEST_ASSERT_EQUAL_INT(0, segbuf_write(&g_b, off, src, sizeof(src)));

    uint8_t out[40];
    memset(out, 0, sizeof(out));
    TEST_ASSERT_EQUAL_INT(0, segbuf_read(&g_b, off, out, sizeof(out)));
    TEST_ASSERT_EQUAL_MEMORY(src, out, sizeof(src));
}

static void test_read_is_panel_absolute_across_many_segments(void)
{
    setup_buf(78200u, 4096u);
    /* Fill every byte with its own low byte, then read a long run that crosses several segments. */
    for (size_t off = 0; off < 78200u; off += 1024) {
        uint8_t chunk[1024];
        size_t n = (78200u - off < 1024) ? (78200u - off) : 1024;
        for (size_t i = 0; i < n; i++) chunk[i] = (uint8_t)((off + i) & 0xFF);
        TEST_ASSERT_EQUAL_INT(0, segbuf_write(&g_b, off, chunk, n));
    }
    uint8_t out[1500];
    const size_t start = 4096u - 700u;
    TEST_ASSERT_EQUAL_INT(0, segbuf_read(&g_b, start, out, sizeof(out)));
    for (int i = 0; i < 1500; i++) {
        TEST_ASSERT_EQUAL_UINT8((uint8_t)((start + i) & 0xFF), out[i]);
    }
}

static void test_read_past_the_end_is_refused_not_clipped(void)
{
    setup_buf(78200u, 4096u);
    uint8_t out[64];
    /* Exactly the last byte is fine. */
    TEST_ASSERT_EQUAL_INT(0, segbuf_read(&g_b, 78200u - 1, out, 1));
    /* One byte past the end must fail: a clipped read would draw a stale row and report success. */
    TEST_ASSERT_EQUAL_INT(-1, segbuf_read(&g_b, 78200u - 1, out, 2));
    TEST_ASSERT_EQUAL_INT(-1, segbuf_read(&g_b, 78200u, out, 1));
}

static void test_write_past_the_end_is_refused(void)
{
    setup_buf(78200u, 4096u);
    uint8_t src[64] = {0};
    TEST_ASSERT_EQUAL_INT(0, segbuf_write(&g_b, 78200u - 64, src, 64));
    TEST_ASSERT_EQUAL_INT(-1, segbuf_write(&g_b, 78200u - 63, src, 64));
}

static void test_a_zero_length_access_is_a_noop_success(void)
{
    setup_buf(78200u, 4096u);
    uint8_t x = 0;
    TEST_ASSERT_EQUAL_INT(0, segbuf_read(&g_b, 0, &x, 0));
    TEST_ASSERT_EQUAL_INT(0, segbuf_write(&g_b, 0, &x, 0));
}

static void test_a_missing_segment_fails_the_read(void)
{
    setup_buf(78200u, 4096u);
    g_b.seg[5] = NULL;   /* simulate an allocation that failed */
    uint8_t out[16];
    /* A read landing in segment 5 must fail rather than read through a NULL. */
    TEST_ASSERT_EQUAL_INT(-1, segbuf_read(&g_b, 5u * 4096u, out, sizeof(out)));
    /* A read that stays inside an earlier segment is unaffected. */
    TEST_ASSERT_EQUAL_INT(0, segbuf_read(&g_b, 0, out, sizeof(out)));
}

/* ---- the allocator ---- */

/* A tiny fake heap whose LARGEST SINGLE ALLOCATION is capped, so a large segment fails and the
 * trial loop must shrink. This is the fragmentation the module exists for: the total is plenty,
 * but no single piece is big enough. */
static size_t g_alloc_cap;      /* largest single allocation allowed */
static int    g_live;           /* outstanding allocations, to catch a leak */
static int    g_alloc_calls;

static void *cap_alloc(size_t n)
{
    g_alloc_calls++;
    if (n > g_alloc_cap) return NULL;
    g_live++;
    return malloc(n);
}
static void cap_free(void *p) { if (p) { g_live--; free(p); } }

static void test_alloc_shrinks_the_segment_until_it_fits(void)
{
    g_alloc_cap = 4096; g_live = 0; g_alloc_calls = 0;
    segbuf_t b; memset(&b, 0, sizeof(b));
    /* Ask for 32 KB segments; the cap of 4 KB must drive it down to 4 KB, which fits. */
    TEST_ASSERT_EQUAL_INT(0, segbuf_alloc(&b, 78200u, 32768u, 1024u, cap_alloc, cap_free));
    TEST_ASSERT_EQUAL_UINT(4096u, b.seg_bytes);
    TEST_ASSERT_EQUAL_INT(20, b.n);
    /* Round-trips through the real allocator's memory. */
    uint8_t src[8] = {1,2,3,4,5,6,7,8};
    TEST_ASSERT_EQUAL_INT(0, segbuf_write(&b, 4096u - 4u, src, 8));
    uint8_t out[8];
    TEST_ASSERT_EQUAL_INT(0, segbuf_read(&b, 4096u - 4u, out, 8));
    TEST_ASSERT_EQUAL_MEMORY(src, out, 8);
    segbuf_free(&b, cap_free);
    TEST_ASSERT_EQUAL_INT(0, g_live);      /* nothing leaked */
}

static void test_alloc_fails_cleanly_when_nothing_fits(void)
{
    g_alloc_cap = 100; g_live = 0;
    segbuf_t b; memset(&b, 0, sizeof(b));
    /* No segment size at or above the 1024 floor fits 100 bytes per piece. */
    TEST_ASSERT_EQUAL_INT(-1, segbuf_alloc(&b, 78200u, 32768u, 1024u, cap_alloc, cap_free));
    TEST_ASSERT_EQUAL_INT(0, b.n);
    TEST_ASSERT_EQUAL_INT(0, g_live);      /* every partial attempt was given back */
}

/* An allocator that succeeds only `n_ok` times, so a multi-segment attempt fails midway — the
 * case where a leaked half-allocated buffer would starve the next TLS handshake. */
static int g_quota;
static void *quota_alloc(size_t n)
{
    if (g_quota <= 0) return NULL;
    g_quota--;
    g_live++;
    return malloc(n);
}

static void test_alloc_failure_midway_leaks_nothing(void)
{
    g_alloc_cap = 4096; g_live = 0;
    segbuf_t b; memset(&b, 0, sizeof(b));
    /* The largest attempt (32 KB segments) needs only 3 pieces; every smaller size needs more, so a
     * quota of 2 makes EVERY attempt fail partway. The call must return -1 having taken and
     * returned 2 segments on each of the tries, leaking none. */
    g_quota = 2;
    TEST_ASSERT_EQUAL_INT(-1, segbuf_alloc(&b, 78200u, 32768u, 1024u, quota_alloc, cap_free));
    TEST_ASSERT_EQUAL_INT(0, g_live);      /* nothing leaked across the retries */
    TEST_ASSERT_EQUAL_INT(0, b.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_segs_needed_rounds_up);
    RUN_TEST(test_seg_len_last_is_short);
    RUN_TEST(test_write_then_read_round_trips_across_segments);
    RUN_TEST(test_read_is_panel_absolute_across_many_segments);
    RUN_TEST(test_read_past_the_end_is_refused_not_clipped);
    RUN_TEST(test_write_past_the_end_is_refused);
    RUN_TEST(test_a_zero_length_access_is_a_noop_success);
    RUN_TEST(test_a_missing_segment_fails_the_read);
    RUN_TEST(test_alloc_shrinks_the_segment_until_it_fits);
    RUN_TEST(test_alloc_fails_cleanly_when_nothing_fits);
    RUN_TEST(test_alloc_failure_midway_leaks_nothing);
    return UNITY_END();
}
