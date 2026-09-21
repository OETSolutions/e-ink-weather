#pragma once

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdint.h>

/* Opt-in heap tracing for the refresh/render path.
 *
 * WHY THIS IS PERMANENT CODE RATHER THAN A PATCH APPLIED WHEN NEEDED: verifying this device's
 * DRAM behaviour used to mean adding ESP_LOGI("DIAG", ...) calls, flashing (a ~40 s cycle),
 * reading the log, then REMOVING the instrumentation and flashing again — and doing that once per
 * question. The instrumentation was rewritten from scratch every round, and the removal step was
 * its own source of bugs (a botched strip left duplicate lines and broke the build once).
 *
 * Keeping it behind a compile-time flag costs nothing in the shipped image: with the flag unset,
 * HEAP_DIAG() expands to nothing at all, so there is no code, no string and no branch. Turn it on
 * for a bench round with `pio run -e esp32dev-trace -t upload`, and every interesting point in the
 * render window reports the two numbers that actually matter.
 *
 * WHICH TWO NUMBERS, AND WHY THOSE: a framebuffer is ONE contiguous 78,200-byte allocation, so
 * `free_heap` (the total) is the wrong number — a device with 130 KB free but no 78 KB hole
 * cannot draw, and a log quoting only the total sends the reader hunting a leak that is not
 * there. `largest_free_block` is the number that predicts success or failure; the total is only
 * useful NEXT TO it, because "total high, largest low" means fragmentation and "both low" means
 * a genuine leak. Printing one without the other is what made the earlier rounds slow. */
#ifndef HEAP_TRACE
#define HEAP_TRACE 0
#endif

#if HEAP_TRACE
/* `label` is a plain string literal at every call site, so the cost is one log line per point. */
#define HEAP_DIAG(label)                                                      \
    do {                                                                      \
        ESP_LOGI("heaptrace", "%-28s free=%7u largest=%7u",                   \
                 (label),                                                     \
                 (unsigned)esp_get_free_heap_size(),                          \
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)); \
    } while (0)

/* The full region breakdown. Only for the ONE point that needs it (a failed allocation): the
 * per-region dump is ~12 lines, so calling it per refresh would bury the log it is meant to
 * make readable. */
#define HEAP_DUMP(label)                                                      \
    do {                                                                      \
        ESP_LOGW("heaptrace", "--- heap regions at %s ---", (label));         \
        heap_caps_print_heap_info(MALLOC_CAP_8BIT);                           \
    } while (0)

/* The BLOCK-LEVEL dump, which is what identifies a fragmenter. The per-region summary proves
 * fragmentation (total high, largest low) but not its CAUSE: only the block list shows the
 * allocations sitting between the free pieces.
 *
 * SCOPED TO ONE REGION, because the whole-heap version does not survive the UART. heap_caps_dump()
 * prints every block in every region — ~600 lines here — which overflows the log and TRUNCATES
 * the region being examined, and the region big enough to hold a framebuffer is always the last
 * one printed. So pass one finds the region holding the largest free block (the only region a
 * framebuffer could come from), and pass two lists EVERY block in just that region. No size filter:
 * the fragmenting allocations are tiny (measured: 212 bytes total split a 112 KB region), so a
 * "big blocks only" filter hides exactly the blocks being hunted.
 *
 * The callbacks do NOT log. heap_caps_walk() holds the heap lock for the whole traversal and
 * ESP_LOGW can allocate, which aborts against that lock (observed: abort() on the first dump), and
 * an unfiltered ESP_LOGW per block starved the idle task into an interrupt-WDT reset (observed).
 * So the walkbacks only copy, and the printing happens after the lock is released. */
typedef struct {
    intptr_t want;   /* region start of the largest free block (pass one) */
    size_t   best;   /* largest free block seen (pass one) */
    struct { const void *p; unsigned size; int used; } b[64];
    int n;
} heap_big_ctx_t;

static inline bool heap_scan_walker(walker_heap_into_t h, walker_block_info_t b, void *ud)
{
    heap_big_ctx_t *c = (heap_big_ctx_t *)ud;
    if (!b.used && b.size > c->best) { c->best = b.size; c->want = h.start; }
    return true;
}

static inline bool heap_dump_walker(walker_heap_into_t h, walker_block_info_t b, void *ud)
{
    heap_big_ctx_t *c = (heap_big_ctx_t *)ud;
    if (h.start == c->want && c->n < (int)(sizeof(c->b) / sizeof(c->b[0]))) {
        c->b[c->n].p    = b.ptr;
        c->b[c->n].size = (unsigned)b.size;
        c->b[c->n].used = b.used ? 1 : 0;
        c->n++;
    }
    return true;
}

#define HEAP_BLOCKS(label)                                                    \
    do {                                                                      \
        static heap_big_ctx_t s_ctx;                                          \
        s_ctx.n = 0; s_ctx.best = 0; s_ctx.want = 0;                          \
        heap_caps_walk(MALLOC_CAP_8BIT, heap_scan_walker, &s_ctx);            \
        heap_caps_walk(MALLOC_CAP_8BIT, heap_dump_walker, &s_ctx);            \
        ESP_LOGW("heaptrace",                                                 \
                 "--- region %p (largest free %u) at %s: %d blocks ---",      \
                 (void *)s_ctx.want, (unsigned)s_ctx.best, (label), s_ctx.n); \
        for (int _i = 0; _i < s_ctx.n; _i++) {                                \
            ESP_LOGW("heaptrace", "  %s %6u B @ %p",                         \
                     s_ctx.b[_i].used ? "USED" : "FREE",                      \
                     s_ctx.b[_i].size, s_ctx.b[_i].p);                        \
        }                                                                     \
    } while (0)
#else
#define HEAP_DIAG(label) do { } while (0)
#define HEAP_DUMP(label) do { } while (0)
#define HEAP_BLOCKS(label) do { } while (0)
#endif
