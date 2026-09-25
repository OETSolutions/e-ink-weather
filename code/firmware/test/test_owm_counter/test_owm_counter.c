/* Host tests for the OWM daily call counter (spec §3.4).
 *
 * The logic under test is small but the failure it guards against is expensive and silent: the
 * free tier is 1,000 calls/day against a 10-15 minute refresh (~96-144 calls), so the cap is a
 * tripwire for a runaway refresh loop rather than a normal operating limit. A counter that is
 * off by one day, or that reports "0 calls" when it cannot count at all, would hide exactly the
 * bug it exists to catch. */

#include "unity.h"
#include "owm_counter.h"

void setUp(void) {}
void tearDown(void) {}

/* A timestamp inside a known calendar day: 2026-09-19 12:00 UTC. */
#define DAY_A_BASE (1789000000L)          /* arbitrary but fixed */
#define DAY_A_ANY  ((DAY_A_BASE / 86400L) * 86400L + 3600L)
#define DAY_B_ANY  (DAY_A_ANY + 86400L)
#define DAY_C_ANY  (DAY_A_ANY + 2L * 86400L)

static void test_starts_unknown_and_permits_a_call(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    /* Unknown means "we have never seen a timestamp", which must NOT be treated as "no calls
     * made" — those are different states, and conflating them hides a runaway count. */
    TEST_ASSERT_EQUAL_INT(0, owm_counter_known(&c));
    /* But it must still permit the call: refusing to fetch on an unknown day would brick the
     * display permanently, which is far worse than one uncounted call. */
    TEST_ASSERT_EQUAL_INT(1, owm_counter_should_call(&c, 1000));
}

static void test_counting_within_one_day(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    owm_counter_note_call(&c, DAY_A_ANY, 1000);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_known(&c));
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));

    owm_counter_note_call(&c, DAY_A_ANY + 600, 1000);
    owm_counter_note_call(&c, DAY_A_ANY + 1200, 1000);
    TEST_ASSERT_EQUAL_INT(3, owm_counter_calls(&c));
    TEST_ASSERT_EQUAL_INT(1, owm_counter_should_call(&c, 1000));
}

/* The provider's quota resets on a CALENDAR boundary, so the count must reset there and
 * nowhere else. A rolling 24 h window would let a device that refreshes across midnight
 * accumulate calls from two calendar days and cap itself early — refusing to update the
 * display on a perfectly normal day. */
static void test_count_resets_on_a_new_calendar_day(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    owm_counter_note_call(&c, DAY_A_ANY, 1000);
    owm_counter_note_call(&c, DAY_A_ANY + 60, 1000);
    TEST_ASSERT_EQUAL_INT(2, owm_counter_calls(&c));

    /* One second past the day boundary: a fresh quota. */
    owm_counter_note_call(&c, ((DAY_A_ANY / 86400L) + 1L) * 86400L + 1L, 1000);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));
    TEST_ASSERT_EQUAL_INT(1, owm_counter_known(&c));
}

/* The cap must actually stop a call — that is its entire purpose. */
static void test_cap_refuses_further_calls(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    owm_counter_note_call(&c, DAY_A_ANY, 1);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));
    /* Cap of 1, already used: the NEXT call must be refused. */
    TEST_ASSERT_EQUAL_INT(0, owm_counter_should_call(&c, 1));

    /* And the cap must lift with the new day, or one busy day would silence the device
     * permanently. The count restarting at 1 is what proves the budget was renewed. */
    owm_counter_note_call(&c, DAY_B_ANY, 1);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));
    TEST_ASSERT_EQUAL_INT(1, owm_counter_should_call(&c, 2));   /* a fresh day has budget */
    TEST_ASSERT_EQUAL_INT(0, owm_counter_should_call(&c, 1));   /* its cap of 1 is used */
}

static void test_calls_do_not_exceed_the_cap_within_a_day(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    for (int i = 0; i < 20; i++) owm_counter_note_call(&c, DAY_A_ANY + i, 5);
    TEST_ASSERT_EQUAL_INT(5, owm_counter_calls(&c));            /* capped at 5, not 20 */
    TEST_ASSERT_EQUAL_INT(0, owm_counter_should_call(&c, 5));
}

/* A response with no usable timestamp (an error body) must not be credited to a day: it would
 * land in whatever day epoch 0 falls in and corrupt the count from then on. */
static void test_zero_timestamp_is_ignored(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    owm_counter_note_call(&c, DAY_A_ANY, 1000);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));

    owm_counter_note_call(&c, 0, 1000);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));            /* unchanged */
    TEST_ASSERT_EQUAL_INT(1, owm_counter_known(&c));
}

/* A clock that goes backwards (a stale cached response, or a device whose location data
 * changed) must not reset the count to 1 and let the cap be evaded. */
