#include <string.h>
#include "unity.h"
#include "vbat_history.h"

/* Battery voltage history (FR-33).
 *
 * THE RISK THIS COVERS: the ring lives in RTC_DATA_ATTR, which is NOT initialised on wake — the
 * bytes are whatever the previous wake left, and on a cold boot they are arbitrary. Every reader
 * therefore has to tolerate a garbage struct, and the two ways to get that wrong are both
 * memory-unsafe: a `count` past the array walks off the end in vbat_history_at(), and a `head`
 * past the array is an out-of-bounds WRITE in push(). Those are the tests that matter here. */

void setUp(void) {}
void tearDown(void) {}

static void test_init_stamps_the_magic_and_empties_the_ring(void)
{
    vbat_history_t h;
    memset(&h, 0xAA, sizeof(h));      /* dirty, like real RTC RAM */
    vbat_history_init(&h);

    TEST_ASSERT_EQUAL_INT(1, vbat_history_valid(&h));
    TEST_ASSERT_EQUAL_INT(0, vbat_history_count(&h));
    TEST_ASSERT_EQUAL_INT(-1, vbat_history_at(&h, 0));
}

/* Uninitialised RTC memory is the normal state on a cold boot, and it must NOT be read as a
 * history. The magic is the only thing standing between arbitrary bytes and a bogus trend. */
static void test_zeroed_or_garbage_memory_is_not_a_history(void)
{
    vbat_history_t z;
    memset(&z, 0, sizeof(z));
    TEST_ASSERT_EQUAL_INT(0, vbat_history_valid(&z));

    vbat_history_t g;
    memset(&g, 0x5A, sizeof(g));
    TEST_ASSERT_EQUAL_INT(0, vbat_history_valid(&g));
}

/* A corrupt count is the out-of-bounds read. It must be rejected outright rather than clamped
 * only where someone remembered to. */
static void test_a_count_past_the_array_is_rejected(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    h.count = VBAT_HISTORY_MAX + 1;
    TEST_ASSERT_EQUAL_INT(0, vbat_history_valid(&h));

    /* And a corrupt head is the out-of-bounds WRITE — the more dangerous of the two. */
    vbat_history_init(&h);
    h.head = VBAT_HISTORY_MAX;
    TEST_ASSERT_EQUAL_INT(0, vbat_history_valid(&h));
}

/* push() onto a corrupt ring must HEAL it, not write through the bad head. This is the test that
 * fails if the validity check is dropped from push(). */
static void test_push_heals_a_corrupt_ring(void)
{
    vbat_history_t h;
    memset(&h, 0x5A, sizeof(h));
    h.magic = VBAT_HISTORY_MAGIC;
    h.head = 9999;                    /* absurd: an unguarded write would be OOB */

    vbat_history_push(&h, 4000);      /* must not crash */

    TEST_ASSERT_EQUAL_INT(1, vbat_history_valid(&h));
    TEST_ASSERT_EQUAL_INT(1, vbat_history_count(&h));
    TEST_ASSERT_EQUAL_INT(4000, vbat_history_at(&h, 0));
}

static void test_samples_read_oldest_first(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    vbat_history_push(&h, 4200);
    vbat_history_push(&h, 4100);
    vbat_history_push(&h, 4000);

    TEST_ASSERT_EQUAL_INT(3, vbat_history_count(&h));
    TEST_ASSERT_EQUAL_INT(4200, vbat_history_at(&h, 0));
    TEST_ASSERT_EQUAL_INT(4100, vbat_history_at(&h, 1));
    TEST_ASSERT_EQUAL_INT(4000, vbat_history_at(&h, 2));
    TEST_ASSERT_EQUAL_INT(-1, vbat_history_at(&h, 3));
    TEST_ASSERT_EQUAL_INT(-1, vbat_history_at(&h, -1));
}

/* THE WRAP IS THE SUBTLE CASE: once full, the oldest sample is the one about to be overwritten,
 * so its position depends on `head`. Reading from slot 0 would silently report the samples in the
 * wrong order — a trend of the wrong SIGN, which is the one thing this value is used to decide
 * ("is the pack falling"). */
