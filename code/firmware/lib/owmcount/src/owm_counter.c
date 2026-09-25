#include "owm_counter.h"

/* Seconds in a calendar day. The day index is unix-seconds / 86400, i.e. UTC days — close
 * enough to the provider's own boundary, and the cap is a tripwire at 1000 calls against an
 * expected ~100/day, so an hour of timezone skew cannot matter. */
#define SECONDS_PER_DAY 86400L

void owm_counter_init(owm_counter_t *c)
{
    if (!c) return;
    c->day = 0;
    c->calls = 0;
    c->have_day = 0;
}

int owm_counter_should_call(const owm_counter_t *c, int cap)
{
    if (!c) return 1;
    /* Unknown day fails OPEN. Refusing to fetch when the count cannot be maintained would
     * permanently brick the display, which is a far worse outcome than one uncounted call. */
    if (!c->have_day) return 1;
    return c->calls < cap;
}

void owm_counter_note_call(owm_counter_t *c, long now_unix, int cap)
{
    if (!c || now_unix <= 0) return;

    const long day = now_unix / SECONDS_PER_DAY;
    if (!c->have_day) {
        /* First timestamp ever: it establishes the day. */
        c->day = day;
        c->calls = 0;
        c->have_day = 1;
    } else if (day > c->day) {
        /* A LATER calendar day is a quota boundary: the previous count is discarded wholesale
         * rather than decayed. This is the only event that resets the count.
         *
         * A SAME-OR-EARLIER day must NOT reset, and both halves of that matter.
         *
         * An earlier day cannot be a real boundary — real time only moves forward — so it can
         * only be a stale response, and treating it as a boundary would let the cap be evaded.
         *
         * A later day is not always a boundary either. 2.5/forecast carries no "now": its only
         * timestamp is "list"[0]."dt", the NEXT 3-hourly slot, so once the device is in the last
         * three hours of a UTC day that slot is 00:00 TOMORROW. Crediting one fetch by that slot
         * and the other by the 2.5/weather observation alternated the day between two values on
         * every tick, resetting the count to 1 each time — measured on the bench 2026-09-24:
         * owm_day_calls pinned at 1 while partials_since_full advanced normally, i.e. the cap
         * was dead. Taking the LATEST day seen makes the count monotonic, so both fetches in a
         * tick accumulate into one day and the tripwire actually fires. The cost is that the day
         * index can lead the true UTC day by up to the slot offset (3 h); against a cap that is
         * a tripwire at 1000 calls versus an expected ~480/day, that is immaterial. */
        c->day = day;
        c->calls = 0;
    }
    /* Clamped, so the reported count is never larger than the cap it is compared against. */
    if (c->calls < cap) c->calls++;
}

int owm_counter_calls(const owm_counter_t *c)
{
    return c ? c->calls : 0;
}

int owm_counter_known(const owm_counter_t *c)
{
    return c ? c->have_day : 0;
}
