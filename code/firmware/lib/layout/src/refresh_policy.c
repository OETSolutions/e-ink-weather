#include "refresh_policy.h"

refresh_kind_t refresh_decide(int partials_since_full, int partial_limit,
                              int hours_since_full)
{
    /* Cold boot: nothing has been drawn since the last full, so the panel may be showing
     * an image of unknown age. Start clean. */
    if (partials_since_full <= 0) return REFRESH_FULL;

    /* Budget spent. This single comparison also covers a non-positive limit (0 or a
     * negative from a corrupt config): at this point partials_since_full >= 1, so
     * `1 >= limit` is true for any limit <= 0 and the refresh is full. A separate
     * `partial_limit <= 0` branch would be unreachable-as-a-difference — an equivalent
     * mutant — so it is deliberately not written. */
    if (partials_since_full >= partial_limit) return REFRESH_FULL;

    /* Datasheet rule (FR-10): the panel must be fully refreshed at least every 24 h or
     * ghosting/image sticking occurs. This is an INDEPENDENT forced-full condition — it is
     * not "later" in any meaningful sense, since all four branches return the same value,
     * so the order of the checks above cannot mask it. */
    if (hours_since_full >= 24) return REFRESH_FULL;

    return REFRESH_PARTIAL;
}
