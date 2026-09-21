#include "vbat_history.h"

#include <string.h>

/* The pure half of the battery history (FR-33). No RTC, no ADC, no time — just the ring, so it
 * is host-testable (NFR-6). The device owns the storage; this owns the rules that make arbitrary
 * retained bytes safe to read. */

void vbat_history_init(vbat_history_t *h)
{
    if (!h) return;
    memset(h, 0, sizeof(*h));
    h->magic = VBAT_HISTORY_MAGIC;
}

int vbat_history_valid(const vbat_history_t *h)
{
    if (!h) return 0;
    if (h->magic != VBAT_HISTORY_MAGIC) return 0;
    if (h->count > VBAT_HISTORY_MAX) return 0;
    if (h->head  >= VBAT_HISTORY_MAX) return 0;
    return 1;
}

void vbat_history_push(vbat_history_t *h, int32_t millivolts)
{
    if (!h) return;
    /* A negative reading is an ADC failure, not a voltage. Recording it would put a large
     * negative sample into the trend and make a healthy pack look like it was collapsing. */
    if (millivolts < 0) return;

    /* Heal a corrupt ring rather than write into it: a stale `head` past the array would be an
     * out-of-bounds store, which is precisely the corruption this validity rule exists to keep
     * out of the read path. On a real cold boot the magic is absent, so this is the normal path. */
    if (!vbat_history_valid(h)) vbat_history_init(h);

    h->millivolts[h->head] = millivolts;
    h->head = (uint16_t)((h->head + 1) % VBAT_HISTORY_MAX);
    if (h->count < VBAT_HISTORY_MAX) h->count++;
}

int vbat_history_count(const vbat_history_t *h)
{
    if (!h) return 0;
    return h->count > VBAT_HISTORY_MAX ? VBAT_HISTORY_MAX : (int)h->count;
}

int32_t vbat_history_at(const vbat_history_t *h, int i)
{
    const int n = vbat_history_count(h);
    if (i < 0 || i >= n) return -1;

    /* Once the ring is full, the oldest sample sits at `head` (the slot about to be overwritten).
     * Before that, samples were written from 0 and nothing has wrapped. */
    const int start = (h->count >= VBAT_HISTORY_MAX) ? (int)h->head : 0;
    return h->millivolts[(start + i) % VBAT_HISTORY_MAX];
}

double vbat_history_trend(const vbat_history_t *h, double elapsed_minutes)
{
    const int n = vbat_history_count(h);
    /* One sample cannot show a trend, and neither can zero. Returning 0.0 rather than dividing by
     * a zero span keeps a cold boot from reporting a phantom slope — power_classify() reads a
     * non-negative trend as "not falling", which is the safe default for an unknown. */
    if (n < 2 || !(elapsed_minutes > 0.0)) return 0.0;

    const int32_t first = vbat_history_at(h, 0);
    const int32_t last  = vbat_history_at(h, n - 1);
    if (first < 0 || last < 0) return 0.0;

    return ((double)last - (double)first) / 1000.0 / elapsed_minutes;
}
