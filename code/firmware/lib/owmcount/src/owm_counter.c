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
    if (!c->have_day || day != c->day) {
        /* First call ever, or the first of a new calendar day: the quota has reset, so the
         * previous count is discarded wholesale rather than decayed. */
        c->day = day;
        c->calls = 0;
        c->have_day = 1;
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
