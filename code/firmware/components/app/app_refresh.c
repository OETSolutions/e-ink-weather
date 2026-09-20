#include "app_refresh.h"
#include "api.h"
#include "api_store.h"
#include "canvas.h"
#include "cfg_store.h"
#include "datasrc.h"
#include "epd.h"
#include "fonts.h"
#include "layout_model.h"
#include "net_http.h"
#include "net_wifi.h"
#include "nvs_keys.h"
#include "owm.h"
#include "provscreen.h"
#include "prov.h"
#include "refresh_policy.h"
#include "render.h"
#include "esp_heap_caps.h"
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
    if (!s_fb_prev) {
        /* The largest free block is reported, not just the total. Each framebuffer is one
         * contiguous 76 KiB allocation, so total free heap is the wrong number to look at: a
         * device with 200 KiB free but no 76 KiB hole cannot draw, and a message quoting only
         * the total sends the reader looking for a leak that is not there. */
        ESP_LOGE(TAG, "cannot allocate the framebuffer (%u bytes) "
                      "(free heap %u, largest block %u)",
                 (unsigned)EPD_FB_BYTES,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
        return -1;
    }
    return 0;
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
 * costs nothing on the glass, which is the same principle as FR-29's last-good image. */
static datasrc_value_t s_last_temp;
static int             s_have_last_temp;

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
 * Preference order: the uploaded bitmap slot, then the built-in vendor image. A device that
 * has never had a bitmap pushed still renders something meaningful rather than a blank
 * panel (FR-29). */
static int load_static_layer(uint8_t *dst, int *from_slot)
{
    if (bitmap_store_load(dst) == 0) {
        *from_slot = api_live_bitmap_slot();
        return 0;
    }
    memcpy(dst, boot_logo, EPD_FB_BYTES);
    *from_slot = -1;
    return 0;
}

/* The values to stamp, and the boxes they go in. The boxes are the DEFAULT layout's, used
 * only while the web app has not yet pushed a layout; the real fields arrive with the
 * config in Task 17. Keeping a fixed set here means a fresh device shows real readings
 * instead of an empty frame. */
static const value_field_t DEFAULT_FIELDS[2] = {
    { 48,  76, 420, 110, 'L', 'T', FONT_VALUE },
    { 48, 256, 420, 110, 'L', 'T', FONT_VALUE },
};

static void fmt_temp(char *buf, size_t cap, const datasrc_value_t *v)
{
    if (v->status == DATASRC_OK && v->is_numeric) {
        snprintf(buf, cap, "%.1f", v->value);
    } else {
        /* A dash, not "0.0" and not blank. Zero would be a plausible temperature and a
         * blank would look like a rendering fault; the dash says "no reading" without
         * pretending to be one. */
        snprintf(buf, cap, "--");
    }
}

esp_err_t app_render_last_good(void)
{
    if (ensure_prev() != 0) return ESP_ERR_NO_MEM;
    /* The last-good push happens before the network, so the second buffer is normally
     * available; if it is not, compose into the resident one and push a full frame. */
    const int have_next = (acquire_next() == 0);
    uint8_t *const work = have_next ? s_fb_next : s_fb_prev;

    int slot = -1;
    if (load_static_layer(work, &slot) != 0) return ESP_ERR_INVALID_STATE;

    /* No live values yet (this runs before the network), so compose with the static layer
     * alone. The frame is what the previous power cycle left, which is the point: FR-29
     * wants the last good image visible immediately, not a blank panel during the fetch. */
    canvas_t c;
    canvas_init(&c, work);
    if (render_compose_stream(&c, flash_reader, work, NULL, NULL, 0) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t e = epd_write_frame(work);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel write failed: %s", esp_err_to_name(e));
        api_note_error("render: panel write failed");
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

/* Fetch the current temperature from OWM into `out`. Returns 0 on success.
 *
 * One endpoint for now: the free 2.5/weather product, because a key without the One Call
 * subscription — the default state of a new account — cannot reach 3.0 at all. owm.h
 * handles both shapes, so upgrading later is a URL change. */
static int fetch_current(char *resp, size_t resplen, datasrc_value_t *out)
{
    char *json = NULL;
    if (cfg_store_get(cfg_store_nvs(), &json) != 0) return -1;

    /* The OWM URL is built from the stored location and key. Both live in NVS, written by
     * the web UI (FR-30); neither is ever compiled in. */
    nvs_handle_t h;
    char url[512];
    char key[64] = {0};
    double lat = 0, lon = 0;
    size_t klen = sizeof(key);
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, DEVENV_KEY_OWM_KEY, key, &klen);
        /* A missing location is not fatal: 0,0 is a defined place (the Gulf of Guinea) and
         * the resulting reading is obviously wrong, which is better than no reading and a
         * silent failure. */
        size_t llen = sizeof(double);
        nvs_get_blob(h, DEVENV_KEY_LOC_LAT, &lat, &llen);
        llen = sizeof(double);
        nvs_get_blob(h, DEVENV_KEY_LOC_LON, &lon, &llen);
        nvs_close(h);
    }
    free(json);

    if (key[0] == '\0') {
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
    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather"
             "?lat=%.6f&lon=%.6f&units=imperial&appid=%s", lat, lon, key);

    if (net_http_get_json(url, NULL, resp, resplen) != ESP_OK) {
        ESP_LOGW(TAG, "OWM request failed");
        return -1;
    }

    *out = owm_parse_current_temp(resp, (long)(esp_timer_get_time() / 1000000LL));

    /* Account for the call now that the response has supplied its own timestamp — the day it
     * belongs to is not knowable before the reply arrives. An error response carries no
     * timestamp, so observed_at stays 0 and api_owm_note_call() ignores it: a failed request
     * still costs a call, but crediting it to the wrong day would be worse than undercounting. */
    api_owm_note_call(out->observed_at, 1);

    return out->status == DATASRC_OK ? 0 : -1;
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

    if (provscreen_render(work, ap_ssid, PROV_POP_STRING) != 0) {
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
    (void)source;

    /* Only the RESIDENT framebuffer is acquired here — s_fb_prev, the frame on the glass.
     * The transient one is taken later, once the radio has been torn down on battery. See the
     * block comment above for why holding both this early starves esp_wifi_init(). */
    if (ensure_prev() != 0) {
        api_note_error("render: out of memory for framebuffers");
        return;
    }

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
     * 4 KB is still ~8x the real response: OWM 2.5/weather for a full document with a long
     * place name measures ~513 bytes. It cannot be shrunk much further without risking
     * net_http_get_json() failing with ESP_ERR_NO_MEM on a genuinely larger response, which
     * would silently stop the display updating. */
    static char resp[4096];
    datasrc_value_t temp;
    memset(&temp, 0, sizeof(temp));
    const int fetched = fetch_current(resp, sizeof(resp), &temp) == 0;

    /* The radio is torn down BEFORE the ADC read and before any panel work (HW-3, NFR-3) —
     * but ONLY on battery. On USB the device stays awake to serve the API (FR-31), and the
     * API is useless without a network, so the connection is kept up deliberately. */
    if (source != POWER_SOURCE_USB) {
        net_wifi_disconnect();
    }

    if (!fetched) {
        /* Not fatal, and NOT a reason to skip the render: the panel still gets whatever
         * static layer is live (which is the whole point when the web app has just uploaded
         * one), with the last known reading if there is one. The error is recorded for
         * /api/status either way (FR-33). */
        api_note_error("owm: no current reading");
        ESP_LOGW(TAG, "fetch failed; using the last known reading");
    } else {
        s_last_temp = temp;
        s_have_last_temp = 1;
    }

    /* Decide full vs partial through the TESTED policy, never an inline comparison. */
    const int limit = api_partial_limit();
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
    const int have_next = (acquire_next() == 0);
    if (!have_next) {
        ESP_LOGW(TAG, "no second framebuffer (free %u, largest %u); falling back to a full refresh",
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }
    uint8_t *const work = have_next ? s_fb_next : s_fb_prev;

    /* Build the new frame from the static layer plus the freshest reading we have. */
    int slot = -1;
    if (load_static_layer(work, &slot) != 0) return;

    char tbuf[16];
    datasrc_value_t shown;
    memset(&shown, 0, sizeof(shown));
    if (s_have_last_temp) shown = s_last_temp;
    else shown.status = DATASRC_ERR_UNAVAILABLE;   /* renders as "--" */
    fmt_temp(tbuf, sizeof(tbuf), &shown);
    const char *values[2] = { tbuf, "--" };

    canvas_t c;
    canvas_init(&c, work);
    if (render_compose_stream(&c, flash_reader, work,
                              DEFAULT_FIELDS, values, 2) != 0) {
        ESP_LOGE(TAG, "compose failed");
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
    ESP_LOGI(TAG, "%s refresh done: %s",
             e == ESP_OK && have_next ? (kind == REFRESH_PARTIAL ? "partial" : "full") : "full",
             tbuf);

    /* Back to deep sleep (FR-12). The image is bistable and survives it, and leaving the
     * controller powered costs current for no benefit. On battery the device deep-sleeps
     * immediately after this anyway, so this only matters for the USB serve loop. */
    epd_sleep();
}
