#include "app_refresh.h"
#include "api.h"
#include "api_store.h"
#include "canvas.h"
#include "cfg_store.h"
#include "cJSON.h"
#include "datasrc.h"
#include "epd.h"
#include "fonts.h"
#include "geo_ip.h"
#include "ha.h"
#include "layout_model.h"
#include "net_http.h"
#include "net_wifi.h"
#include "nvs_keys.h"
#include "owm.h"
#include "provscreen.h"
#include "prov.h"
#include "refresh_policy.h"
#include "render.h"
#include "thermal_guard.h"
#include "value_resolve.h"
#include "widgets.h"
#include "esp_heap_caps.h"
#include "heap_trace.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs.h"
#include <string.h>
#include <stdlib.h>

/* The vendor demo reference image, used as the static layer until the web app has uploaded
 * one (Task 21). Declared here rather than included, because it lives in src/ and this is a
 * component — and because it is a placeholder to be deleted, so the dependency should be
 * one line, not an include path. */
/* The boot mark shown when no layout bitmap has been uploaded yet. Generated from the
 * OETSolutions / Aether artwork by tools/gen_boot_logo.py — see that script for the pipeline.
 * This replaced the vendor GoodDisplay demo image, which had no business on a shipped
 * device: it was a bring-up baseline, not artwork. */
extern const uint8_t boot_logo[EPD_FB_BYTES];
/* The header-sized version of the same mark, for the provisioning screen. Generated together
 * with the splash so the two cannot drift apart. */
extern const uint8_t boot_badge[];
extern const int boot_badge_w;
extern const int boot_badge_h;

static const char *TAG = "refresh";

/* How long ensure_static() waits for the freed static-layer region to coalesce before giving up
 * on a tick.
 *
 * MEASURED, NOT GUESSED. When the region is fragmented by a network allocation sitting in the
 * middle of it, the largest free block reads 77,824 — 376 bytes SHORT of the 78,200 a
 * framebuffer needs — and the block walk shows the fragmenter is a handful of 28..132-byte
 * network-stack allocations that free on their own. The pieces then coalesce into one
 * 78,380-byte block, which DOES fit, but the coalesce is not instant: under sustained 2 s HTTP
 * churn the wait was measured acquiring on attempts 16..29, i.e. 800..1,450 ms, with the FIRST
 * acquisition needing longer than the old 5 s ceiling in the worst observed case. So 5 s was
 * marginal and 15 s is a comfortable margin over a 1.45 s measurement.
 *
 * A tick that still cannot get its frame keeps the previous image on the glass, which is the
 * correct FR-29 behaviour, and it RETRIES shortly (see the retry in app_refresh_tick) rather
 * than waiting a whole interval — a stale panel for 15 minutes is the real cost being avoided.
 * On battery this loop is never entered under normal use: the radio is torn down before the
 * render window, so the region is already clean. */
#define FB_ACQUIRE_WAIT_MS 15000
#define FB_ACQUIRE_POLL_MS 50

/* The slot identity meaning "no picture is known to be on the glass", i.e. a partial has no valid
 * base. Distinct from any REAL identity a layout can have: the artwork sentinels are -(100 + page)
 * for page 0..7 (-100..-107), the legacy bitmap uses 0 or 1, and the built-in boot mark uses -200
 * (SLOT_BOOT_LOGO) — so -1 cannot collide with an identity that is genuinely reproducible.
 *
 * The distinction matters, because it is not a sign test: the artwork sentinel is NEGATIVE, so
 * `slot < 0` is true for every real uploaded layout. See s_shown_slot. */
#define SLOT_NONE      (-1)
#define SLOT_BOOT_LOGO (-200)

/* The fields to stamp one refresh, and their values. Sized by the parse caps rather than allocated.
 * This is the TICK-LOCAL page, which also carries the parsed widgets and the FR-27 reporting arrays;
 * only the three members a render needs are copied into the cross-tick snapshot below. */
typedef struct {
    layout_widget_t widgets[LAYOUT_MAX_FIELDS];
    value_field_t   fields[LAYOUT_MAX_FIELDS];
    char            values[LAYOUT_MAX_FIELDS][40];
    const char     *value_ptrs[LAYOUT_MAX_FIELDS];
    /* The widget id and "was this a real reading" flag, parallel to values/fields, so
     * GET /api/values (FR-27) can report which box each resolved string belongs to. The id is
     * copied rather than pointed at: the widget array is overwritten by the next refresh. */
    char            ids[LAYOUT_MAX_FIELDS][24];
    int             has_value[LAYOUT_MAX_FIELDS];
    int             n;              /* how many the page asked for */
    int             n_fields;       /* how many are actually stamped (dynamic only) */
} page_render_t;

/* JUST THE DRAWABLE HALF OF A PAGE: the boxes, their strings, and the count.
 *
 * WHY THIS IS A SEPARATE, SMALL TYPE. The previous frame must be reproducible across ticks, and
 * page_render_t is 9,320 bytes because it also carries the parsed widgets (24 x 288) and the
 * FR-27 reporting arrays — none of which a render reads. Keeping one of those resident costs
 * ~9 KB of .bss, and .bss on this part is DRAM the heap does not get: measured, adding a 9,320-byte
 * static here dropped the largest free block below the 78,200 bytes the static layer needs, so
 * NOTHING could be drawn. The drawable half is 1,640 bytes, which fits. */
typedef struct {
    value_field_t fields[LAYOUT_MAX_FIELDS];
    char          values[LAYOUT_MAX_FIELDS][40];
    const char   *value_ptrs[LAYOUT_MAX_FIELDS];
    int           n_fields;
} frame_values_t;

/* What a render needs, pointing at either a live page or the snapshot. `values` is `const char **`
 * rather than `const char *const *` so a live page's array can be used without a cast. */
typedef struct {
    const value_field_t *fields;
    const char *const   *values;
    int                  n_fields;
} frame_spec_t;

/* ONE 78,200-byte buffer, holding the STATIC LAYER of the page currently on the glass — not the
 * composed frame.
 *
 * WHY THE STATIC LAYER AND NOT THE FRAME: a partial refresh passes the SSD2677 the previous and
 * the next frame, because it derives each pixel's transition from the pair. Two frames of 78,200
 * bytes do not fit this part — the only DRAM region large enough for one is 113,840 bytes and the
 * pair needs 156,400 — so a second framebuffer NEVER allocated. Measured on the bench: the
 * transient allocation failed on every refresh and /api/status read `partials_since_full: 0`
 * against `fulls_total: 6`, so FR-11's partial path had never run and every refresh was a full
 * panel flash.
 *
 * Holding the static layer instead makes both frames derivable from ONE buffer: the previous frame
 * is this layer stamped with s_prev_values' strings, and the next frame is the same layer stamped
 * with the current page's values. A partial is only ever taken when the slot identity matches,
 * which means the two frames genuinely share this layer — so it is the right thing to keep.
 *
 * Allocated lazily and FREED ACROSS EVERY FETCH (see app_refresh_tick): it competes for the one
 * region the TLS handshake also needs, and the handshake wins because a fetch that fails leaves
 * every reading as "--".
 *
 * IT IS HEAP-ALLOCATED, NOT STATIC: as an array it would sit in the linker's static DRAM region
 * (dram0_0_seg, 180,736 bytes), which is what made an earlier two-buffer version fail to link. */
static uint8_t *s_static;
/* The identity of the picture on the glass, from load_static_layer(): a NEGATIVE sentinel for
 * per-page artwork (-(100+page)), the bitmap's slot for the legacy single bitmap, SLOT_BOOT_LOGO
 * for the built-in boot mark, SLOT_NONE when nothing is known. Compared for equality to decide
 * whether a partial is valid.
 *
 * IT MUST NOT DOUBLE AS THE "IS ANYTHING ON THE GLASS" FLAG, and that is a bug this code had: the
 * artwork sentinel is negative, so `s_shown_slot < 0` was true for every real uploaded layout and
 * every tick read "nothing on the glass" and forced a FULL refresh — which is exactly why
 * /api/status showed `partials_since_full: 0` no matter what the partial budget said. The two
 * questions are different and now have different answers: identity here, presence in
 * s_glass_present below. */
static int      s_shown_slot = SLOT_NONE;
/* Non-zero once a push has put an image on the glass and nothing has overwritten or dropped it
 * since. This, not the sign of the slot, is what refresh_decide() means by "something is on the
 * glass to diff against". */
static int      s_glass_present;

/* The values that were stamped into the frame now on the glass, so the PREVIOUS frame can be
 * recomposed for a partial. This is the whole reason a partial is possible here: the previous
 * frame is not stored (two frames do not fit), it is REPRODUCED from the resident static layer
 * plus these values, and 24 strings of 40 bytes is 1.6 KB rather than 78,200. */
static frame_values_t s_prev_values;
static int            s_have_prev_values;   /* 0 until a push has recorded what it drew */

/* Allocate the static layer on first use. Returns 0 on success, -1 if the allocation failed. Held
 * across the push and released across the fetch (see app_refresh_tick). */
