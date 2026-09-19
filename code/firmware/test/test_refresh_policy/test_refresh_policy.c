#include "unity.h"
#include "refresh_policy.h"

void setUp(void) {}
void tearDown(void) {}

static void test_first_ever_refresh_is_full(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 5, 0));
}

static void test_partial_until_the_limit(void)
{
    for (int i = 1; i < 5; i++) {
        TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(i, 5, 0));
    }
}

static void test_full_at_the_limit(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(5, 5, 0));
}

/* The datasheet rule (FR-10): refresh at least every 24 h or ghosting occurs.
 * This must beat the partial counter. */
static void test_daily_full_refresh_overrides_partial_budget(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 5, 24));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 5, 48));
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(1, 5, 23));
}

static void test_zero_limit_never_allows_partial(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(0, 0, 0));
    /* A negative limit is as invalid as zero, and must not be read as "unlimited". */
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, -3, 0));
}

/* The daily rule must fire for ANY age at or beyond 24 h, including well past it, and
 * regardless of how much budget remains. A missed daily refresh is permanent panel damage
 * (ghosting/image sticking), not a cosmetic issue. */
static void test_daily_rule_fires_at_and_beyond_24h(void)
{
    for (int h = 24; h <= 200; h += 7) {
        TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 100, h));
    }
    /* And it does NOT fire early. */
    for (int h = 0; h < 24; h++) {
        TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(1, 100, h));
    }
}

/* A negative elapsed time is nonsense input; treating it as "very old" would force a full
 * refresh on every wake, so it must be treated as recent. */
static void test_negative_hours_does_not_force_full(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(1, 5, -1));
}

/* A limit of 1 means every update after the first is full — the boundary must be exactly
 * at the limit, not off by one. */
static void test_limit_of_one(void)
{
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(1, 1, 0));
    TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(2, 1, 0));
}

/* Exhaustive boundary sweep: with limit L and age < 24 h, partials 1..L-1 must be PARTIAL
 * and L..must be FULL. This is the property the whole function exists to guarantee. */
static void test_boundary_sweep(void)
{
    for (int limit = 1; limit <= 8; limit++) {
        for (int p = 1; p < limit; p++) {
            TEST_ASSERT_EQUAL_INT(REFRESH_PARTIAL, refresh_decide(p, limit, 0));
        }
        for (int p = limit; p <= limit + 3; p++) {
            TEST_ASSERT_EQUAL_INT(REFRESH_FULL, refresh_decide(p, limit, 0));
        }
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_first_ever_refresh_is_full);
    RUN_TEST(test_partial_until_the_limit);
    RUN_TEST(test_full_at_the_limit);
    RUN_TEST(test_daily_full_refresh_overrides_partial_budget);
    RUN_TEST(test_zero_limit_never_allows_partial);
    RUN_TEST(test_daily_rule_fires_at_and_beyond_24h);
    RUN_TEST(test_negative_hours_does_not_force_full);
    RUN_TEST(test_limit_of_one);
    RUN_TEST(test_boundary_sweep);
    return UNITY_END();
}
