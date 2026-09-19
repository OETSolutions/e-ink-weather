#include "unity.h"
#include "refresh_policy.h"

void setUp(void) {}
void tearDown(void) {}

static void test_first_ever_refresh_is_full(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 0, 5, 0));
}

/* THE REGRESSION THIS SIGNATURE EXISTS FOR. After a full refresh the glass is clean, so the
 * counter is 0 — but something IS on the glass and the next update must be a PARTIAL. Fusing
 * "nothing drawn" into "counter is 0" made this case impossible to express, so every refresh
 * became a full one. */
static void test_after_a_full_refresh_the_next_one_is_partial(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(0, 0, 5, 0));
}

static void test_partial_until_the_limit(void)
{
    for (int i = 1; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(0, i, 5, 0));
    }
}

static void test_full_at_the_limit(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 5, 5, 0));
}

/* The datasheet rule (FR-10): refresh at least every 24 h or ghosting occurs.
 * This must beat the partial counter. */
static void test_daily_full_refresh_overrides_partial_budget(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 1, 5, 24));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 0, 5, 48));
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(0, 1, 5, 23));
}

static void test_zero_limit_never_allows_partial(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 0, 0, 0));
    /* ...including when the counter is already past it, which is the state a user reaches
     * by setting the limit to 0 after some partials have already happened. */
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 7, 0, 1));
}

/* A negative limit is as disabling as zero, for the same reason. */
static void test_negative_limit_never_allows_partial(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 1, -3, 1));
}

/* A negative counter means the count is untrustworthy, which must not be read as "budget
 * still available" — with a limit of 1 and a counter of -1, "one partial allowed" would be
 * wrong. Any negative counter is treated as the budget being spent. */
static void test_negative_counter_forces_full(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, -1, 5, 1));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, -1, 1, 1));
}

/* The boundary is exactly 24, not 25: 23 h is partial, 24 h is full. */
static void test_the_24_hour_boundary_is_exact(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(0, 1, 5, 23));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL,    refresh_decide(0, 1, 5, 24));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL,    refresh_decide(0, 1, 5, 25));
}

/* One below the limit is still partial — an off-by-one here would spend a full refresh
 * every cycle, halving the battery life for no ghosting benefit. */
static void test_one_below_the_limit_is_still_partial(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(0, 4, 5, 0));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL,    refresh_decide(0, 5, 5, 0));
}

/* A limit of 1 means every refresh after the first is full. */
static void test_limit_of_one_alternates(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL,    refresh_decide(1, 0, 1, 0));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL,    refresh_decide(0, 1, 1, 0));
}

/* Nothing on the glass beats everything else: even a fresh counter and an unspent budget
 * must not produce a partial, because there is no frame to diff against. */
static void test_nothing_on_glass_beats_an_unspent_budget(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 0, 5, 0));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 0, 5, 23));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_ever_refresh_is_full);
    RUN_TEST(test_after_a_full_refresh_the_next_one_is_partial);
    RUN_TEST(test_partial_until_the_limit);
    RUN_TEST(test_full_at_the_limit);
    RUN_TEST(test_daily_full_refresh_overrides_partial_budget);
    RUN_TEST(test_zero_limit_never_allows_partial);
    RUN_TEST(test_negative_limit_never_allows_partial);
    RUN_TEST(test_negative_counter_forces_full);
    RUN_TEST(test_the_24_hour_boundary_is_exact);
    RUN_TEST(test_one_below_the_limit_is_still_partial);
    RUN_TEST(test_limit_of_one_alternates);
    RUN_TEST(test_nothing_on_glass_beats_an_unspent_budget);
    return UNITY_END();
}