static int ensure_static(void)
{
    if (s_static) return 0;

    s_static = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);
    if (s_static) return 0;

    /* THE STATIC LAYER HAS TO WAIT FOR THE NETWORK STACK TO LET GO.
     *
     * Measured on the bench (HEAP_TRACE probe): this device has exactly ONE DRAM region large
     * enough to hold a 78,200-byte buffer — region 0x3ffe4350, ~113 KB (every other region
     * caps at ~66 KB). It is freed across every fetch for TLS, and when it is freed the region
     * reads as 99.8% free yet is split by a handful of TINY network-stack allocations (the block
     * walk shows 84 B, 112 B, 128 B and several 28-36 B pieces) into pieces such as
     * 14,256 + 34,788 + 63,596 — none of which fits. `free_heap` is ~128 KB in BOTH the failing
     * and the succeeding case, which is why the old message sent readers hunting a leak: there is
     * none. This is fragmentation, pure and simple.
     *
     * The pieces COALESCE back into one 98,304-byte block on their own — measured by polling on a
     * failing tick: after ~800 ms the largest block jumped 69,632 -> 98,304 while `free` did not
     * move (129,492 before and after). So the fix is to wait for the coalesce rather than fail.
     *
     * HOW LONG TO WAIT: at the device's REAL refresh cadence this loop is never entered at all —
     * measured over a 20-tick USB soak at a 30 s interval, the first malloc succeeded every time
     * with the full 98,304-byte block free, and no tick waited. The wait only appears under
     * SUSTAINED fast HTTP churn (the bench harness's 2 s interval plus an artwork upload), where
     * the clear took 800 ms to several seconds. 15 s is a comfortable margin over a 1.45 s
     * measurement, and a tick that still cannot get its buffer keeps the previous image on the
     * glass — the correct FR-29 behaviour — and RETRIES shortly rather than waiting a whole
     * interval.
     *
     * On battery this loop is never reached under normal use: the radio is torn down before the
     * render window, so the region is already clean. */
    const int attempts = FB_ACQUIRE_WAIT_MS / FB_ACQUIRE_POLL_MS;
    for (int attempt = 0; attempt < attempts; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(FB_ACQUIRE_POLL_MS));
#if HEAP_TRACE
        if (attempt % 5 == 0) {
            ESP_LOGW("heaptrace", "  static-layer wait %2d: largest=%u free=%u", attempt,
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                     (unsigned)esp_get_free_heap_size());
        }
#endif
        s_static = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);
        if (s_static) {
#if HEAP_TRACE
            if (attempt > 0) {
                ESP_LOGW("heaptrace", "  static layer acquired on attempt %d (~%d ms)",
                         attempt, attempt * FB_ACQUIRE_POLL_MS);
            }
#endif
            return 0;
        }
    }

    {
        /* The largest free block is reported, not just the total. This is one contiguous 76 KiB
         * allocation, so total free heap is the wrong number to look at: a device with 200 KiB
         * free but no 76 KiB hole cannot draw, and a message quoting only the total sends the
         * reader looking for a leak that is not there. */
        ESP_LOGE(TAG, "cannot allocate the static layer (%u bytes) "
                      "(free heap %u, largest block %u)",
                 (unsigned)EPD_FB_BYTES,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        /* The region breakdown, only here: this is the one point where "which block is in the
         * way" is the question, and the dump is too large to print per refresh. */
        HEAP_DUMP("static layer allocation failed");
        HEAP_BLOCKS("static layer allocation failed");
        return -1;
    }
}

/* Set when a tick could not get its static layer and therefore drew nothing. The image
 * on the glass is still the last good one (correct FR-29 behaviour), but the region may stay
 * fragmented for far longer than the acquire wait — measured: a state that held the largest
 * block at ~38 KB for 30 s+ with only ~230 bytes of small USED blocks in the way. So the serve
 * loop retries promptly on this flag, via the pending-refresh request, rather than leaving the
 * panel stale for a whole interval. A full refresh is requested because there is no resident
 * diff base left. */
static volatile int s_fb_lost;

int app_refresh_frame_lost(void) { return s_fb_lost; }

/* The last temperature this device successfully fetched, and when.
 *
 * WHY IT IS REMEMBERED: a failed fetch must not blank the reading. Without this, an OWM
 * outage would replace a real temperature with "--" on the next render — and since a bitmap
 * upload also triggers a render (so the web app can see what it uploaded), a user who had
 * just pushed a new layout would watch their temperature disappear for reasons that have
 * nothing to do with the layout. Keeping the last value means a transient network failure
 * costs nothing on the glass, which is the same principle as FR-29's last-good image.
 *
 * See s_last_current, declared further down, for what is actually kept. */


/* The last successful full refresh, for the datasheet's 24 h rule. Only full refreshes set
 * it: a partial does not clear ghosting, so it cannot postpone the need for one.
 *
 * Deliberately NOT persisted across deep sleep. The panel's own glass holds the image across
 * a sleep, but the RTC keeps counting, so this stays meaningful for a device that sleeps.
 * A cold boot resets it to 0, which makes the first refresh a full one — the safe default,
 * since the firmware cannot know what a power cut left on the glass. */
static int64_t s_last_full_us;

/* Whole hours since the last full refresh. 0 before the first one, which refresh_decide()
 * treats as "refresh fully" anyway. */
static int hours_since_full(void)
{
    if (s_last_full_us == 0) return 0;
    const int64_t h = (esp_timer_get_time() - s_last_full_us) / 3600000000LL;
    return h > 24 ? 24 : (int)h;    /* clamp: past the boundary the exact value is moot */
}

/* Read the static layer from flash through the streaming renderer, so the 78,200-byte
 * image is never resident in RAM on top of the two framebuffers (which would be 235 KB). */
static int flash_reader(void *ctx, size_t offset, uint8_t *dst, size_t len)
{
    const uint8_t *base = (const uint8_t *)ctx;
    memcpy(dst, base + offset, len);
    return 0;
}

/* Pull the static layer into `dst`. Returns 0 on success.
 *
 * Preference order: the page's OWN artwork, then the single legacy bitmap, then the built-in boot
 * mark. A device that has never had anything pushed still renders something meaningful rather
 * than a blank panel (FR-29).
 *
 * `page` IS WHAT MAKES ROTATION CORRECT. With one shared layer (the old behaviour), a rotating
 * page stamped its readings onto ANOTHER page's artwork — page 2's "TOMORROW HIGH" label sitting
 * over page 1's temperature — which was seen on hardware. The page's own picture is therefore
 * tried FIRST, and only a page that genuinely has none falls back. */
static int load_static_layer(uint8_t *dst, int page, int *from_slot)
{
    if (artwork_store_load_page(page, dst) == 0) {
        /* THE IDENTITY MUST INCLUDE THE PAGE, not just "artwork". A partial refresh is diffed
         * against the previous frame and is only valid when both are the SAME picture; during
         * rotation the previous frame is the PREVIOUS PAGE's artwork. Returning one shared
         * sentinel for every page would make a page change look like an unchanged source, and the
         * partial would diff page 2's new frame against page 1's old one — scribbling ghosted
         * fragments of the previous layout onto the glass. Encoding the page keeps the check
         * honest: different page, different identity, therefore a full refresh. */
        *from_slot = -(100 + page);
        return 0;
    }
    if (bitmap_store_load(dst) == 0) {
        *from_slot = api_live_bitmap_slot();
        return 0;
    }
    memcpy(dst, boot_logo, EPD_FB_BYTES);
    /* The built-in boot mark is REPRODUCIBLE — it is a const array in flash, not an upload — so it
     * gets a real identity of its own rather than SLOT_NONE. It must not share -1 with "unknown",
     * because the two mean opposite things: this one can be a valid diff base, and a partial of the
     * boot mark against itself is a genuine no-op rather than corruption. -200 cannot collide with
     * the artwork sentinels (-(100+page) = -100..-107) or the bitmap slots (0 or 1). */
    *from_slot = SLOT_BOOT_LOGO;
    return 0;
}

/* ------------------------------------------------------------------------------------------
 * THE LAYOUT IS THE WEB APP'S, NOT THE FIRMWARE'S
 *
 * This used to be `static const value_field_t DEFAULT_FIELDS[2]` — two boxes at fixed
 * coordinates, stamped with a temperature and a "--". The device therefore rendered a layout
 * nobody had authored: a user's pushed layout was parsed for scheduling only and then thrown
 * away, so every widget but the current temperature was blank, and the one reading that did
 * appear sat in a box the user had not drawn.
 *
 * The boxes now come from the document (lib/layout/src/widgets.c), the values from
 * lib/layout/src/value_resolve.c, and the page from the rotation schedule. The firmware still
 * never INVENTS a position or a label — it stamps the readings the web app's layout asked for
 * (FR-1). Keeping that property is what lets a layout change ship with no firmware update.
 * ------------------------------------------------------------------------------------------ */

/* Turn the parsed widgets into the renderer's field/value arrays.
 *
 * Only DYNAMIC widgets are stamped: a 'static' widget is baked into the bitmap by the web app
 * and stamping it again would put a reading on top of the chrome. */
static void build_fields(page_render_t *p, const value_sources_t *src,
                         char (*ids)[48], int n_ids, long now)
{
    int nf = 0;
    for (int i = 0; i < p->n; i++) {
        const layout_widget_t *w = &p->widgets[i];
        if (w->role != 'd') continue;
        if (w->w <= 0 || w->h <= 0) continue;       /* nothing to clip to */

        value_field_t *f = &p->fields[nf];
        f->x = w->x; f->y = w->y; f->w = w->w; f->h = w->h;
        f->align_h = w->align_h;
        f->align_v = w->align_v;
        f->font_id = w->font_id;

        /* The buffer is per-field and the pointer array is parallel to `fields`, because
         * render_compose_stream() takes `const char *const *` and does not own the strings. */
        p->has_value[nf] = value_format_widget(w, src, ids, n_ids, now, NULL,
                                               p->values[nf], sizeof(p->values[nf]));
        p->value_ptrs[nf] = p->values[nf];
        /* The widget's own id travels with the value, for GET /api/values (FR-27): the editor
         * previews the REAL fetched data by asking the device what it resolved, and without the
         * id the app could not tell which box a string belongs to. */
        strncpy(p->ids[nf], w->id, sizeof(p->ids[nf]) - 1);
        p->ids[nf][sizeof(p->ids[nf]) - 1] = '\0';
        nf++;
    }
    p->n_fields = nf;
}