static void test_earlier_timestamp_within_same_day_still_counts(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    owm_counter_note_call(&c, DAY_A_ANY + 3600, 1000);
    owm_counter_note_call(&c, DAY_A_ANY + 60, 1000);
    TEST_ASSERT_EQUAL_INT(2, owm_counter_calls(&c));
    TEST_ASSERT_EQUAL_INT(1, owm_counter_known(&c));
}

/* A response from an EARLIER calendar day must not reset the count either. Real time only
 * moves forward, so an earlier day cannot be a genuine quota boundary — it can only be a stale
 * response, and treating it as a boundary is what let the cap be evaded. */
static void test_earlier_day_does_not_reset_the_count(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    owm_counter_note_call(&c, DAY_B_ANY, 1000);      /* day B: 1 */
    owm_counter_note_call(&c, DAY_B_ANY + 60, 1000); /* day B: 2 */
    /* A stale response dated to the PREVIOUS day arrives. It must be counted, not honoured as a
     * fresh quota — otherwise a device whose two data sources disagree on the date would reset
     * its own count on every tick. */
    owm_counter_note_call(&c, DAY_A_ANY, 1000);
    TEST_ASSERT_EQUAL_INT(3, owm_counter_calls(&c));
}

/* THE REGRESSION THAT SHIPPED: on the free tier the tick makes TWO OWM calls, and they credit
 * the count from different timestamps — 2.5/weather by the observation `dt`, 2.5/forecast by
 * "list"[0]."dt" (the NEXT 3-hourly slot). In the last three hours of a UTC day that slot is
 * 00:00 TOMORROW, so the two calls reported adjacent days and each reset the other's count. The
 * count oscillated 1-2 forever and the 1000/day cap could never trip. Measured on the bench
 * 2026-09-24: owm_day_calls pinned at 1 while partials_since_full climbed normally.
 *
 * Interleaving a later day and an earlier day must ACCUMULATE, not oscillate. The first tick
 * rolls the day forward once (A -> B) and so reports 1; every tick after that adds both calls,
 * because B is never "earlier" again. Before the fix the count sat at 1 forever. */
static void test_two_sources_disagreeing_on_the_day_accumulate(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    /* Tick 1: observation (day A), then the forecast slot (day B, tomorrow). */
    owm_counter_note_call(&c, DAY_A_ANY, 1000);
    owm_counter_note_call(&c, DAY_B_ANY, 1000);
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));     /* the A->B roll discards A's count */

    /* Tick 2: the SAME pair. Before the fix this drove the count back down to 1 every tick;
     * now both calls accumulate, because A and B are both no longer later than the day held. */
    owm_counter_note_call(&c, DAY_A_ANY + 180, 1000);
    owm_counter_note_call(&c, DAY_B_ANY + 180, 1000);
    TEST_ASSERT_EQUAL_INT(3, owm_counter_calls(&c));

    /* Tick 3: it must keep climbing, which is the whole point. */
    owm_counter_note_call(&c, DAY_A_ANY + 360, 1000);
    owm_counter_note_call(&c, DAY_B_ANY + 360, 1000);
    TEST_ASSERT_EQUAL_INT(5, owm_counter_calls(&c));
}

/* And the cap must actually fire despite that disagreement — the point of the fix. */
static void test_cap_trips_despite_two_disagreeing_sources(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    for (int i = 0; i < 10 && owm_counter_should_call(&c, 6); i++) {
        owm_counter_note_call(&c, DAY_A_ANY + i, 6);      /* observation, day A */
        owm_counter_note_call(&c, DAY_B_ANY + i, 6);      /* forecast slot, day B */
    }
    TEST_ASSERT_EQUAL_INT(6, owm_counter_calls(&c));
    TEST_ASSERT_EQUAL_INT(0, owm_counter_should_call(&c, 6));
}

/* Day boundaries are exact: the last second of a day belongs to that day, the first second of
 * the next does not. */
static void test_day_boundary_is_exact(void)
{
    owm_counter_t c;
    owm_counter_init(&c);

    const long day_start = (DAY_A_ANY / 86400L) * 86400L;
    owm_counter_note_call(&c, day_start + 86399, 1000);      /* last second of the day */
    owm_counter_note_call(&c, day_start + 86400, 1000);      /* first second of the next */
    TEST_ASSERT_EQUAL_INT(1, owm_counter_calls(&c));   /* the new day's first call */
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_starts_unknown_and_permits_a_call);
    RUN_TEST(test_counting_within_one_day);
    RUN_TEST(test_count_resets_on_a_new_calendar_day);
    RUN_TEST(test_cap_refuses_further_calls);
    RUN_TEST(test_calls_do_not_exceed_the_cap_within_a_day);
    RUN_TEST(test_zero_timestamp_is_ignored);
    RUN_TEST(test_earlier_timestamp_within_same_day_still_counts);
    RUN_TEST(test_earlier_day_does_not_reset_the_count);
    RUN_TEST(test_two_sources_disagreeing_on_the_day_accumulate);
    RUN_TEST(test_cap_trips_despite_two_disagreeing_sources);
    RUN_TEST(test_day_boundary_is_exact);
    return UNITY_END();
}
