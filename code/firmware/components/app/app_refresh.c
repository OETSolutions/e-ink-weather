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

/* TWO framebuffers. A partial refresh takes the frame CURRENTLY ON THE GLASS and the new
 * frame, and the controller derives each pixel's transition from the pair — so the previous
 * frame has to be kept. Keeping one buffer and treating a partial as "just draw the region"
 * is the mistake that produces a panel full of noise; verified on hardware.
 *
 * THEY ARE HEAP-ALLOCATED, NOT STATIC. Two of them is 156,400 bytes, and the linker's
 * static DRAM region (dram0_0_seg) is only 180,736 bytes — so as static arrays they
 * overflowed it by 23,016 bytes and the firmware would not link at all. On the heap they
 * come out of the 320 KB of RAM this part has, leaving the static region for everything
 * else. Verified: the same two buffers link cleanly when malloc'd.
 *
 * Allocated lazily on first use, because a device that is only ever going to serve the API
 * has no reason to hold 156 KB. */
static uint8_t *s_fb_prev;
static uint8_t *s_fb_next;

/* Declared here because the render entry points below use them, while their definitions sit
 * after the long explanatory block further down. */
static int  ensure_prev(void);
static int  acquire_next(void);
static void release_next(void);

/* Allocate the RESIDENT framebuffer — the frame on the glass — on first use. Returns 0 on
 * success, -1 if the allocation failed. Only this one is held between refreshes; the second,
 * transient buffer is managed by acquire_next()/release_next() and the block comment further
 * down explains why the split exists. */
static int ensure_prev(void)
{
    if (s_fb_prev) return 0;

    s_fb_prev = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);
    if (s_fb_prev) return 0;

    /* THE FRAMEBUFFER HAS TO WAIT FOR THE NETWORK STACK TO LET GO, AND THAT IS THE WHOLE BUG.
     *
     * Measured on the bench (HEAP_TRACE probe): this device has exactly ONE DRAM region large
     * enough to hold a 78,200-byte framebuffer — region 0x3ffe4350, ~113 KB. On USB the radio
     * stays up for the API (FR-31), and right after a fetch that region's free space is split by
     * a few tiny allocations (tens of bytes — the block walk shows 24/84/112 B pieces) into
     * pieces of 68,428 + 25,308 + 4,104, none of which fits a framebuffer. Within about 800 ms
     * those pieces COALESCE back into one 98,304-byte block — while `free_heap` does not move by a
     * single byte (129,492 before and after). So this is not a leak and not a shortage: it is a
     * transient allocation that sits astride the region and is released by its owner on a timer.
     *
     * That is why the failure looked random (~half of plugged-in ticks) and why it never happens
     * on battery: on battery net_wifi_disconnect() tears the whole stack down BEFORE the render
     * window, so the region is already clean. On USB there are no credentials to tear down, only
     * the debris of a completed fetch, and it clears on its own.
     *
     * SO THE FIX IS TO WAIT FOR IT, WITH A BOUND. A retry loop that polls until the region is
     * actually usable converts a coin-flip into a guaranteed draw, and the cost is paid ONLY on
     * the losing ticks — a healthy tick returns above, on the first malloc, having waited
     * nothing. The 3 s budget is ~4x the 800 ms the probe measured, so a slower clear still
     * succeeds; if it genuinely never clears the failure is reported exactly as before.
     *
     * The wait is affordable because this condition is a USB one: on battery the radio is torn
     * down before the render window, so the region is already clean and this first malloc does
     * not fail. A battery device reaches the loop only under genuine memory pressure, where it
     * costs one bounded 3 s awake period before reporting the same failure — rare, and not
     * worth gating on the power source to avoid. */
    for (int attempt = 0; attempt < 60; attempt++) {
        vTaskDelay(pdMS_TO_TICKS(50));      /* 60 x 50 ms = 3 s ceiling */
#if HEAP_TRACE
        if (attempt % 5 == 0) {
            ESP_LOGW("heaptrace", "  framebuffer wait %2d: largest=%u free=%u", attempt,
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                     (unsigned)esp_get_free_heap_size());
        }
#endif
        s_fb_prev = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);
        if (s_fb_prev) return 0;
    }

    {
        /* The largest free block is reported, not just the total. Each framebuffer is one
         * contiguous 76 KiB allocation, so total free heap is the wrong number to look at: a
         * device with 200 KiB free but no 76 KiB hole cannot draw, and a message quoting only
         * the total sends the reader looking for a leak that is not there. */
        ESP_LOGE(TAG, "cannot allocate the framebuffer (%u bytes) "
                      "(free heap %u, largest block %u)",
                 (unsigned)EPD_FB_BYTES,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        /* The region breakdown, only here: this is the one point where "which block is in the
         * way" is the question, and the dump is too large to print per refresh. */
        HEAP_DUMP("resident framebuffer allocation failed");
        HEAP_BLOCKS("resident framebuffer allocation failed");
        return -1;
    }
}