/* ------------------------------------------------------------------------------------------
 * PUSHING A FRAME OUT OF ONE STATIC LAYER
 *
 * A partial refresh hands the SSD2677 the frame CURRENTLY ON THE GLASS and the frame it should
 * show next, and the controller derives each pixel's transition from the pair. That pair is why
 * FR-11's partial path had never run on this hardware: two 78,200-byte frames do not fit — the
 * only DRAM region large enough for one is 113,840 bytes and the pair needs 156,400 — so the
 * second framebuffer failed to allocate on EVERY refresh. Measured on the bench: the transient
 * allocation failed on every attempt and /api/status read `partials_since_full: 0` against
 * `fulls_total: 6`, so every refresh was a full panel flash and the whole FR-11 strategy was
 * dead code that reported success.
 *
 * The frames are therefore not STORED, they are REPRODUCED. One buffer holds the page's static
 * layer (the picture the web app uploaded, the expensive half at 78,200 bytes); the values that
 * were stamped on it last time live in s_prev_values, which is 1.6 KB. Stamping
 * those strings onto the layer gives the frame on the glass, and stamping the new values
 * gives the next frame — so both are produced a BAND at a time, into two buffers of
 * RENDER_BAND_ROWS rows each, and neither whole frame ever exists in RAM.
 *
 * The layer must be the SAME for both frames, which is guaranteed rather than assumed: the caller
 * passes `partial` only when the slot identity matches (see app_refresh_tick), and a slot encodes
 * the picture (load_static_layer). Different page, different artwork, different bitmap => a full
 * refresh, because a partial across two unrelated pictures would leave ghosted fragments of the
 * old layout on the glass.
 *
 * WHY THE BAND BUFFERS ARE ALLOCATED PER PUSH AND FREED AGAIN: this part has exactly one region
 * large enough for the static layer, and that region is what the TLS handshake needs on every
 * fetch. A permanently-held band pair is memory the radio cannot have at boot — the exact
 * starvation that leaves an unprovisioned device unable to be set up (see app_fbs_reserve()).
 * Held only inside the push, they cost nothing the radio can see. */

/* The band size, in rows. 136 divides 680 exactly, giving 5 bands and a 15,640-byte buffer each —
 * the LEAST work per push, which is why it is the first choice. It is only the first choice,
 * though: on USB the radio keeps running for the API (FR-31), and the static layer plus the radio's
 * own allocations leave the largest free block around 14,848 bytes — measured on the bench, 792
 * bytes SHORT of a 15,640-byte band, so the push failed with ESP_ERR_NO_MEM on every tick. Rather
 * than pin the band to whatever happens to fit today, the size is found by TRIAL: halve it until
 * both buffers fit. Each halving is a divisor of 680 down to 8 (680 = 2^3 * 5 * 17), so no band is
 * ever an awkward fraction.
 *
 * A smaller band is not a correctness change — the driver walks whatever rows it is given and the
 * compose is row-exact — it only costs more callback invocations. */
#define RENDER_BAND_ROWS_MAX 136
#define RENDER_BAND_ROWS_MIN 8

/* What a band provider needs: the static layer, the two value sets, the band geometry, and the
 * scratch buffers the composes read out of. */
typedef struct {
    uint8_t            *prev_band;      /* composed: what is on the glass */
    uint8_t            *next_band;      /* composed: what it should show */
    const frame_spec_t *next;
    const frame_spec_t *prev;           /* NULL for a full refresh: no previous frame is read */
    int                 band_rows;      /* the size this push settled on (see above) */
} band_push_t;

/* The driver's band provider (epd_band_fn).
 *
 * IT IS CALLED IN DESCENDING BAND ORDER — the panel scans bottom-up, so the last band goes out
 * first — and it must write its band where the driver expects it, which is why it returns the rows
 * it produced and the driver cross-checks against the rows that band actually has.
 *
 * `prev` may be NULL for a full refresh: the previous frame is never read then (a full expands the
 * next frame alone), so composing it would be wasted work — and, worse, a wasted band buffer.
 *
 * Returns 0 on success. A compose failure propagates as a non-zero return, which the driver turns
 * into ESP_ERR_INVALID_STATE and the caller into a failed push that leaves the old image up. */
static int band_provider(void *ctx, int band_index,
                         uint8_t *prev_out, uint8_t *next_out, int *rows_out)
{
    band_push_t *b = (band_push_t *)ctx;
    const int y0 = band_index * b->band_rows;
    const int rows = (EPD_HEIGHT - y0 < b->band_rows) ? (EPD_HEIGHT - y0) : b->band_rows;

    canvas_t c;

    canvas_init_band(&c, b->next_band, y0, rows);
    if (render_compose_band(&c, flash_reader, s_static,
                            b->next->fields, b->next->values, b->next->n_fields) != 0) {
        return -1;
    }

    if (b->prev) {
        canvas_init_band(&c, b->prev_band, y0, rows);
        if (render_compose_band(&c, flash_reader, s_static,
                                b->prev->fields, b->prev->values, b->prev->n_fields) != 0) {
            return -1;
        }
    }

    *rows_out = rows;
    (void)prev_out;     /* the driver hands us the buffers we already have in `b` */
    (void)next_out;
    return 0;
}

/* Push the composited page, band by band, out of the resident static layer. Returns ESP_OK, or an
 * error the caller surfaces and records without changing the glass.
 *
 * `partial` selects the wire format AND how much is composed: a partial needs the previous frame as
 * well, a full does not. The band buffers are taken here, because only the caller knows whether the
 * partial path is live yet — and they are freed on every exit, including the failures, because a
 * leaked pair is memory the next fetch's TLS handshake needs. */
static int push_banded(const frame_spec_t *next, const frame_spec_t *prev, int partial)
{
    /* Find the largest band that fits, by trial. See RENDER_BAND_ROWS_MAX for why the size cannot
     * be a constant. The two buffers are taken together and released together, so a size where only
     * one fits is not accepted: a partial needs the pair, and settling for a size that cannot hold
     * both would turn every partial into a full refresh — silently, which is the failure this whole
     * change exists to remove. */
    uint8_t *prev_band = NULL;
    uint8_t *next_band = NULL;
    int band_rows = 0;

    for (int r = RENDER_BAND_ROWS_MAX; r >= RENDER_BAND_ROWS_MIN; r /= 2) {
        const size_t bytes = (size_t)r * EPD_PITCH;
        next_band = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (!next_band) continue;
        if (!partial) { band_rows = r; break; }
        prev_band = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (prev_band) { band_rows = r; break; }
        /* Only one fitted, so this size is no good for a partial: give the buffer back and try the
         * next size down rather than leaking it and failing. */
        free(next_band);
        next_band = NULL;
    }

    if (!next_band) {
        ESP_LOGE(TAG, "no band buffer fits (largest block %u); the %s refresh cannot be drawn",
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                 partial ? "partial" : "full");
        free(prev_band);
        return ESP_ERR_NO_MEM;
    }

    band_push_t b = {
        .prev_band = prev_band,
        .next_band = next_band,
        .next = next,
        /* A full refresh composes the next frame only, so the previous VALUES are not needed —
         * and passing them anyway would make the provider compose a second frame for the sole
         * purpose of the driver ignoring it. */
        .prev = partial ? prev : NULL,
        .band_rows = band_rows,
    };

    /* The driver is told the SAME band geometry the buffers were sized for; the two must agree or
     * it would walk past the end of a buffer. */
    const esp_err_t e = epd_write_frame_banded(band_provider, &b, prev_band, next_band,
                                               band_rows, partial);
    free(prev_band);
    free(next_band);
    return e;
}

/* The last temperature this device successfully fetched, and when.
 *
 * WHY IT IS REMEMBERED: a failed fetch must not blank the reading. Without this, an OWM outage
 * would replace a real temperature with "--" on the next render — and since a bitmap upload also
 * triggers a render (so the web app can see what it uploaded), a user who had just pushed a new
 * layout would watch their temperature disappear for reasons that have nothing to do with the
 * layout. Keeping the last value means a transient network failure costs nothing on the glass,
 * which is the same principle as FR-29's last-good image.
 *
 * It is the CURRENT CONDITIONS document that is kept rather than a formatted string: the widgets
 * bind to different fields of it (temp, humidity, wind, condition), so remembering the parsed
 * string would only serve whichever widget happened to be first. */
static char s_last_current[2048];
static int  s_have_last_current;

/* HW-1's operating-temperature guard.
 *
 * The panel datasheet gives TOPR 0..50 C and warns that drawing outside it produces garbage;
 * HW-1 requires the firmware to "surface a warning state rather than render garbage". The guard
 * has existed and been host-tested since Task 6b, but was never CALLED from the render path —
 * so the requirement was unmet on the device.
 *
 * `valid` is false whenever the sensor is unusable, which on this hardware is ALWAYS: the
 * SSD2677's 0x40 register reads a constant -15 C regardless of ambient (measured 2026-09-18, see
 * the spec's HW-1 note). Feeding that constant in with valid=1 would report TOO_COLD on a warm
 * bench and block every render, which is precisely the failure the guard's fourth state exists to
 * prevent. So the reading is validated against the known-stuck constant and only a reading that
 * has actually MOVED is trusted.
 *
 * Returns 1 when the render must be skipped (genuinely out of range), 0 otherwise — including
 * THERMAL_UNKNOWN, where HW-1 says render and log rather than block. */
static int thermal_blocks_render(void)
{
    static int s_boot_c = 0;
    static int s_have_prev = 0;

    int t = 0;
    const int valid = (epd_read_temp(&t) == ESP_OK);

    if (!valid) {
        if (!s_have_prev) {
            ESP_LOGW(TAG, "panel temperature unreadable; guard cannot decide, rendering");
            s_have_prev = 1;
        }
        return 0;
    }

    /* The first usable reading is remembered but cannot be classified: one sample cannot be told
     * apart from a stuck constant. It is logged so the value is on the record. */
    if (!s_have_prev) {
        s_boot_c = t;
        s_have_prev = 1;
        ESP_LOGI(TAG, "panel temperature %d C (first reading; guard needs a second to trust it)", t);
        return 0;
    }

    /* A sensor that never changes is the known-stuck case, and must not gate the render. */
    if (t == s_boot_c) {
        return 0;
    }

    const thermal_state_t th = thermal_check(t, 1);
    if (th == THERMAL_TOO_COLD || th == THERMAL_TOO_HOT) {
        ESP_LOGE(TAG, "panel temperature %d C is outside the 0..50 C operating range; "
                      "skipping this refresh (HW-1)", t);
        api_note_error(th == THERMAL_TOO_COLD ? "thermal: too cold to render"
                                              : "thermal: too hot to render");
        return 1;
    }
    return 0;
}