static void test_a_full_ring_overwrites_the_oldest_and_keeps_order(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    for (int i = 0; i < VBAT_HISTORY_MAX; i++) vbat_history_push(&h, 4000 + i);

    TEST_ASSERT_EQUAL_INT(VBAT_HISTORY_MAX, vbat_history_count(&h));
    TEST_ASSERT_EQUAL_INT(4000, vbat_history_at(&h, 0));

    /* One more: the oldest (4000) is dropped and 4024 becomes the newest. */
    vbat_history_push(&h, 9999);
    TEST_ASSERT_EQUAL_INT(VBAT_HISTORY_MAX, vbat_history_count(&h));
    TEST_ASSERT_EQUAL_INT(4001, vbat_history_at(&h, 0));       /* 4000 evicted */
    TEST_ASSERT_EQUAL_INT(9999, vbat_history_at(&h, VBAT_HISTORY_MAX - 1));
}

/* A failed ADC read is not a voltage. Recording it would put a huge negative into the trend and
 * make a healthy pack look like it was collapsing between two samples. */
static void test_a_negative_sample_is_ignored(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    vbat_history_push(&h, 4000);
    vbat_history_push(&h, -1);        /* failed read */

    TEST_ASSERT_EQUAL_INT(1, vbat_history_count(&h));
    TEST_ASSERT_EQUAL_INT(4000, vbat_history_at(&h, 0));
}

static void test_trend_is_volts_per_minute_across_the_span(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    vbat_history_push(&h, 4200);      /* 4.200 V */
    vbat_history_push(&h, 4100);
    vbat_history_push(&h, 4000);      /* 4.000 V, 0.2 V lower */

    /* 0.2 V over 30 minutes = 0.00667 V/min, and it is FALLING (negative). */
    TEST_ASSERT_FLOAT_WITHIN(1e-6, -0.2 / 30.0, vbat_history_trend(&h, 30.0));
}

/* One sample cannot show a trend. Returning 0.0 (not a division result) matters because
 * power_classify() reads a non-negative trend as "not falling" — the safe default for an unknown,
 * whereas a NaN or an inf would make the classification arbitrary. */
static void test_trend_is_zero_without_enough_history(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    TEST_ASSERT_EQUAL_FLOAT(0.0, vbat_history_trend(&h, 30.0));

    vbat_history_push(&h, 4000);
    TEST_ASSERT_EQUAL_FLOAT(0.0, vbat_history_trend(&h, 30.0));
}

/* A zero span must not divide by zero. This happens on a cold boot where the sample count is
 * known but no interval was ever applied. */
static void test_trend_is_zero_for_a_zero_span(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    vbat_history_push(&h, 4200);
    vbat_history_push(&h, 4000);
    TEST_ASSERT_EQUAL_FLOAT(0.0, vbat_history_trend(&h, 0.0));
}

/* A rising voltage (charging) is a positive trend — that is what tells a charging cell from a
 * discharging one, and it must not be sign-flipped. */
static void test_a_rising_voltage_is_a_positive_trend(void)
{
    vbat_history_t h;
    vbat_history_init(&h);
    vbat_history_push(&h, 3900);
    vbat_history_push(&h, 4100);
    TEST_ASSERT_TRUE(vbat_history_trend(&h, 10.0) > 0.0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_stamps_the_magic_and_empties_the_ring);
    RUN_TEST(test_zeroed_or_garbage_memory_is_not_a_history);
    RUN_TEST(test_a_count_past_the_array_is_rejected);
    RUN_TEST(test_push_heals_a_corrupt_ring);
    RUN_TEST(test_samples_read_oldest_first);
    RUN_TEST(test_a_full_ring_overwrites_the_oldest_and_keeps_order);
    RUN_TEST(test_a_negative_sample_is_ignored);
    RUN_TEST(test_trend_is_volts_per_minute_across_the_span);
    RUN_TEST(test_trend_is_zero_without_enough_history);
    RUN_TEST(test_trend_is_zero_for_a_zero_span);
    RUN_TEST(test_a_rising_voltage_is_a_positive_trend);
    return UNITY_END();
}