/* Which slot's bytes are currently in s_fb_prev. -1 = nothing drawn yet this power cycle. */
static int s_shown_slot = -1;

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
    *from_slot = -1;
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

/* The fields to stamp this refresh, and their values. Sized by the parse caps rather than
 * allocated: this lives on the calling task's stack, and the parse bounds it. */
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

esp_err_t app_render_last_good(void)
{
    if (ensure_prev() != 0) return ESP_ERR_NO_MEM;
    /* The last-good push happens before the network, so the second buffer is normally
     * available; if it is not, compose into the resident one and push a full frame. */
    const int have_next = (acquire_next() == 0);
    uint8_t *const work = have_next ? s_fb_next : s_fb_prev;

    /* Page 0: the boot path shows the last-good image before the network, and it cannot know
     * which page was on the glass when the device slept. Page 0 is the defined default (a
     * single-page layout renders identically at any index). */
    int slot = -1;
    if (load_static_layer(work, 0, &slot) != 0) {
        if (have_next) release_next();      /* see the leak note in app_refresh_tick() */
        return ESP_ERR_INVALID_STATE;
    }

    /* No live values yet (this runs before the network), so compose with the static layer
     * alone. The frame is what the previous power cycle left, which is the point: FR-29
     * wants the last good image visible immediately, not a blank panel during the fetch. */
    canvas_t c;
    canvas_init(&c, work);
    if (render_compose_stream(&c, flash_reader, work, NULL, NULL, 0) != 0) {
        if (have_next) release_next();
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t e = epd_write_frame(work);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel write failed: %s", esp_err_to_name(e));
        api_note_error("render: panel write failed");
        if (have_next) release_next();
        return e;
    }

    /* This frame is now on the glass, so it becomes the "previous" frame a later partial
     * refresh will be diffed against. The second buffer is handed back: it is 76.4 KiB that
     * the coming TLS handshake needs, and keeping it is what produced the mbedTLS
     * "sha_get_engine_state" abort. The image is on the glass and the panel is bistable, so
     * nothing is lost by not holding it. */
    memcpy(s_fb_prev, work, EPD_FB_BYTES);
    s_shown_slot = slot;
    if (have_next) release_next();
    api_record_refresh(1);       /* a full update by definition */
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

/* The forecast document buffer, and its size is a MEMORY constraint rather than a taste choice.
 *
 * MEASURED FAILURE THIS SIZING FIXES: a full 5-day/3-hour response is 16,522 bytes, and holding a
 * 20 KB buffer for it while the TLS handshake ALSO needs ~20 KB contiguous on this part (a 16 KB
 * in-buffer plus a 4 KB out-buffer, from MBEDTLS_SSL_IN/OUT_CONTENT_LEN) made the handshake fail
 * with `mbedtls_ssl_setup returned -0x7F00` (ALLOC_FAILED) — observed as "forecast request
 * failed" and `--` in every forecast box, with a largest-free-block of only 30,720 bytes at that
 * moment. The device was competing with itself.
 *
 * So the request is bounded to the days the layout actually asks for. OWM takes `cnt` as a count
 * of 3-hour blocks, 8 per day, and the layout's highest `dayIndex` says how many days are needed.
 * Two days measures ~6.8 KB — well inside this buffer — and a layout that wants more gets more,
 * up to the 5-day horizon OWM offers anyway. */
#define FORECAST_BUF_BYTES 12288

/* Fetch the current conditions from OWM. Returns 0 on success, -1 otherwise. */
static int fetch_current(const fetch_creds_t *c, char *resp, size_t resplen,
                         datasrc_value_t *out)
{
    if (c->key[0] == '\0') {
        ESP_LOGW(TAG, "no OWM key stored; skipping fetch");
        return -1;
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

/* Fetch the forecast into `buf`. Returns 0 on success.
 *
 * `days` is how many days the layout's widgets actually reference, taken from the highest
 * dayIndex they bind. The request is bounded with OWM's `cnt` (3-hour blocks, 8 per day) rather
 * than asking for the full 5-day document and discarding most of it: the full response is
 * 16,522 bytes measured, and carrying that much live while the TLS handshake needs ~20 KB
 * contiguous made the handshake fail with ALLOC_FAILED (see FORECAST_BUF_BYTES).
 *
 * A `days` of 0 means "ask for the whole horizon", which is what an alert-only page wants: the
 * official alerts ride in this document, so the page needs it fetched but not trimmed. */
static int fetch_forecast(const fetch_creds_t *c, char *buf, size_t buflen, int days)
{
    if (c->key[0] == '\0') return -1;
    if (!api_owm_should_call()) {
        api_note_error("owm: daily call cap reached");
        return -1;
    }

    char url[512];
    if (days > 0) {
        /* Clamped to 5 days: that is the whole horizon the free 5-day/3-hour product carries,
         * so asking for more would return the same data with a misleading count. */
        int cnt = days * 8;
        if (cnt > 40) cnt = 40;
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
        ESP_LOGW(TAG, "forecast request failed");
        return -1;
    }
    /* Credit the call against today, taken from the response's own first block. A response with
     * no "list" is not a forecast and is not credited to a wrong day. */
    cJSON *root = cJSON_Parse(buf);
    if (root) {
        cJSON *dt = cJSON_GetObjectItemCaseSensitive(
                        cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(root, "list"), 0), "dt");
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
    /* ONE buffer, and that one is the frame on the glass. Reserving two here is what starved
     * the radio: measured, the device had 2.9 KiB left after two framebuffers, the linker's
     * static DRAM and esp_wifi_init(), so the driver aborted with ESP_ERR_NO_MEM and the
     * device could not be provisioned. The second is acquired inside the render window, where
     * it is actually needed and where the radio has been torn down on battery — see the block
     * comment in app_refresh_tick(). */
    return ensure_prev() == 0 ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_fbs_release(void)
{
    free(s_fb_prev); s_fb_prev = NULL;
    free(s_fb_next); s_fb_next = NULL;
    /* The previous frame is gone, so the next push must be a FULL refresh: a partial refresh
     * would diff the new frame against uninitialised memory and scribble noise on the glass.
     * Clearing this is what makes that happen — refresh_decide() sees no previous frame and
     * chooses a full push. */
    s_shown_slot = -1;
    ESP_LOGI(TAG, "framebuffers released (%u bytes back to the heap)",
             (unsigned)(2 * EPD_FB_BYTES));
}

/* Draw the "how to set this up" screen: the AP name, the BLE PoP, and QR codes for the setup
 * page and the two provisioning apps (FR-30).
 *
 * Uses ONE framebuffer, and reuses the resident one when it is already held, because this runs
 * at the last moment before provisioning needs the memory back — so it must not ask for 152 KiB
 * it is about to hand over. Composed via lib/provscreen, which is host-tested (the QR modules
 * are verified byte-for-byte against the reference matrices; see test/test_provscreen). */
void app_render_setup_screen(void)
{
    if (ensure_prev() != 0) {
        ESP_LOGE(TAG, "cannot draw the setup screen: no framebuffer");
        return;
    }
    const int have_next = (acquire_next() == 0);
    uint8_t *const work = have_next ? s_fb_next : s_fb_prev;

    char ap_ssid[32];
    prov_service_name(ap_ssid, sizeof(ap_ssid));

    /* Pass the brand badge in HERE rather than drawing it first: the render fills the page
     * white before composing, so anything drawn beforehand would be erased. */
    if (provscreen_render_branded(work, ap_ssid, PROV_POP_STRING,
                                  boot_badge, boot_badge_w, boot_badge_h) != 0) {
        ESP_LOGE(TAG, "setup screen render failed");
        if (have_next) release_next();
        return;
    }

    if (epd_wake() != ESP_OK) {
        ESP_LOGE(TAG, "panel did not wake for the setup screen");
        if (have_next) release_next();
        return;
    }

    const esp_err_t e = epd_write_frame(work);
    /* The resident buffer follows this like any other full frame, so the NEXT partial refresh
     * (after provisioning, on a later boot) diffs against the right thing. The slot is -1:
     * this is not the layout bitmap, and marking it otherwise would make a later partial diff
     * the setup screen against a layout. */
    if (e == ESP_OK) {
        memcpy(s_fb_prev, work, EPD_FB_BYTES);
        s_shown_slot = -1;
        ESP_LOGI(TAG, "setup screen shown: SSID \"%s\"", ap_ssid);
    } else {
        ESP_LOGE(TAG, "setup screen push failed: %s", esp_err_to_name(e));
    }
    epd_sleep();
    if (have_next) release_next();
}

/* ---------------------------------------------------------------------------------------
 * WHY THE SECOND FRAMEBUFFER IS TRANSIENT RATHER THAN RESIDENT
 *
 * Measured on this part: total DRAM available for dynamic allocation is 234.2 KiB, and
 * esp_wifi_init() alone takes 26.2 KiB (5 static RX buffers, a 6.6 KiB driver task, the
 * management and dynamic pools). Add the linker's 52.4 KiB of static .bss/.data and the two
 * 76.4 KiB framebuffers and the sum is 312 KiB against 234 available — the device cannot hold
 * two framebuffers and a live radio at the same time. With both reserved up front,
 * esp_wifi_init() failed with ESP_ERR_NO_MEM ("Expected to init 10 rx buffer, actual is 6")
 * and the HTTP server then failed with ESP_ERR_HTTPD_ALLOC_MEM, leaving the device with a
 * working panel and no way to configure it. Only ~2.9 KiB was left.
 *
 * Only ONE of the two is actually needed for any length of time:
 *
 *   - s_fb_prev must be resident, because it is the frame currently on the glass and a
 *     partial refresh is diffed against it (epd_write_frame_partial reads it row by row).
 *     It is what survives between refreshes.
 *   - s_fb_next is needed only inside app_refresh_tick() and app_render_last_good() — loaded,
 *     composed into, pushed, and then copied into s_fb_prev. Outside that window it is dead
 *     weight, and holding it is what starves the radio.
 *
 * So s_fb_next is acquired on entry to the render window and released on exit. On battery the
 * radio has already been torn down by net_wifi_disconnect() before the window opens, so the
 * transient allocation always fits; in USB mode the API keeps the radio up, and the ~90 KiB
 * that frees is what lets the HTTP server start at all.
 *
 * The panel is bistable and is put to sleep after every push, so releasing the transient
 * buffer costs nothing visually — the image is on the glass, not in RAM.
 * ------------------------------------------------------------------------------------- */

/* Acquire the transient framebuffer. Returns 0 on success. Logs the same diagnostic as
 * ensure_fbs() on failure, because "largest free block" is the number that matters for a
 * single contiguous allocation. */
static int acquire_next(void)
{
    if (s_fb_next) return 0;
    s_fb_next = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);
    if (!s_fb_next) {
        ESP_LOGE(TAG, "cannot allocate the transient framebuffer (%u bytes) "
                      "(free heap %u, largest block %u)",
                 (unsigned)EPD_FB_BYTES,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return -1;
    }
    return 0;
}

static void release_next(void)
{
    free(s_fb_next);
    s_fb_next = NULL;
}

void app_refresh_tick(power_source_t source)
{
    /* The render window's heap profile, printed at the four points that bracket it. With
     * HEAP_TRACE unset these expand to nothing (see heap_trace.h) — they exist so a bench round
     * can see WHERE the contiguous block goes without re-instrumenting and re-flashing. */
    HEAP_DIAG("tick entry");

    /* Only the RESIDENT framebuffer is acquired here — s_fb_prev, the frame on the glass.
     * The transient one is taken later, once the radio has been torn down on battery. See the
     * block comment above for why holding both this early starves esp_wifi_init(). */
    if (ensure_prev() != 0) {
        api_note_error("render: out of memory for framebuffers");
        return;
    }
    HEAP_DIAG("after ensure_prev");

    /* ---- THE RESIDENT FRAME IS RELEASED ACROSS THE FETCH ----
     *
     * s_fb_prev is held only to be the diff base for a partial refresh. A partial needs BOTH
     * frames, and two 76.4 KiB frames plus the radio do not fit (see the DRAM budget note), so the
     * frame is released here and the render falls back to a full refresh — which needs only one.
     *
     * This is LOAD-BEARING FOR TLS, and measurably so: the TLS handshake needs ~20 KB CONTIGUOUS
     * (an 8 KB in-buffer, a 4 KB out-buffer, the X.509 certificate verification, and the task's
     * own stack), and with the frame resident the largest free block in the big DRAM region is
     * only ~30 KB and it is fragmented further by the config read — measured, the certificate
     * verification then fails with -0x2880 (X509_ALLOC_FAILED) and EVERY reading falls back to
     * "--". Verified on the bench both ways: holding the frame makes the fetch fail on every
     * tick, not occasionally.
     *
     * The panel is bistable and asleep between pushes, so releasing costs nothing visible: the
     * image stays on the glass and is re-acquired below. */
    if (source == POWER_SOURCE_USB) {
        free(s_fb_prev); s_fb_prev = NULL;
        /* No resident frame means no diff base, so the next push MUST be a full refresh. Resetting
         * this is what guarantees it: s_shown_slot is what refresh_decide() consults for
         * "nothing on the glass", and a partial against the re-acquired (uninitialised) buffer
         * would diff the new layout against garbage and scribble noise onto the panel. */
        s_shown_slot = -1;
    }
    HEAP_DIAG("after USB release of prev");

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

    const long now_unix = (long)(esp_timer_get_time() / 1000000LL);

    /* ---- Fetch, one document per source ----
     *
     * Each fetch is independent: a forecast failure must not stop the current reading from
     * appearing, and vice versa. That is why the results are separate strings rather than one
     * blob. */
    char *forecast = NULL;
    static char ha_resp[512];
    ha_resp[0] = '\0';

    /* The parsed current reading is used for the fetch's success and for the daily-call
     * bookkeeping (fetch_current credits the call to the response's own day); the widgets then
     * re-read the DOCUMENT rather than this one value. */
    datasrc_value_t current;
    memset(&current, 0, sizeof(current));
    const int got_current = (fetch_current(&creds, resp, sizeof(resp), &current) == 0);

    if (needs.need_owm_daily || needs.need_owm_alert) {
        forecast = heap_caps_malloc(FORECAST_BUF_BYTES, MALLOC_CAP_8BIT);
        if (forecast) {
            /* `max_day_index` is the highest day any widget references, so the request carries
             * exactly the days the page will draw. An alert-only page binds no day and gets the
             * whole horizon, because the official alerts ride in this document. */
            const int days = needs.need_owm_daily ? needs.max_day_index + 1 : 0;
            if (fetch_forecast(&creds, forecast, FORECAST_BUF_BYTES, days) != 0) {
                free(forecast);
                forecast = NULL;
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
    } else {
        /* Remember the DOCUMENT, not a formatted string: the widgets bind to different fields
         * of it (temp, humidity, wind, conditions), so caching one number would only serve
         * whichever widget happened to be first in the page. */
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
        current_doc = resp;
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
     * between releasing the resident framebuffer and re-acquiring it. A transient 16 KB block
     * landing in the freshly-freed 76 KB hole is precisely the fragmentation that makes the
     * re-acquire fail, which is silent on the glass. Measured with HEAP_TRACE: the window opens
     * with one clean 110,592-byte block, and that is the whole margin the framebuffer needs.
     *
     * `cfg` is already parsed above and layout_config_parse() seeds the same default (5) and the
     * same clamp, so this is the identical number with no allocation. */
    const int limit = cfg.partial_refresh_limit;
    const int forced_full = api_take_full_refresh();
    /* "Nothing on the glass" is tracked separately from the partial counter: the counter is
     * reset to 0 BY a full refresh, so treating 0 as "nothing drawn" would make every
     * refresh a full one and kill the partial path entirely. */
    const int nothing_on_glass = (s_shown_slot < 0);
    /* The datasheet's 24 h rule, from real elapsed time — see hours_since_full(). Passing a
     * constant 0 here would silently disable it, which is the one policy that protects the
     * panel from permanent ghosting. */
    refresh_kind_t kind = refresh_decide(nothing_on_glass, api_partials_since_full(),
                                         limit, hours_since_full());
    if (forced_full) kind = REFRESH_FULL;

    /* ---- The render window: try for the second framebuffer ----
     *
     * The transient buffer is requested HERE, after the fetch and after net_wifi_disconnect().
     * That ordering matters, and so does the fact that this is a PREFERENCE rather than a
     * requirement.
     *
     * On battery the radio was just torn down, which frees roughly 27 KiB, and the buffer
     * fits — so the partial-refresh path (FR-11) works as designed.
     *
     * On USB the radio stays up for the API (FR-31), and then it does NOT fit: measured, the
     * heap has ~82 KiB free when this runs but split into ten blocks with the largest only
     * 22.5 KiB, because the radio's allocations sit between the free pieces. There is no
     * ordering that fixes this — two 76.4 KiB framebuffers plus the radio plus the TLS buffers
     * is about 300 KiB against the 234.2 KiB this part has, so a partial refresh and a live
     * radio are mutually exclusive.
     *
     * Rather than fail, the render falls back to a FULL refresh, which needs only ONE buffer:
     * compose straight into s_fb_prev (the frame on the glass is not needed to draw a full
     * frame) and push that. The panel gets the new image either way; the cost is ghosting on
     * a device that is plugged in and being configured, which is exactly when a full refresh
     * is least objectionable. On battery — the deployed case, where FR-11's partial strategy
     * actually matters for power and flicker — the partial path is unaffected. */

    /* Re-acquire the resident frame that USB released across the fetch (above). The wait inside
     * ensure_prev() is what makes this reliable on USB — see its comment for why the region is
     * briefly unusable and how long it takes to clear. s_shown_slot is already -1, so the policy
     * below forces a full refresh — the only kind possible without a diff base. */
    HEAP_DIAG("before re-acquire of prev");
    if (ensure_prev() != 0) {
        api_note_error("render: out of memory for the framebuffer");
        return;
    }
    HEAP_DIAG("after re-acquire of prev");

    const int have_next = (acquire_next() == 0);
    HEAP_DIAG(have_next ? "after transient acquired" : "transient unavailable");
    if (!have_next) {
        ESP_LOGW(TAG, "no second framebuffer (free %u, largest %u); falling back to a full refresh",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    uint8_t *const work = have_next ? s_fb_next : s_fb_prev;

    /* Build the new frame from the static layer plus the page's resolved values.
     *
     * The static layer and the fields must come from the SAME layout generation, and the slot
     * number is what ties them: a partial refresh diffs the new frame against the previous one,
     * so if the web app pushed a NEW layout bitmap while the field list came from an older
     * config, the diff would compare two unrelated pictures and leave ghosted fragments of the
     * old layout on the glass. `slot` changes whenever the bitmap changes, and the partial path
     * below requires s_shown_slot == slot for exactly this reason. */
    int slot = -1;
    if (load_static_layer(work, page_index, &slot) != 0) {
        /* EVERY exit after acquire_next() MUST give the transient buffer back. It is 76.4 KiB
         * of a 234 KiB part, so leaking even one costs a whole framebuffer's worth of the
         * largest contiguous block — enough that the NEXT refresh cannot allocate its resident
         * frame and silently keeps the old image on the glass. That is the "reports success,
         * panel does nothing" failure this path must never produce. */
        if (have_next) release_next();
        return;
    }

    canvas_t c;
    canvas_init(&c, work);
    if (render_compose_stream(&c, flash_reader, work,
                              page.fields, page.value_ptrs, page.n_fields) != 0) {
        ESP_LOGE(TAG, "compose failed");
        if (have_next) release_next();
        return;
    }

    /* The panel may be in deep sleep (the boot path sleeps it after showing the last good
     * image, and the USB serve loop then keeps running). Drawing to a sleeping controller
     * does not fail cleanly — it times out on BUSY, which is indistinguishable from a loose
     * FPC. Waking is a no-op when it is already awake. */
    if (epd_wake() != ESP_OK) {
        ESP_LOGE(TAG, "panel did not wake");
        api_note_error("render: panel did not wake");
        if (have_next) release_next();
        return;
    }

    /* A partial needs BOTH frames, so it is only possible when the second buffer was
     * obtained. Otherwise this is a full refresh, which is also what the policy asks for when
     * the static layer changed — a partial against a previous frame from a DIFFERENT image
     * would diff two unrelated pictures and leave ghosted fragments of the old layout. */
    if (have_next && kind == REFRESH_PARTIAL && s_shown_slot == slot) {
        e = epd_write_frame_partial(s_fb_prev, s_fb_next);
        if (e == ESP_OK) api_record_refresh(0);
    } else {
        e = epd_write_frame(work);
        if (e == ESP_OK) {
            api_record_refresh(1);
            /* Only a full refresh restarts the 24 h clock: a partial does not clear
             * ghosting, so it cannot postpone the need for one. */
            s_last_full_us = esp_timer_get_time();
        }
    }

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel update failed: %s", esp_err_to_name(e));
        api_note_error("render: panel update failed");
        if (have_next) release_next();
        return;
    }

    /* With a second buffer the new frame is copied down into the resident one. In the
     * fallback the frame was composed into the resident one directly, so it is already there. */
    if (have_next) memcpy(s_fb_prev, s_fb_next, EPD_FB_BYTES);
    s_shown_slot = slot;
    /* Give the second buffer back. Holding it would starve the NEXT fetch: it is 76.4 KiB
     * that the TLS handshake needs, and the handshake happens before the next render window.
     * Releasing it here and re-acquiring there is what keeps both steps alive. */
    if (have_next) release_next();
    HEAP_DIAG("after transient released");
    api_record_page(page_index);
    ESP_LOGI(TAG, "%s refresh done: page %d, %d fields",
             e == ESP_OK && have_next ? (kind == REFRESH_PARTIAL ? "partial" : "full") : "full",
             page_index, page.n_fields);

    /* Back to deep sleep (FR-12). The image is bistable and survives it, and leaving the
     * controller powered costs current for no benefit. On battery the device deep-sleeps
     * immediately after this anyway, so this only matters for the USB serve loop. */
    epd_sleep();
}
