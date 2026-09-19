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
#include "owm.h"
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
extern const uint8_t gimage_1[EPD_FB_BYTES];

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

/* Allocate both framebuffers on first use. Returns 0 on success, -1 if either allocation
 * failed — in which case the caller must not render, because a half-allocated pair would
 * make a partial refresh diff against uninitialised memory. */
static int ensure_fbs(void)
{
    if (s_fb_prev && s_fb_next) return 0;

    if (!s_fb_prev)  s_fb_prev  = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);
    if (!s_fb_next)  s_fb_next  = heap_caps_malloc(EPD_FB_BYTES, MALLOC_CAP_8BIT);

    if (!s_fb_prev || !s_fb_next) {
        /* Release whichever succeeded, so a retry does not leak and the failure is
         * reported honestly rather than as a later crash. */
        free(s_fb_prev); s_fb_prev = NULL;
        free(s_fb_next); s_fb_next = NULL;
        ESP_LOGE(TAG, "cannot allocate %u bytes for two framebuffers (free heap %u)",
                 (unsigned)(2 * EPD_FB_BYTES), (unsigned)esp_get_free_heap_size());
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
    memcpy(dst, gimage_1, EPD_FB_BYTES);
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
    if (ensure_fbs() != 0) return ESP_ERR_NO_MEM;

    int slot = -1;
    if (load_static_layer(s_fb_next, &slot) != 0) return ESP_ERR_INVALID_STATE;

    /* No live values yet (this runs before the network), so compose with the static layer
     * alone. The frame is what the previous power cycle left, which is the point: FR-29
     * wants the last good image visible immediately, not a blank panel during the fetch. */
    canvas_t c;
    canvas_init(&c, s_fb_next);
    if (render_compose_stream(&c, flash_reader, s_fb_next, NULL, NULL, 0) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const esp_err_t e = epd_write_frame(s_fb_next);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel write failed: %s", esp_err_to_name(e));
        api_note_error("render: panel write failed");
        return e;
    }

    /* This frame is now on the glass, so it becomes the "previous" frame a later partial
     * refresh will be diffed against. */
    memcpy(s_fb_prev, s_fb_next, EPD_FB_BYTES);
    s_shown_slot = slot;
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
    if (nvs_open("devcfg", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, "owm_key", key, &klen);
        /* A missing location is not fatal: 0,0 is a defined place (the Gulf of Guinea) and
         * the resulting reading is obviously wrong, which is better than no reading and a
         * silent failure. */
        size_t llen = sizeof(double);
        nvs_get_blob(h, "loc_lat", &lat, &llen);
        llen = sizeof(double);
        nvs_get_blob(h, "loc_lon", &lon, &llen);
        nvs_close(h);
    }
    free(json);

    if (key[0] == '\0') {
        ESP_LOGW(TAG, "no OWM key stored; skipping fetch");
        return -1;
    }

    snprintf(url, sizeof(url),
             "https://api.openweathermap.org/data/2.5/weather"
             "?lat=%.6f&lon=%.6f&appid=%s", lat, lon, key);

    if (net_http_get_json(url, NULL, resp, resplen) != ESP_OK) {
        ESP_LOGW(TAG, "OWM request failed");
        return -1;
    }

    *out = owm_parse_current_temp(resp, (long)(esp_timer_get_time() / 1000000LL));
    return out->status == DATASRC_OK ? 0 : -1;
}

void app_refresh_tick(power_source_t source)
{
    (void)source;

    /* Allocate before the network work, so a device that cannot afford the framebuffers
     * fails the fetch too — rather than spending a TLS handshake and then discovering it
     * has nowhere to draw. */
    if (ensure_fbs() != 0) {
        api_note_error("render: out of memory for framebuffers");
        return;
    }

    /* WiFi credentials live in NVS, written by provisioning (FR-30) — never compiled in.
     * net_wifi_connect() needs them explicitly, so they are read here rather than assumed. */
    char ssid[64] = {0};
    char pass[128] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(pass);
    nvs_handle_t h;
    if (nvs_open("devcfg", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, "wifi_ssid", ssid, &slen);
        nvs_get_str(h, "wifi_pass", pass, &plen);
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

    static char resp[8192];
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

    /* Build the new frame from the static layer plus the freshest reading we have. */
    int slot = -1;
    if (load_static_layer(s_fb_next, &slot) != 0) return;

    char tbuf[16];
    datasrc_value_t shown;
    memset(&shown, 0, sizeof(shown));
    if (s_have_last_temp) shown = s_last_temp;
    else shown.status = DATASRC_ERR_UNAVAILABLE;   /* renders as "--" */
    fmt_temp(tbuf, sizeof(tbuf), &shown);
    const char *values[2] = { tbuf, "--" };

    canvas_t c;
    canvas_init(&c, s_fb_next);
    if (render_compose_stream(&c, flash_reader, s_fb_next,
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

    if (kind == REFRESH_FULL || s_shown_slot != slot) {
        /* A full refresh is also forced when the static layer changed: a partial against a
         * previous frame from a DIFFERENT image would diff two unrelated pictures and leave
         * ghosted fragments of the old layout behind. */
        e = epd_write_frame(s_fb_next);
        if (e == ESP_OK) {
            api_record_refresh(1);
            /* Only a full refresh restarts the 24 h clock: a partial does not clear
             * ghosting, so it cannot postpone the need for one. */
            s_last_full_us = esp_timer_get_time();
        }
    } else {
        /* Partial takes the frame currently on the glass AND the new one. */
        e = epd_write_frame_partial(s_fb_prev, s_fb_next);
        if (e == ESP_OK) api_record_refresh(0);
    }

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "panel update failed: %s", esp_err_to_name(e));
        api_note_error("render: panel update failed");
        return;
    }

    memcpy(s_fb_prev, s_fb_next, EPD_FB_BYTES);
    s_shown_slot = slot;
    ESP_LOGI(TAG, "%s refresh done: %s",
             kind == REFRESH_FULL ? "full" : "partial", tbuf);

    /* Back to deep sleep (FR-12). The image is bistable and survives it, and leaving the
     * controller powered costs current for no benefit. On battery the device deep-sleeps
     * immediately after this anyway, so this only matters for the USB serve loop. */
    epd_sleep();
}
