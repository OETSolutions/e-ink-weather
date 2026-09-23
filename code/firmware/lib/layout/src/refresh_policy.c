#include "refresh_policy.h"

refresh_kind_t refresh_decide(int nothing_on_glass, int partials_since_full,
                              int partial_limit, int hours_since_full)
{
    /* Order matters only for readability — every branch returns FULL, so the checks could
     * be reordered without changing behaviour. They are listed most-specific first so the
     * reason for a full refresh is obvious from the code. */
    if (nothing_on_glass)                      return REFRESH_FULL;   /* nothing to diff */
    if (partial_limit <= 0)                    return REFRESH_FULL;   /* partials disabled */
    /* A negative count means the count cannot be trusted, and `-1 >= 5` being false would
     * otherwise read as "budget still available". Fail toward the full refresh. */
    if (partials_since_full < 0)               return REFRESH_FULL;
    if (partials_since_full >= partial_limit)  return REFRESH_FULL;   /* budget spent */
    if (hours_since_full >= 24)                return REFRESH_FULL;   /* datasheet rule */
    return REFRESH_PARTIAL;
}

int boot_needs_last_good_draw(int woke_from_timer)
{
    return woke_from_timer ? 0 : 1;
}