esp_err_t app_render_last_good(void)
{
    if (ensure_static() != 0) return ESP_ERR_NO_MEM;

    /* Page 0: the boot path shows the last-good image before the network, and it cannot know
     * which page was on the glass when the device slept. Page 0 is the defined default (a
     * single-page layout renders identically at any index). */
    int slot = -1;
    if (load_static_layer(s_static, 0, &slot) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    /* No live values yet (this runs before the network), so the frame is the static layer alone.
     * That is the point: FR-29 wants the last good image visible immediately, not a blank panel
     * during the fetch, and the values from the previous power cycle are not recoverable — the RAM
     * that held them did not survive the sleep. So this is the bare picture, and a device that has
     * never fetched shows exactly that rather than a placeholders frame.
     *
     * No values are recorded as the previous frame for the same reason: a later partial must
     * reproduce THIS frame as its "previous" one, and this frame has nothing stamped on it. */
    const frame_spec_t none = { .fields = NULL, .values = NULL, .n_fields = 0 };
    const esp_err_t e = push_banded(&none, NULL, 0);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel write failed: %s", esp_err_to_name(e));
        api_note_error("render: panel write failed");
        return e;
    }

    /* This frame is now on the glass, so it becomes the "previous" frame a later partial refresh
     * is diffed against — recorded as its VALUES, not as 78,200 bytes, because two whole frames do
     * not fit this part (see the block comment above push_banded). */
    s_prev_values.n_fields = 0;
    s_shown_slot = slot;
    s_glass_present = 1;
    s_have_prev_values = 1;
    api_record_refresh(1);       /* a full update by definition */
    /* Start the datasheet's 24 h clock here too. This IS a full refresh of the panel, so it is the
     * moment the clock restarts; leaving it at 0 would make the first tick's hours_since_full()
     * read 0 and postpone the ghosting-clearing refresh by however long this image stayed up. */
    s_last_full_us = esp_timer_get_time();
    ESP_LOGI(TAG, "last-good image pushed (slot %d)", slot);
    return ESP_OK;
}

/* The credentials every fetch needs, read once from NVS. */
typedef struct {
    char   key[64];
    double lat, lon;
    char   ha_url[128];
    char   ha_token[256];
} fetch_creds_t;

static int read_creds(fetch_creds_t *c)
{
    memset(c, 0, sizeof(*c));
    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return -1;
    size_t n = sizeof(c->key);
    nvs_get_str(h, DEVENV_KEY_OWM_KEY, c->key, &n);
    /* A missing location is not fatal: 0,0 is a defined place (the Gulf of Guinea) and the
     * resulting reading is obviously wrong, which is better than no reading and a silent
     * failure. */
    size_t llen = sizeof(double);
    nvs_get_blob(h, DEVENV_KEY_LOC_LAT, &c->lat, &llen);
    llen = sizeof(double);
    nvs_get_blob(h, DEVENV_KEY_LOC_LON, &c->lon, &llen);
    n = sizeof(c->ha_url);
    nvs_get_str(h, DEVENV_KEY_HA_URL, c->ha_url, &n);
    n = sizeof(c->ha_token);
    nvs_get_str(h, DEVENV_KEY_HA_TOKEN, c->ha_token, &n);
    nvs_close(h);
    return 0;
}

/* The forecast horizon, and the buffer that must hold the response. THE TWO ARE DEFINED TOGETHER
 * ON PURPOSE — see the note on the size below.
 *
 * OWM takes `cnt` as a count of 3-hour blocks, 8 per day, and the layout's highest `dayIndex`
 * says how many days are needed. 40 blocks is the whole 5-day horizon the free 2.5/forecast
 * product carries; asking for more returns the same data with a misleading count. */
#define FORECAST_MAX_BLOCKS OWM_FORECAST_MAX_BLOCKS

/* The forecast document buffer, and its size is a MEMORY constraint rather than a taste choice.
 *
 * MEASURED FAILURE THIS SIZING FIXES: a full 5-day/3-hour response is 16,575 bytes, and holding a
 * 20 KB buffer for it while the TLS handshake ALSO needs a contiguous in-buffer on this part made
 * the handshake fail with `mbedtls_ssl_setup returned -0x7F00` (ALLOC_FAILED) — observed as
 * "forecast request failed" and `--` in every forecast box, with a largest-free-block of only
 * 30,720 bytes at that moment. The device was competing with itself.
 *
 * So the request is bounded to the days the layout actually asks for — but the bound and this
 * buffer MUST AGREE, and they did not. The request was allowed up to cnt=40 while this was 12,288
 * bytes, and the response grows ~406 bytes per block (measured live 2026-09-20: 3,512 at cnt=8,
 * 6,819 at 16, 10,069 at 24, 13,314 at 32, 16,575 at 40). net_http refuses to hand a parser a
 * clipped document and reports ESP_ERR_NO_MEM, so every request past 3 days failed outright and
 * EVERY forecast widget fell back to "--" — while the web app happily offers Day 4 and Day 5 in
 * its day picker. The failure was invisible in the config and silent on the glass.
 *
 * The size now comes from owm_forecast_buf_bytes(), which is host-tested (test/test_owm_parse):
 * the request and the buffer are derived from one rule, so they cannot drift apart again. The
 * 17 KB this works out to is affordable because the request happens BEFORE the render window,
 * while the resident framebuffer is released on USB — the same window the old 12,288 fit in. */
#define FORECAST_BUF_BYTES ((size_t)owm_forecast_buf_bytes(FORECAST_MAX_BLOCKS))

/* Whether this device's key has One Call 3.0 available, once probed.
 *
 * TRI-STATE, and all three values are load-bearing:
 *   -1  not probed yet, or the probe could not reach a verdict
 *    0  probed: the key is definitively not subscribed (the documented 401/403)
 *    1  probed: One Call 3.0 answered
 *
 * Cached for the process lifetime because the answer is a property of the KEY, not of the
 * request: re-probing on every refresh would spend a call on a request that is already known to
 * fail, and on a 10-minute cadence that is 144 wasted calls a day against a quota FR-6a exists
 * to protect. */
static int s_onecall_available = -1;

/* Fetch the current conditions from OWM. Returns 0 on success, -1 otherwise.
 *
 * `product` decides the ENDPOINT, which is what makes the config toggle real (FR-6). One Call
 * 3.0 carries current conditions inside its single document, so on that product the forecast
 * fetch below is the one that supplies them and this returns "nothing to do" — asking
 * 2.5/weather as well would be a second call for data already held, against a quota. */
static int fetch_current(const fetch_creds_t *c, char *resp, size_t resplen,
                         datasrc_value_t *out, owm_product_t product)
{
    if (c->key[0] == '\0') {
        ESP_LOGW(TAG, "no OWM key stored; skipping fetch");
        return -1;
    }

    /* One Call 3.0 has no separate current-conditions endpoint: everything rides in the one
     * document, which fetch_forecast() requests. Composing this into the SAME buffer would mean
     * a second ~11 KB response held in the render window for no new data. */
    if (product == OWM_PRODUCT_ONECALL3) {
        memset(out, 0, sizeof(*out));
        out->status = DATASRC_ERR_UNAVAILABLE;
        return 0;
    }

    /* The daily cap is checked BEFORE the request, which is the only point at which it can
     * actually prevent a call (spec §3.4). Reaching it means roughly 7x the expected call
     * rate has occurred, so this fires only on a runaway refresh — the panel keeps its last
     * good image and the error is recorded for /api/status, where the count is also visible. */
    if (!api_owm_should_call()) {
        ESP_LOGE(TAG, "OWM daily call cap reached; not fetching (see /api/status)");
        api_note_error("owm: daily call cap reached");
        return -1;
    }

    /* `units=imperial` is REQUIRED, not cosmetic (spec §3.4). Without it OWM returns the
     * default — Kelvin — and the panel silently shows 288.4 where the user expects 59.0. The
     * failure is quiet because a Kelvin reading is still a plausible-looking number, so
     * nothing errors; it simply disagrees with the user's Home Assistant entities, which is
     * the comparison they will make. The spec settles it: `imperial` gives °F and mph and
     * matches those entities. */
    char url[512];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather"
             "?lat=%.6f&lon=%.6f&units=imperial&appid=%s", c->lat, c->lon, c->key);

    if (net_http_get_json(url, NULL, resp, resplen) != ESP_OK) {
        ESP_LOGW(TAG, "OWM request failed");
        return -1;
    }

    /* Still parsed for its own timestamp, so the daily call cap is credited to the day the
     * response belongs to. A failed request carries no timestamp, so observed_at stays 0 and
     * api_owm_note_call() ignores it: a failed request still costs a call, but crediting it to
     * the wrong day would be worse than undercounting. */
    *out = owm_parse_current_temp(resp, (long)(esp_timer_get_time() / 1000000LL));
    api_owm_note_call(out->observed_at, 1);

    return out->status == DATASRC_OK ? 0 : -1;
}

/* FR-6's One Call 3.0 probe, run once per process.
 *
 * WHY A PROBE RATHER THAN A CONFIG GUESS: One Call 3.0 needs the separate "One Call by Call"
 * subscription, and a key without it gets a 401. Verified live 2026-09-18 with this project's
 * key: 3.0 -> 401 "requires a separate subscription", 2.5/weather and 2.5/forecast -> 200. So
 * "does this key have 3.0" is not knowable from the config, and FR-6 asks for exactly this
 * auto-detection.
 *
 * THE VERDICT IS THE HTTP STATUS, NOT THE RESPONSE BODY — and that distinction is the whole
 * correctness of this function. An earlier version sized the scratch buffer at 2 KB and treated
 * ANY failure as "not subscribed". The trimmed One Call document is ~11 KB, so the buffer always
 * overflowed (net_http returns ESP_ERR_NO_MEM, deliberately, rather than handing a parser a
 * clipped document), and a key that IS subscribed was reported as unsubscribed. The consequence
 * was not confined to a log line: FR-7 then told the user on the glass that official alerts were
 * unavailable on their product, on a key that had them. So the probe now asks for the status and
 * only the documented 401/403 counts as a definite "no".
 *
 * It is a real request, and it costs a call — which is why the answer is cached: the probe runs
 * ONCE, and on a key without the subscription every later refresh goes straight to 2.5 instead
 * of paying for another 401. The cap is charged for it, because it is a call.
 *
 * Returns 1 available, 0 definitively not subscribed, -1 inconclusive (leave the question open
 * and fall back to the free pair for now). */
static int onecall_probe(const fetch_creds_t *c)
{
    if (s_onecall_available >= 0) return s_onecall_available;
    if (c->key[0] == '\0') return -1;           /* nothing to probe with */
    if (!api_owm_should_call()) return -1;      /* do not spend a call we are not allowed */

    char url[512];
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/3.0/onecall"
             "?lat=%.6f&lon=%.6f&exclude=minutely,hourly,current,daily&units=imperial&appid=%s",
             c->lat, c->lon, c->key);

    /* The body is DISCARDED — only the status decides — so this buffer is deliberately tiny. It
     * is not "big enough for the response" and must not be read as if it were: `exclude` trims
     * the document, but even the alerts-only remainder exceeds 2 KB on a real key, so the request
     * is EXPECTED to report overflow. That is fine here precisely because the status is still
     * reported: esp_http_client_perform() completes and the status is read before the sink's
     * overflow flag is consulted, so a 200 with a clipped body still proves the key is
     * subscribed. A 2 KB static scratch also keeps a ~11 KB transient out of the render window,
     * where it would compete with the 78,200-byte framebuffer. */
    static char probe[2048];
    int status = 0;
    const esp_err_t e = net_http_get_json_status(url, NULL, probe, sizeof(probe), &status);

    if (status == 200) {
        s_onecall_available = 1;
        ESP_LOGI(TAG, "One Call 3.0 is available for this key");
    } else if (status == 401 || status == 403) {
        /* The documented answer for a key without the "One Call by Call" subscription. This is
         * the NORMAL path, not an error worth surfacing. */
        s_onecall_available = 0;
        ESP_LOGI(TAG, "One Call 3.0 not available (no subscription); using the free 2.5 products");
    } else {
        /* Anything else — a transport failure, a 5xx, a DNS problem — says nothing about the
         * subscription, so the question is left OPEN and the free pair is used meanwhile. Latching
         * a 0 here would permanently downgrade a subscribed key on one flaky request. */
        ESP_LOGW(TAG, "One Call probe inconclusive (status %d, %s); staying on the free products",
                 status, esp_err_to_name(e));
        return -1;
    }
    api_owm_note_call(0, 1);        /* a real call, credited to no particular day */
    return s_onecall_available;
}

/* Fetch the forecast into `buf`. Returns 0 on success.
 *
 * `days` is how many days the layout's widgets actually reference, taken from the highest
 * dayIndex they bind. The request is bounded with OWM's `cnt` (3-hour blocks, 8 per day) rather
 * than asking for the full 5-day document and discarding most of it: the full response is
 * 16,522 bytes measured, and carrying that much live while the TLS handshake needs ~20 KB
 * contiguous made the handshake fail with ALLOC_FAILED (see FORECAST_BUF_BYTES).
 *
 * A `days` of 0 means "ask for the whole horizon", which is what an alert-only page wants: the
 * official alerts ride in this document, so the page needs it fetched but not trimmed.
 *
 * `product` selects the ENDPOINT (FR-6). On One Call 3.0 there is no separate forecast product:
 * this ONE document carries current conditions, the daily array, and the official alerts, which
 * is why the tick treats it as the current document too when that product is active. */
static int fetch_forecast(const fetch_creds_t *c, char *buf, size_t buflen, int days,
                          owm_product_t product)
{
    if (c->key[0] == '\0') return -1;
    if (!api_owm_should_call()) {
        api_note_error("owm: daily call cap reached");
        return -1;
    }

    char url[512];
    if (product == OWM_PRODUCT_ONECALL3) {
        /* `minutely` and `hourly` are excluded because no widget binds them and together they
         * dominate the document (~11 KB of the ~16 KB). `current` and `daily` are KEPT: the
         * current conditions and the daily min/max are exactly what the page draws, and `alerts`
         * cannot be excluded without defeating FR-7. */
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/3.0/onecall"
                 "?lat=%.6f&lon=%.6f&exclude=minutely,hourly&units=imperial&appid=%s",
                 c->lat, c->lon, c->key);
    } else if (days > 0) {
        /* Clamped to the 5-day horizon the free product carries. The clamp and FORECAST_BUF_BYTES
         * are both derived from FORECAST_MAX_BLOCKS so the request can never outgrow the buffer
         * that receives it — the mismatch that made every Day 4/5 widget render "--". */
        int cnt = days * 8;
        if (cnt > FORECAST_MAX_BLOCKS) cnt = FORECAST_MAX_BLOCKS;
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/forecast"
                 "?lat=%.6f&lon=%.6f&units=imperial&cnt=%d&appid=%s",
                 c->lat, c->lon, cnt, c->key);
    } else {
        snprintf(url, sizeof(url),
                 "https://api.openweathermap.org/data/2.5/forecast"
                 "?lat=%.6f&lon=%.6f&units=imperial&appid=%s", c->lat, c->lon, c->key);
    }

    if (net_http_get_json(url, NULL, buf, buflen) != ESP_OK) {
        ESP_LOGW(TAG, "%s request failed",
                 product == OWM_PRODUCT_ONECALL3 ? "onecall" : "forecast");
        return -1;
    }
    /* Credit the call against today, taken from the response's own first timestamp. The two
     * products carry it in different places ("list"[0]."dt" for 2.5/forecast, the top-level "dt"
     * for One Call), and a response with neither is not a forecast, so it is not credited to a
     * day that might be wrong. */
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *dt = cJSON_GetObjectItemCaseSensitive(
                        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "list"), 0), "dt");
        if (!cJSON_IsNumber(dt)) dt = cJSON_GetObjectItemCaseSensitive(root, "dt");
        api_owm_note_call(cJSON_IsNumber(dt) ? (long)dt->valuedouble : 0, 1);
        cJSON_Delete(root);
    }
    return 0;
}

/* Fetch the page's Home Assistant entities in ONE template call (FR-5a).
 *
 * THE KEY OPTIMISATION: HA renders a template server-side, so the device sends one small POST
 * naming exactly the entities the layout needs and receives one small '|'-separated line
 * ("68.4|41.2"). N per-entity GETs would be N round trips, and /api/states returns every entity
 * on the instance — far too large for a 320 KB part with no PSRAM. */
static int fetch_ha(const fetch_creds_t *c, char (*ids)[48], int n_ids,
                    char *out, size_t outlen)
{
    if (n_ids <= 0) return 0;               /* nothing bound: no request to make */
    if (c->ha_url[0] == '\0' || c->ha_token[0] == '\0') {
        ESP_LOGW(TAG, "HA entities are bound but no HA url/token is stored");
        return -1;
    }

    /* Build the template body: {"template": "{{ states('a') }}|{{ states('b') }}"}.
     * ha_template_add_entity() validates each id against HA's grammar, which matters because an
     * unvalidated id would be interpolated into a Jinja template — a quote in an entity id would
     * be template injection, not merely a bad request. */
    char tmpl[600];
    int len = 0;
    for (int i = 0; i < n_ids; i++) {
        const int next = ha_template_add_entity(tmpl, (int)sizeof(tmpl), len, ids[i]);
        if (next < 0) {
            ESP_LOGW(TAG, "entity template overflow at %d entities", i);
            break;
        }
        len = next;
    }
    if (len == 0) return -1;

    char body[700];
    const int n = snprintf(body, sizeof(body), "{\"template\":\"%s\"}", tmpl);
    if (n < 0 || (size_t)n >= sizeof(body)) return -1;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/template", c->ha_url);

    if (net_http_post_json(url, c->ha_token, body, out, outlen) != ESP_OK) {
        ESP_LOGW(TAG, "HA template request failed");
        return -1;
    }
    return 0;
}

esp_err_t app_fbs_reserve(void)
{
    /* ONE buffer, and it holds the static layer. An earlier version reserved TWO 78,200-byte
     * framebuffers here, which starved the radio: measured, the device had 2.9 KiB left after the
     * two framebuffers, the linker's static DRAM and esp_wifi_init(), so the driver aborted with
     * ESP_ERR_NO_MEM and the device could not be provisioned. The band buffers the push needs are
     * acquired inside the render window and are two orders of magnitude smaller (see push_banded). */
    return ensure_static() == 0 ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_fbs_release(void)
{
    free(s_static); s_static = NULL;
    /* The picture this firmware could reproduce is gone with the layer, so the next push must be
     * a FULL refresh: a partial would diff the new frame against a frame it cannot rebuild and
     * leave ghosted fragments on the glass. Clearing BOTH the identity and the presence flag is
     * what makes that happen — refresh_decide() then sees nothing on the glass and chooses a full
     * push. */
    s_shown_slot = SLOT_NONE;
    s_glass_present = 0;
    s_have_prev_values = 0;
    ESP_LOGI(TAG, "static layer released (%u bytes back to the heap)", (unsigned)EPD_FB_BYTES);
}

/* Draw the "how to set this up" screen: the AP name, the BLE PoP, and QR codes for the setup
 * page and the two provisioning apps (FR-30).
 *
 * Uses the resident static layer, because this runs at the last moment before provisioning needs
 * the memory back — so it must not ask for a second 78,200-byte buffer it is about to hand over.
 * Overwriting the layer is safe and deliberate: the boot path releases it immediately afterwards,
 * and the identity is cleared so the next render reloads a real one rather than diffing a partial
 * against the setup screen. Composed via lib/provscreen, which is host-tested (the QR modules are
 * verified byte-for-byte against the reference matrices; see test/test_provscreen). */
void app_render_setup_screen(void)
{
    if (ensure_static() != 0) {
        ESP_LOGE(TAG, "cannot draw the setup screen: no static layer");
        return;
    }

    char ap_ssid[32];
    prov_service_name(ap_ssid, sizeof(ap_ssid));

    /* Pass the brand badge in HERE rather than drawing it first: the render fills the page
     * white before composing, so anything drawn beforehand would be erased. */
    if (provscreen_render_branded(s_static, ap_ssid, PROV_POP_STRING,
                                  boot_badge, boot_badge_w, boot_badge_h) != 0) {
        ESP_LOGE(TAG, "setup screen render failed");
        return;
    }

    if (epd_wake() != ESP_OK) {
        ESP_LOGE(TAG, "panel did not wake for the setup screen");
        return;
    }

    /* Pushed whole rather than banded: the renderer above already produced the full frame in the
     * layer buffer, so a band pass would only re-read it a band at a time for no memory saving. */
    const esp_err_t e = epd_write_frame(s_static);
    if (e == ESP_OK) {
        /* Identity cleared, NOT set: this is not the layout bitmap and it carries no layout
         * values, so marking it otherwise would authorise a later partial to diff a real layout
         * against the setup screen. Presence is cleared with it for the same reason — something IS
         * on the glass, but it is not something this firmware can reproduce, and a full refresh is
         * the only correct way to replace it. */
        s_shown_slot = SLOT_NONE;
        s_glass_present = 0;
        s_have_prev_values = 0;
        ESP_LOGI(TAG, "setup screen shown: SSID \"%s\"", ap_ssid);
    } else {
        ESP_LOGE(TAG, "setup screen push failed: %s", esp_err_to_name(e));
    }
    epd_sleep();
}

/* ---------------------------------------------------------------------------------------
 * WHY THE BAND BUFFERS ARE TRANSIENT RATHER THAN RESIDENT
 *
 * Measured on this part: total DRAM available for dynamic allocation is 234.2 KiB, and
 * esp_wifi_init() alone takes 26.2 KiB (5 static RX buffers, a 6.6 KiB driver task, the
 * management and dynamic pools). Add the linker's 52.4 KiB of static .bss/.data and two
 * 76.4 KiB framebuffers and the sum is 312 KiB against 234 available — the device cannot hold
 * TWO FRAMEBUFFERS and a live radio at the same time. With two reserved up front,
 * esp_wifi_init() failed with ESP_ERR_NO_MEM ("Expected to init 10 rx buffer, actual is 6")
 * and the HTTP server then failed with ESP_ERR_HTTPD_ALLOC_MEM, leaving the device with a
 * working panel and no way to configure it. Only ~2.9 KiB was left.
 *
 * Only ONE whole-panel buffer is needed for any length of time: the static layer, because it is
 * the expensive half (78,200 bytes) and both frames of a partial are derived from it. The band
 * buffers the push composes into are 31 KB for the pair together — small enough to be taken and
 * given back inside the render window, and the reason a partial is possible here at all.
 *
 * The panel is bistable and is put to sleep after every push, so releasing them costs nothing
 * visually — the image is on the glass, not in RAM.
 * ------------------------------------------------------------------------------------- */

void app_refresh_tick(power_source_t source, int force_full)
{
    /* The render window's heap profile, printed at the four points that bracket it. With
     * HEAP_TRACE unset these expand to nothing (see heap_trace.h) — they exist so a bench round
     * can see WHERE the contiguous block goes without re-instrumenting and re-flashing. */
    HEAP_DIAG("tick entry");

    /* The static layer is NOT acquired here, on either power source.
     *
     * It used to be held across the fetch, so the frame would survive as the diff base for a
     * partial refresh. That is no longer possible or useful: the fetch needs the region far more
     * than the layer does (see the release below), and on battery a partial was never reachable
     * anyway — one refresh per wake, then deep sleep, with the layer and its values living in RAM
     * that does not survive esp_deep_sleep_start().
     *
     * Acquiring it here would also be actively HARMFUL. When the re-acquire at the END of a tick
     * fails on a fragmented region, s_static is left NULL; an entry acquire would then fail too and
     * the tick would return BEFORE the fetch, so one failed render costs the NEXT tick's data as
     * well as its image, and the device stays stale for several ticks while the region clears
     * (measured: one failure cascaded into four). Not acquiring means a failed render costs only
     * that tick's image; the next tick fetches and draws normally.
     *
     * The layer is taken once, at the re-acquire below, after the radio is down and the fetch
     * buffers are freed — which is the point at which it is actually needed and most likely to fit. */
    if (s_static) { free(s_static); s_static = NULL; }
    HEAP_DIAG("tick entry (no static layer held)");

    /* ---- THE STATIC LAYER IS RELEASED ACROSS THE FETCH, ON BOTH POWER SOURCES ----
     *
     * WHY IT MUST BE: the TLS handshake needs CONTIGUOUS DRAM for the in-buffer, the out-buffer,
     * the X.509 verification working set and the worker's own stack, and it can only come from the
     * ONE region large enough for a 78,200-byte buffer (measured: region 0x3ffe4350, 113,840 bytes —
     * every other region caps at 64,936). Holding the layer there leaves 35,640 bytes for
     * everything else, which is not enough. Measured, holding it makes EVERY fetch fail:
     * `mbedtls_ssl_setup returned -0x7F00` (ALLOC_FAILED) and `PK verify failed` (X509 alloc),
     * with every reading falling back to "--".
     *
     * THIS APPLIES ON BATTERY TOO, and the earlier "on battery the layer is held and both fit"
     * reasoning was WRONG — it was true only while the TLS in-buffer was 8192. At the 16384 the
     * server's TLS records actually require (see sdkconfig.defaults), the arithmetic is
     * 78,200 (layer) + 16,384 (TLS in) + ~4 K (out) + ~19 K (forecast) > 113,840. Measured on the
     * bench in battery mode: the handshake failed for BOTH fetches and the panel drew placeholders.
     * The fetch buffers and the layer cannot coexist in that region at any tolerable buffer size.
     *
     * RELEASING COSTS NOTHING ON BATTERY EITHER, because a partial refresh there was never
     * possible in the first place: the device performs ONE refresh per wake and then deep-sleeps,
     * and s_static / s_prev_values / s_shown_slot live in ordinary RAM that does not survive
     * esp_deep_sleep_start(). So there is no diff base to preserve across a fetch — every battery
     * render is necessarily a full refresh, and the 78,200 bytes are better spent on the fetch.
     * FR-11's partial strategy belongs to the always-on path, where it is what keeps flicker and
     * refresh time down.
     *
     * The cost of releasing is the re-acquire below, which can fail while the region is
     * fragmented — see ensure_static()'s bounded wait, which is what makes that re-acquire reliable.
     * The panel is bistable and asleep between pushes, so releasing costs nothing visible. */

    /* NOTE: s_shown_slot and s_glass_present are deliberately NOT cleared here. They describe the
     * picture ON THE GLASS, which releasing the RAM does not change — the frame is still up there.
     * Clearing them would say "nothing on the glass" and force a FULL refresh on every tick, which
     * is how an earlier version of this function made FR-11's partial path dead code. They are
     * cleared only when the glass is genuinely overwritten by something unreproducible or the layer
     * is dropped — app_fbs_release() and the setup screen. */

    /* WiFi credentials live in NVS, written by provisioning (FR-30) — never compiled in.
     * net_wifi_connect() needs them explicitly, so they are read here rather than assumed. */
    char ssid[64] = {0};
    char pass[128] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(pass);
    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, DEVENV_KEY_WIFI_SSID, ssid, &slen);
        nvs_get_str(h, DEVENV_KEY_WIFI_PASS, pass, &plen);
        nvs_close(h);
    }

    if (ssid[0] == '\0') {
        /* Unprovisioned is a NORMAL first-boot state, not an error: the device has no
         * credentials yet and is waiting to be set up. The last good image stays up and the
         * device stays reachable over the API so it CAN be set up. */
        ESP_LOGI(TAG, "no WiFi credentials stored; waiting to be provisioned");
        return;
    }

    /* Connect with a bounded wait. A failure here is not fatal — the last good image stays
     * up and the device stays reachable (FR-29). */
    esp_err_t e = net_wifi_connect(ssid, pass, 20000);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "no network (%s); keeping the last good image", esp_err_to_name(e));
        api_note_error("net: connect failed");
        return;
    }

    /* Fill in a city-level location from the public IP, but ONLY if the user has not set one
     * (FR-30). This runs on the first refresh after a device joins a network it was just
     * provisioned for, so the config page the user opens next already has plausible
     * coordinates in its fields instead of two empty boxes they would have to fill from a
     * map. It is a DEFAULT, never an overwrite — see geo_ip.c for why that distinction is the
     * whole point. Cheap on every later boot: one NVS read, no request. */
    if (geo_ip_fill_if_unset()) {
        ESP_LOGI(TAG, "location was empty; filled it from the public IP (a city-level guess)");
    }

    /* 4 KB, and the size is load-bearing rather than arbitrary.
     *
     * This is static, so it lives in .bss — and on this part .bss ends exactly where the
     * second-largest DRAM heap region begins, so every byte here is a byte that region does
     * not have. The panel needs TWO contiguous 78,200-byte framebuffers, one of which must
     * come from that region, and it can only satisfy a request that fits a TLSF size class
     * (multiples of 4 KB at this size). At 8 KB this buffer pushed the region's largest
     * satisfiable block down to 77,824 — 376 bytes short of a framebuffer — and the device
     * booted unable to draw anything at all. See app_fbs_reserve().
     *
     * 4 KB is still ~8x the current-weather response: OWM 2.5/weather for a full document with
     * a long place name measures ~521 bytes. The FORECAST document does NOT fit here and is not
     * asked to — it is ~16.5 KB and gets a transient heap buffer instead (see
     * FORECAST_BUF_BYTES), because growing this array is exactly what starved the framebuffer. */
    static char resp[4096];

    /* ---- What does this page actually need? ----
     *
     * Resolved BEFORE fetching so that one forecast document serves every forecast widget, and
     * a page with no Home Assistant binding makes no HA request at all. */
    layout_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    /* ONE read of the stored document, used for BOTH the config and the page's widgets.
     *
     * Reading it twice (once for the config, once for the widgets) is not merely wasteful: each
     * read allocates the full CFG_JSON_MAX_LEN block, and on USB these reads happen in the window
     * between releasing the resident framebuffer for the fetch and re-acquiring it for the render.
     * A transient block that big lands inside the freshly-freed 78 KB hole and splits it, which is
     * exactly the fragmentation that can make the re-acquire fail. One read, one free. */
    char *cfg_json = NULL;
    int cfg_ok = (cfg_store_get(cfg_store_nvs(), &cfg_json) == 0 && cfg_json != NULL);
    if (cfg_ok && layout_config_parse(cfg_json, &cfg) != 0) cfg_ok = 0;
    if (!cfg_ok) {
        ESP_LOGW(TAG, "stored config unparseable; using defaults");
        cfg.update_seconds = 900;
        cfg.partial_refresh_limit = 5;
        cfg.page_count = 1;
    }

    /* WHICH PAGE: the rotation schedule, from real elapsed time since boot. FR-15 wants the
     * device to cycle pages on its own, and this is the only place that can happen. */
    const long elapsed_s = (long)(esp_timer_get_time() / 1000000LL);
    const int page_index = layout_page_at(&cfg, elapsed_s);

    static page_render_t page;      /* static: ~4 KB of widgets, too big for the 3.5 KB stack */
    memset(&page, 0, sizeof(page));
    if (cfg_ok) {
        page.n = layout_widgets_parse(cfg_json, page_index, page.widgets, LAYOUT_MAX_FIELDS);
    }
    free(cfg_json);
    if (page.n < 0) page.n = 0;
    HEAP_DIAG("after config read+free");

    value_needs_t needs;
    value_scan_needs(page.widgets, page.n, &needs);

    /* The page's HA entities, in template order. Collected HERE because the second HA widget on
     * a page reads the second token of the one response line. */
    char ha_ids[LAYOUT_MAX_FIELDS][48];
    int ha_want = 0;
    const int n_ha = value_collect_ha_entities(page.widgets, page.n, ha_ids,
                                               LAYOUT_MAX_FIELDS, &ha_want);

    fetch_creds_t creds;
    read_creds(&creds);

    /* FR-6: which product will ACTUALLY be fetched.
     *
     * Resolved ONCE, here, and used for BOTH the endpoint selection below and FR-7's alert
     * answer — so the alert bar can never describe a different product from the one the fetch
     * used, which is exactly how the bar came to announce "alerts unavailable" on a key that had
     * them.
     *
     * The probe runs only for AUTO. An explicit setting already says which product the key
     * carries, so probing would spend a call answering a question the config answered. */
    const owm_product_t product =
        (cfg.owm_product == OWM_PRODUCT_AUTO)
            ? owm_product_resolve(OWM_PRODUCT_AUTO, onecall_probe(&creds))
            : (owm_product_t)cfg.owm_product;
    const int onecall = (product == OWM_PRODUCT_ONECALL3);

    const long now_unix = (long)(esp_timer_get_time() / 1000000LL);

    /* ---- Fetch, one document per source ----
     *
     * Each fetch is independent: a forecast failure must not stop the current reading from
     * appearing, and vice versa. That is why the results are separate strings rather than one
     * blob. */
    char *forecast = NULL;
    static char ha_resp[512];
    ha_resp[0] = '\0';

    datasrc_value_t current;
    memset(&current, 0, sizeof(current));

    /* On One Call 3.0 there is NO separate current-conditions product: the one document carries
     * current, daily, and the official alerts together, so it is fetched whenever the page binds
     * any of the three. On the free tier the two 2.5 products are genuinely independent calls and
     * each is made only when the page binds something from it. */
    const int need_forecast_doc = needs.need_owm_daily || needs.need_owm_alert ||
                                  (onecall && needs.need_owm_current);
    int got_current = 0;

    if (onecall) {
        /* Nothing to fetch here: the current conditions arrive inside the One Call document and
         * are parsed out of it below. Asking 2.5/weather as well would be a second call for data
         * already held. */
        got_current = 0;
    } else {
        /* The parsed current reading is used for the fetch's success and for the daily-call
         * bookkeeping (fetch_current credits the call to the response's own day); the widgets
         * then re-read the DOCUMENT rather than this one value. */
        got_current = (fetch_current(&creds, resp, sizeof(resp), &current, product) == 0);
    }

    if (need_forecast_doc) {
        forecast = heap_caps_malloc(FORECAST_BUF_BYTES, MALLOC_CAP_8BIT);
        if (forecast) {
            /* `max_day_index` is the highest day any widget references, so the request carries
             * exactly the days the page will draw. An alert-only page binds no day and gets the
             * whole horizon, because the official alerts ride in this document. */
            const int days = needs.need_owm_daily ? needs.max_day_index + 1 : 0;
            if (fetch_forecast(&creds, forecast, FORECAST_BUF_BYTES, days, product) != 0) {
                free(forecast);
                forecast = NULL;
            } else if (onecall) {
                /* The One Call document IS the current-conditions document, so the reading the
                 * rest of this function treats as "did the current fetch work" comes out of it.
                 * Parsing rather than assuming success: a 200 whose body did not survive the
                 * buffer is not a reading, and treating it as one would cache garbage. */
                current = owm_parse_current_temp(forecast,
                                                 (long)(esp_timer_get_time() / 1000000LL));
                got_current = (current.status == DATASRC_OK);
            }
        } else {
            /* Not fatal — the widgets show their fallback — but worth logging, because the usual
             * reason is heap pressure and that is worth seeing. */
            ESP_LOGW(TAG, "no heap for the forecast buffer (%u bytes)", (unsigned)FORECAST_BUF_BYTES);
        }
    }

    if (needs.need_ha) {
        if (fetch_ha(&creds, ha_ids, n_ha, ha_resp, sizeof(ha_resp)) != 0) {
            ha_resp[0] = '\0';
        }
    }

    /* The radio is torn down BEFORE the ADC read and before any panel work (HW-3, NFR-3) —
     * but ONLY on battery. On USB the device stays awake to serve the API (FR-31), and the
     * API is useless without a network, so the connection is kept up deliberately. */
    if (source != POWER_SOURCE_USB) {
        net_wifi_disconnect();
    }

    if (!got_current) {
        /* Not fatal, and NOT a reason to skip the render: the panel still gets whatever
         * static layer is live (which is the whole point when the web app has just uploaded
         * one), with the last known reading if there is one. The error is recorded for
         * /api/status either way (FR-33). */
        api_note_error("owm: no current reading");
        ESP_LOGW(TAG, "fetch failed; widgets fall back to their placeholders");
    } else if (!onecall) {
        /* Remember the DOCUMENT, not a formatted string: the widgets bind to different fields
         * of it (temp, humidity, wind, conditions), so caching one number would only serve
         * whichever widget happened to be first in the page.
         *
         * Only on the free tier: `resp` holds 2.5/weather there, and on One Call the current
         * conditions live in the forecast document, which is a transient buffer freed below and
         * must not be cached by reference. */
        const size_t n = strlen(resp);
        if (n < sizeof(s_last_current)) {
            memcpy(s_last_current, resp, n + 1);
            s_have_last_current = 1;
        }
    }

    /* When this fetch failed, fall back to the document kept from the last one that worked, so
     * an outage leaves the readings on the glass rather than replacing every one with "--". A
     * device that has NEVER had a good fetch has nothing to fall back to and correctly shows the
     * placeholders. */
    const char *current_doc = NULL;
    if (got_current) {
        /* On One Call the current conditions come out of the forecast document; on the free tier
         * they come out of the dedicated 2.5/weather response. Either way the widgets re-read the
         * DOCUMENT, so the right one has to be handed over. */
        current_doc = onecall ? forecast : resp;
    } else if (s_have_last_current) {
        current_doc = s_last_current;
        ESP_LOGI(TAG, "using the last good reading document (%u bytes)",
                 (unsigned)strlen(s_last_current));
    }

    /* Build the field/value arrays from the page's widgets. A NULL document makes the widgets
     * that needed it resolve to their own fallback rather than to a wrong number. */
    const value_sources_t src = {
        .owm_current = current_doc,
        .owm_daily   = forecast,
        .ha_line     = (needs.need_ha && ha_resp[0]) ? ha_resp : NULL,
        /* FR-7, from the RESOLVED product — the same value the fetches above used, so the bar
         * cannot describe a different product from the one on the glass. */
        .owm_alerts_supported = needs.need_owm_alert ? owm_product_has_alerts(product) : 0,
    };
    build_fields(&page, &src, ha_ids, n_ha, now_unix);

    /* Hand the resolved strings to the API for GET /api/values (FR-27), which is what lets the
     * editor preview REAL fetched data instead of re-deriving it (and, in the embedded case,
     * without any credentials or internet of its own). Recorded even for a failed fetch: the
     * widget then resolved to its fallback, and "the panel will show --" is exactly what the
     * preview should say rather than keeping a stale value from a previous refresh. */
    api_record_values(page.ids, page.values, page.has_value, page.n_fields,
                      page_index, cfg_ok ? cfg.page_count : 1);

    /* The forecast buffer has served its purpose; releasing it here gives the render window the
     * ~16.5 KB back, which matters on USB where the second framebuffer is already tight. */
    free(forecast);
    forecast = NULL;
    HEAP_DIAG("after forecast free");

    /* The TLS worker's stack headroom, in the trace build only. net_http_stack_hwm() has existed
     * since the TLS work but nothing ever READ it, so NET_TLS_TASK_STACK's 16 KB was never
     * justified by the measurement its own comment demands — and that 16 KB is a contiguous DRAM
     * allocation competing with the 78,200-byte framebuffer in this very window. Printing it here
     * is what makes shrinking the stack an evidence-based change rather than a gamble. */
#if HEAP_TRACE
    ESP_LOGI("heaptrace", "%-28s %u bytes of stack never touched",
             "tls stack headroom", net_http_stack_hwm());
#endif

    /* Decide full vs partial through the TESTED policy, never an inline comparison.
     *
     * The limit comes from the config THIS TICK ALREADY PARSED, not from api_partial_limit().
     * That function re-reads the stored document through cfg_store_get(), which mallocs the full
     * 16,384-byte CFG_JSON_MAX_LEN block — and this line is INSIDE the render window, in the gap
     * between releasing the static layer and re-acquiring it. A transient 16 KB block landing in
     * the freshly-freed 78 KB hole is precisely the fragmentation that makes the re-acquire fail,
     * which is silent on the glass. Measured with HEAP_TRACE: the window opens with one clean
     * 110,592-byte block, and that is the whole margin the layer needs.
     *
     * `cfg` is already parsed above and layout_config_parse() seeds the same default (5) and the
     * same clamp, so this is the identical number with no allocation. */
    const int limit = cfg.partial_refresh_limit;
    /* A caller-supplied `force_full` is for the case where the caller KNOWS the frame on the
     * glass is no longer a valid diff base (a layout/bitmap change). The pending request flag
     * is read here as before, so the periodic mains refresh — which passes 0 — still gets the
     * ordinary partial/full decision instead of flickering a full refresh every interval. */
    const int forced_full = force_full || api_take_full_refresh();
    /* "Nothing on the glass" is tracked separately from the partial counter: the counter is
     * reset to 0 BY a full refresh, so treating 0 as "nothing drawn" would make every
     * refresh a full one and kill the partial path entirely.
     *
     * IT IS ITS OWN FLAG, NOT A TEST ON THE SLOT'S SIGN. The artwork identity is a NEGATIVE
     * sentinel (-(100 + page)), so `s_shown_slot < 0` was true for every device with an uploaded
     * layout — every tick read "nothing on the glass" and forced a full refresh, which is why
     * /api/status reported partials_since_full: 0 regardless of the partial budget. The two
     * questions ("is there anything to diff against" and "is it the same picture") are separate
     * and are now answered separately. */
    const int nothing_on_glass = !s_glass_present;
    /* The datasheet's 24 h rule, from real elapsed time — see hours_since_full(). Passing a
     * constant 0 here would silently disable it, which is the one policy that protects the
     * panel from permanent ghosting. */
    refresh_kind_t kind = refresh_decide(nothing_on_glass, api_partials_since_full(),
                                         limit, hours_since_full());
    if (forced_full) kind = REFRESH_FULL;

    /* ---- The render window: re-acquire the static layer ----
     *
     * The layer is taken HERE, after the fetch and after net_wifi_disconnect(), because that is
     * when the region it needs is most likely to be free — see the release note at the top of this
     * function. It is REQUIRED rather than a preference: without it nothing can be composed, so a
     * failure fails the tick and leaves the last good image on the glass (FR-29).
     *
     * This is the ONE whole-panel buffer the push needs. The previous and next frames are composed
     * a band at a time out of it (see push_banded), which is what makes FR-11's partial path
     * reachable on this part at all — the old design asked for a SECOND 78,200-byte framebuffer
     * here, which never fitted, so every refresh fell back to a full flash and the partial path
     * was dead code that reported success.
     *
     * The wait inside ensure_static() is what makes this reliable on USB — see its comment for why
     * the region is briefly unusable and how long it takes to clear. The slot identity is NOT
     * cleared, so a partial whose picture is still on the glass remains available. */
    HEAP_DIAG("before static layer acquire");
    if (ensure_static() != 0) {
        /* Flag it so the serve loop retries shortly instead of holding the stale image for a
         * full interval; the flag is the only signal, since the image on the glass is
         * deliberately untouched. */
        s_fb_lost = 1;
        api_note_error("render: out of memory for the static layer");
        return;
    }
    s_fb_lost = 0;
    HEAP_DIAG("after static layer acquire");

    /* Pull the static layer into the resident buffer. This is the ONLY whole-panel buffer held:
     * the previous and the next FRAME are each produced a band at a time from it by push_banded(),
     * which is what makes a partial possible on this part (see the block comment on s_static). */
    int slot = -1;
    if (load_static_layer(s_static, page_index, &slot) != 0) {
        ESP_LOGE(TAG, "no static layer for page %d", page_index);
        return;
    }

    /* The panel may be in deep sleep (the boot path sleeps it after showing the last good
     * image, and the USB serve loop then keeps running). Drawing to a sleeping controller
     * does not fail cleanly — it times out on BUSY, which is indistinguishable from a loose
     * FPC. Waking is a no-op when it is already awake. */
    if (epd_wake() != ESP_OK) {
        ESP_LOGE(TAG, "panel did not wake");
        api_note_error("render: panel did not wake");
        return;
    }

    /* HW-1: do not draw outside the panel's 0..50 C operating range. Checked HERE, after
     * epd_wake() and before the push, because the guard's read of the controller's temperature is
     * an SPI command that only answers on an awake panel — reading it earlier would make the
     * guard always come back UNKNOWN. Nothing is drawn to a panel outside its range: that is
     * exactly the "garbage on the glass" HW-1 forbids.
     *
     * This logs, and records THROUGH api_note_error, which is /api/status's evidence that a
     * refresh was suppressed and why (FR-33). */
    if (thermal_blocks_render()) {
        return;
    }

    /* A partial is only valid when the frame on the glass came from the SAME static layer and
     * the SAME values we still hold: the controller derives each pixel's transition from the
     * previous/next pair, so diffing against a frame this firmware cannot reproduce would leave
     * ghosted fragments of an unrelated picture. `s_shown_slot` is the picture's identity (see
     * load_static_layer) and `s_have_prev_values` says the previous values were recorded.
     *
     * `actual_partial` is recorded rather than re-derived at the log below. An earlier version
     * recomputed "partial" from `kind` alone, which is the DECISION and not the ACTION: when the
     * previous frame could not be reproduced the decision was still PARTIAL while a full refresh
     * ran, so the log said "partial refresh done" and every FR-11 check read that as a partial. */
    int actual_partial = (kind == REFRESH_PARTIAL && s_shown_slot == slot &&
                          s_have_prev_values);

    /* THE PANEL'S WAVEFORM IS THE DRIVER'S BUSINESS, and it enforces it inside every writer
     * (epd_ensure_waveform): a partial is drawn with the PARTIAL OTP waveform and a full with the
     * temperature-compensated LUT ladder, and the two are NOT interchangeable. Driving the
     * controller in one mode while sending the other mode's stream is silent on the wire — the
     * bytes are accepted and the panel shows something, just wrong (grey, half-transitioned pixels
     * where a value changed) — so the check belongs where the register state is known, not at this
     * call site. There were two reasons FR-11's partial path had never run at all: the second
     * framebuffer never allocated (now replaced by band composition), and epd_init_partial() was
     * never called. The driver now does both correctly on its own.
     *
     * A waveform failure therefore surfaces here as a failed push, which is handled below: the image
     * stays as it was and the error is recorded for /api/status (FR-33). */
    const frame_spec_t next = {
        .fields = page.fields, .values = page.value_ptrs, .n_fields = page.n_fields,
    };
    const frame_spec_t prev = {
        .fields = s_prev_values.fields, .values = s_prev_values.value_ptrs,
        .n_fields = s_prev_values.n_fields,
    };

    e = push_banded(&next, &prev, actual_partial);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel update failed: %s", esp_err_to_name(e));
        api_note_error("render: panel update failed");
        /* A NO-MEM failure here is a heap problem, not a panel problem, and it is transient in the
         * same way a failed layer acquire is: the region is fragmented by small network allocations
         * that free on their own. Measured on USB before the adaptive band size existed, the band
         * buffer missed by 792 bytes on EVERY tick for several minutes — the panel kept a stale
         * reading the whole time, with nothing retrying it until the next 15-minute interval. Flag
         * it so the serve loop comes back in seconds. */
        if (e == ESP_ERR_NO_MEM) s_fb_lost = 1;
        return;
    }

    /* The values just drawn are the ones a LATER partial must reproduce for its "previous" frame,
     * so they are recorded only after the push succeeded. `slot` too: it identifies the picture,
     * and a slot recorded against a frame that never made it to the glass would authorise a
     * partial against the wrong base.
     *
     * THE STRINGS ARE COPIED INTO THE SNAPSHOT'S OWN STORAGE AND THE POINTERS REBASED, which is not
     * incidental. `page.value_ptrs[i]` points into `page.values[i]` — the TICK-LOCAL page, which the
     * next tick overwrites. Copying the pointers would leave the snapshot pointing at the NEW
     * values, so the "previous" frame would be composed with the very values it is meant to differ
     * from, the two frames would be identical, and the partial would write nothing to the glass
     * while reporting success. Rebasing onto the snapshot's own storage is what makes it a
     * snapshot. */
    s_prev_values.n_fields = page.n_fields;
    for (int i = 0; i < page.n_fields; i++) {
        s_prev_values.fields[i] = page.fields[i];
        memcpy(s_prev_values.values[i], page.values[i], sizeof(s_prev_values.values[i]));
        s_prev_values.value_ptrs[i] = s_prev_values.values[i];
    }
    s_shown_slot = slot;
    s_glass_present = 1;
    s_have_prev_values = 1;

    if (actual_partial) {
        api_record_refresh(0);
    } else {
        api_record_refresh(1);
        /* Only a full refresh restarts the 24 h clock: a partial does not clear ghosting, so it
         * cannot postpone the need for one. */
        s_last_full_us = esp_timer_get_time();
    }
    api_record_page(page_index);
    ESP_LOGI(TAG, "%s refresh done: page %d, %d fields",
             actual_partial ? "partial" : "full",
             page_index, page.n_fields);

    /* Back to deep sleep (FR-12). The image is bistable and survives it, and leaving the
     * controller powered costs current for no benefit. On battery the device deep-sleeps
     * immediately after this anyway, so this only matters for the USB serve loop. */
    epd_sleep();
}
