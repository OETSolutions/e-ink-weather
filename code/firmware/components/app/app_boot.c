#include "app_boot.h"
#include "app_refresh.h"
#include "api.h"
#include "api_ota.h"
#include "cfg_store.h"
#include "api_store.h"
#include "epd.h"
#include "factory_reset.h"
#include "gt30.h"
#include "layout_model.h"
#include "nvs_keys.h"
#include "power.h"
#include "prov.h"
#include "refresh_policy.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "boot";

/* Step 2 of FR-28: read VBAT with the WiFi radio OFF.
 *
 * The divider lands on GPIO26 = ADC2_CH9 (verified from the schematic), and ADC2 is
 * UNUSABLE while WiFi is active (HW-3) — the radio owns the ADC. Reading it after the
 * network comes up returns a plausible-looking wrong number, which then picks the wrong
 * power source and the wrong sleep behaviour. Hence the ordering. */
static double read_vbat(void)
{
    adc_oneshot_unit_handle_t adc = NULL;
    adc_oneshot_unit_init_cfg_t init = { .unit_id = ADC_UNIT_2 };
    if (adc_oneshot_new_unit(&init, &adc) != ESP_OK) return 0.0;

    adc_oneshot_chan_cfg_t ch = {
        .atten = ADC_ATTEN_DB_12,      /* ~0..3.1 V usable range */
        .bitwidth = ADC_BITWIDTH_12,
    };
    if (adc_oneshot_config_channel(adc, ADC_CHANNEL_9, &ch) != ESP_OK) {
        adc_oneshot_del_unit(adc);
        return 0.0;
    }

    /* Average a few reads. The divider is 300k/1M from a Li-ion cell, which is a high
     * enough source impedance that a single sample is noisy; averaging costs ~1 ms and
     * keeps the power-source decision from flickering between wake cycles. */
    long sum = 0;
    int n = 0;
    for (int i = 0; i < 8; i++) {
        int raw = 0;
        if (adc_oneshot_read(adc, ADC_CHANNEL_9, &raw) == ESP_OK) {
            sum += raw;
            n++;
        }
    }
    adc_oneshot_del_unit(adc);
    if (n == 0) return 0.0;

    const double vref = power_vref_from_raw((int)(sum / n), 4096, 3.3);
    return power_vbat_from_vref(vref);
}

/* Load the stored config, falling back to the default document. The config is the source of
 * the wake interval, the partial budget and the page rotation, so every downstream step
 * needs it; a corrupt store must not stop the device from coming up. */
static int load_config(layout_config_t *out)
{
    char *json = NULL;
    if (cfg_store_get(cfg_store_nvs(), &json) != 0) {
        json = strdup(cfg_store_default_json());
        if (!json) return -1;
    }
    const int rc = layout_config_parse(json, out);
    free(json);
    return rc;
}

void app_seed_wifi(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return;

    /* Called from app_main before the boot path's NVS init, so bring NVS up here. The call
     * is idempotent, and without it nvs_open fails with ESP_ERR_NVS_NOT_INITIALIZED — which
     * looks like a permissions problem rather than an ordering one. */
    esp_err_t ne = nvs_flash_init();
    if (ne == ESP_ERR_NVS_NO_FREE_PAGES || ne == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ne = nvs_flash_init();
    }
    if (ne != ESP_OK) {
        ESP_LOGW(TAG, "seed: nvs_flash_init failed: %s", esp_err_to_name(ne));
        return;
    }

    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "seed: cannot open NVS");
        return;
    }

    char existing[64] = {0};
    size_t n = sizeof(existing);
    if (nvs_get_str(h, DEVENV_KEY_WIFI_SSID, existing, &n) == ESP_OK && existing[0] != '\0') {
        /* Never overwrite: this must not be able to clobber a provisioned device, which is
         * the difference between a bench convenience and a footgun. */
        ESP_LOGI(TAG, "seed: credentials already present, leaving them alone");
        nvs_close(h);
        return;
    }

    esp_err_t e = nvs_set_str(h, DEVENV_KEY_WIFI_SSID, ssid);
    if (e == ESP_OK && pass) e = nvs_set_str(h, DEVENV_KEY_WIFI_PASS, pass);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "seed: %s", e == ESP_OK ? "wrote bench credentials" : "failed");
}

/* Bench-only: seed the OWM key into NVS if none is stored.
 *
 * WHY THE SAME NEVER-OVERWRITE RULE AS app_seed_wifi(): this exists so a bench build can fetch
 * real weather without being walked through the captive portal, and it must not be able to
 * clobber a device that a real user has provisioned. An overwrite would silently replace the
 * owner's key with the developer's every time this image is flashed.
 *
 * On a shipped build there is no such header, so this is not compiled and the key arrives only
 * through provisioning (FR-30). */
void app_seed_owm_key(const char *key)
{
    if (!key || !*key) return;

    esp_err_t ne = nvs_flash_init();
    if (ne == ESP_ERR_NVS_NO_FREE_PAGES || ne == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ne = nvs_flash_init();
    }
    if (ne != ESP_OK) return;

    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    char existing[DEVENV_BUF_OWM_KEY] = {0};
    size_t n = sizeof(existing);
    if (nvs_get_str(h, DEVENV_KEY_OWM_KEY, existing, &n) == ESP_OK && existing[0] != '\0') {
        ESP_LOGI(TAG, "seed: OWM key already present, leaving it alone");
        nvs_close(h);
        return;
    }
    esp_err_t e = nvs_set_str(h, DEVENV_KEY_OWM_KEY, key);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "seed: %s OWM key", e == ESP_OK ? "wrote bench" : "failed to write");
}

/* Bench-only: seed the Home Assistant URL and token into NVS if none is stored.
 *
 * WHY THIS EXISTS: the generator that emits secrets_build.h read only WiFi and the OWM key
 * from code/.env, so a bench device had NO Home Assistant credentials at all — every HA
 * widget resolved to its "--" fallback and the config app reported "No Home Assistant URL
 * configured", even though HA_URL and HA_TOKEN were sitting in .env the whole time. The
 * device-side path was correct (read_creds() reads both keys, fetch_ha() refuses without
 * them); nothing ever POPULATED the keys on a bench build, and there was no UI or endpoint
 * to do it after the fact either. This closes that gap for the bench path; PUT /api/secrets
 * covers a running device.
 *
 * Same never-overwrite contract as the other two seeders, and for the same reason: this must
 * not replace a real owner's HA instance whenever a developer flashes their bench image. A
 * partially-populated pair (URL with no token, or vice versa) is left ALONE rather than
 * half-written — HA needs both, and a URL with no token would make fetch_ha() log the same
 * "no url/token" warning that this function exists to remove. */
void app_seed_ha(const char *url, const char *token)
{
    if (!url || !*url || !token || !*token) return;

    esp_err_t ne = nvs_flash_init();
    if (ne == ESP_ERR_NVS_NO_FREE_PAGES || ne == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        ne = nvs_flash_init();
    }
    if (ne != ESP_OK) return;

    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;

    char existing[DEVENV_BUF_HA_URL] = {0};
    size_t n = sizeof(existing);
    if (nvs_get_str(h, DEVENV_KEY_HA_URL, existing, &n) == ESP_OK && existing[0] != '\0') {
        ESP_LOGI(TAG, "seed: HA URL already present, leaving it alone");
        nvs_close(h);
        return;
    }

    /* Both values are written together or not at all. Half a credential pair is worse than none:
     * fetch_ha() refuses without either, but a stored URL with no token would look configured and
     * fail at the request instead — and the guard at the top of this function already requires
     * both, so there is nothing left to re-check here. */
    esp_err_t e = nvs_set_str(h, DEVENV_KEY_HA_URL, url);
    if (e == ESP_OK) e = nvs_set_str(h, DEVENV_KEY_HA_TOKEN, token);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "seed: %s HA credentials", e == ESP_OK ? "wrote bench" : "failed to write");
}

/* Steps 4-6: connect if there are credentials, fetch, render, push.
 *
 * A separate function because the provisioning step has to sit AFTER it — there are no
 * credentials to connect with on an unconfigured device, so the fetch cannot run first — and
 * before the API comes up. Returns nothing: every failure inside is reported through the API
 * status and the last-good image, which is the FR-29 behaviour.
 *
 * On an unconfigured device this deliberately does NOT try to connect. The refresh path is
 * written to leave the panel showing the last good image when there is no network, which is
 * what the user sees while they are provisioning. */
static void app_boot_network_cycle(power_source_t source)
{
    /* force_full = 0: the boot path has no reason to pre-empt the policy — the pending-request
     * flag is read inside the tick itself. */
    app_refresh_tick(source, 0);
}

void app_boot_run(void)
{
    /* SAY WHY THE LAST BOOT ENDED. Without this there is NO record of a panic, a watchdog reset or a
     * brownout anywhere on the device: the only symptom is "it was dead and then it booted", which
     * is impossible to attribute to code, power or the network. This cost a full investigation into
     * an unreproducible freeze. The reason is one call and one line, and it is the first thing a
     * later boot can report about the previous one. */
    const esp_reset_reason_t rr = esp_reset_reason();
    const char *rrs = "unknown";
    switch (rr) {
        case ESP_RST_POWERON:  rrs = "power-on";          break;
        case ESP_RST_EXT:      rrs = "external pin";      break;
        case ESP_RST_SW:       rrs = "software restart";  break;
        case ESP_RST_PANIC:    rrs = "PANIC (crash)";     break;
        case ESP_RST_INT_WDT:  rrs = "interrupt WDT";     break;
        case ESP_RST_TASK_WDT: rrs = "task WDT";          break;
        case ESP_RST_WDT:      rrs = "other WDT";         break;
        case ESP_RST_DEEPSLEEP:rrs = "deep-sleep wake";   break;
        case ESP_RST_BROWNOUT: rrs = "BROWNOUT (power)";  break;
        case ESP_RST_SDIO:     rrs = "SDIO";              break;
        default: break;
    }
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT ||
        rr == ESP_RST_WDT || rr == ESP_RST_BROWNOUT) {
        ESP_LOGE(TAG, "previous boot ended abnormally: %s (reset reason %d)", rrs, (int)rr);
    } else {
        ESP_LOGI(TAG, "reset reason: %s", rrs);
    }

    /* ---- 1. NVS and config ---- */
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* A partition that needs erasing is a first boot or a version change, not a fault.
         * Leaving it uninitialised would make every later nvs_open fail, which surfaces as
         * "the device forgot its config" rather than as an NVS error. */
        ESP_LOGW(TAG, "NVS needs erase (%s); reformatting", esp_err_to_name(e));
        nvs_flash_erase();
        e = nvs_flash_init();
    }
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(e));
    }

    api_reset_cycle_counters();

    /* Bring the reset button up before anything slow, so a user who is holding it while the
     * device powers on is noticed as early as possible. */
    factory_reset_begin();

    layout_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (load_config(&cfg) != 0) {
        ESP_LOGW(TAG, "stored config unparseable; using defaults");
        api_note_error("boot: config unparseable");
        memset(&cfg, 0, sizeof(cfg));
        cfg.update_seconds = 900;
        cfg.partial_refresh_limit = 5;
        cfg.page_count = 1;
    }
    ESP_LOGI(TAG, "config: update=%ds partial_limit=%d pages=%d",
             cfg.update_seconds, cfg.partial_refresh_limit, cfg.page_count);

    /* ---- 2. Battery sense, with WiFi OFF (HW-3) ---- */
    const double vbat = read_vbat();
    /* trend 0.0: one sample cannot show a trend, and power_classify() treats a non-negative
     * trend as "not falling", so a full pack that is really discharging is not misread as
     * mains. The user's powerMode override in the config is the correction for a wrong
     * guess (FR-8). */
    power_source_t source = power_classify(vbat, 0.0);
    /* Apply the user's FR-8 override. The inference cannot tell a full resting cell from
     * mains — there is no resolvable USB-present pin on this board — so the config is the
     * correction, and a device whose owner said "battery" must deep-sleep even while its
     * pack happens to read above the mains threshold. */
    const power_source_t detected = source;
    source = power_apply_mode(source, cfg.power_mode);
    /* Cache it for /api/status: this is the only moment the battery can be read (HW-3). */
    api_vbat_history_boot();
    api_note_vbat(vbat, (int)source);
    ESP_LOGI(TAG, "vbat=%.2fV source=%s%s", vbat,
             source == POWER_SOURCE_USB ? "usb"
             : source == POWER_SOURCE_BATTERY ? "battery" : "unknown",
             (source != detected) ? " (powerMode override)" : "");

    /* ---- 3. Panel: last good image FIRST, before any network work (FR-29) ---- */

    /* Task 6b step 6: report the GT30 font-chip probe once per boot.
     *
     * IT MUST RUN BEFORE epd_init(), and that is not a style choice. The probe bit-bangs SCLK and
     * samples MISO, but once epd_init() brings the hardware SPI bus up those two pins belong to
     * the SPI peripheral — toggling them from GPIO has no effect, the clock never runs, and the
     * probe reads a constant and reports the chip ABSENT on a board where it is populated. Called
     * after epd_init() it produced exactly that false negative on this bench. Before the bus
     * exists the pins are plain GPIO, which is the state the probe was verified in.
     *
     * The chip's glyphs are not used for rendering — the flash atlas is (FR-4a) — so this is purely
     * the observability the plan promised; the probe caches its result and never blocks. */
    ESP_LOGI(TAG, "font chip: %s (probed before the SPI bus claims the pins)",
             gt30_present() ? "present" : "absent");

    if (epd_init() != ESP_OK) {
        /* A BUSY timeout means the panel did not answer — a loose FPC or an unpowered
         * panel. Log it and carry on: the device must still come up and stay reachable
         * over WiFi rather than hanging or reboot-looping against a wall (Task 6 step 6a). */
        ESP_LOGE(TAG, "panel did not respond (BUSY timeout) — skipping draw, continuing");
        api_note_error("boot: panel not responding");
    } else {
        /* DO NOT REDRAW THE IMAGE THAT IS ALREADY THERE AFTER A TIMER WAKE (FR-10…FR-13).
         *
         * This draw is for the FR-29 case: something must be on the glass immediately, before the
         * network, and on a cold boot the firmware cannot know what a power cut left there. After a
         * deliberate deep sleep it CAN know, and drawing then costs two things for nothing — the
         * frame on the glass is already the last good image (the panel is bistable), and this draw
         * paints the bare static layer, because the readings did not survive the sleep. So it would
         * REPLACE a frame with readings by one without, force a full flashing update on a device
         * that just woke to save power, and make the next tick put the numbers back.
         *
         * See boot_needs_last_good_draw() for the full reasoning; the decision is a tested function
         * rather than an `if` here so the panel-life rule lives with the other refresh policy. */
        const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        const int woke_from_timer = (cause == ESP_SLEEP_WAKEUP_TIMER);
        if (boot_needs_last_good_draw(woke_from_timer)) {
            const esp_err_t r = app_render_last_good();
            if (r != ESP_OK) {
                ESP_LOGW(TAG, "no last-good image to show: %s", esp_err_to_name(r));
            }
        } else {
            /* The frame the device slept with is still up. Record nothing about it: the refresh
             * tick re-derives whether it can diff against the glass from its own state, and
             * s_have_prev_values is 0 here precisely because this boot did not draw — which is the
             * honest answer, since the values stamped on that frame are not recoverable. */
            ESP_LOGI(TAG, "woke from timer; the frame on the glass is still the last good image, "
                          "so the pre-fetch draw is skipped (one panel update this wake)");
        }
        epd_sleep();
    }

    /* ---- 3b. Factory reset, if the button is held ----
     * Deliberately BEFORE the network work and before provisioning. A user holding the button at
     * power-on is asking to erase everything, and this is the only point that works from every
     * state — after this the path may block in provisioning, where nothing else polls.
     *
     * Placed after the panel has an image so the user is not left staring at a blank screen
     * while they hold. */
    if (factory_reset_check_hold()) return;

    /* ---- 3c. Provisioning is deferred to step 8 ---- */

    /* ---- 4-6. Network, fetch, render, push ---- */
    app_boot_network_cycle(source);

    /* ---- 7. Mark this image good, if it is on probation (FR-32) ----
     * Reaching here means the image booted AND completed a refresh cycle, which is exactly
     * the proof rollback protection is waiting for. Doing this earlier — at the top of
     * app_main, or in the OTA handler — would accept a build that boots but cannot draw. */
    api_ota_mark_valid_if_pending();

    /* ---- 8. Provisioning, if this device has never been set up (FR-30) ----
     * Placed AFTER the panel has its image so the user sees something on the glass while they
     * enter credentials, and AFTER the refresh cycle because there are no credentials to
     * fetch with. On success this does NOT return — it restarts the device (see prov.h).
     *
     * The framebuffers are RELEASED first, and that is the whole point of this ordering.
     * Provisioning is the most memory-hungry thing this firmware does — NimBLE's host and
     * controller, a SoftAP, the WiFi driver — and holding 152.7 KiB of framebuffers across it
     * starves esp_wifi_init(), which fails with ESP_ERR_NO_MEM ("Expected to init 10 rx
     * buffer, actual is 7") and leaves the device unreachable in exactly the state where it
     * has no other way to be configured. The image is already on the glass and the panel is
     * bistable, so giving the memory back costs nothing the user can see. */
    if (!prov_is_configured()) {
        /* Show HOW to set the device up, before releasing the memory the screen needs.
         *
         * WHY THIS IS DRAWN HERE AND NOT EARLIER: it needs one framebuffer, and this is the last
         * moment one is available — the release below frees both so the radio can come up. The
         * panel is bistable and is put to sleep after the push, so the instructions stay on the
         * glass for as long as provisioning takes, with no memory held.
         *
         * This is drawn rather than the last-good image on purpose: the last-good image is a
         * weather reading the user cannot act on, while the setup screen is the one thing they
         * need at exactly this moment. */
        app_render_setup_screen();

        app_fbs_release();

        const esp_err_t pe = prov_run_if_unconfigured();
        if (pe != ESP_OK) {
            /* Provisioning could not even start (no memory, WiFi would not come up). The
             * device stays reachable and unconfigured rather than boot-looping. Take the
             * framebuffers back so the API path can still draw, and report honestly if the
             * network stack's own allocations have made that impossible now. */
            ESP_LOGE(TAG, "provisioning failed to start: %s", esp_err_to_name(pe));
            api_note_error("boot: provisioning failed");
            if (app_fbs_reserve() != ESP_OK) {
                ESP_LOGE(TAG, "framebuffers could not be re-reserved after the failed start");
            }
        }
        /* Not reached on success: provisioning restarts the device, already configured. */
    }

    if (source == POWER_SOURCE_USB) {
        /* On USB the device must stay awake to serve the API (FR-31), so the server starts
         * AFTER the first render — the panel gets its image immediately, and the API comes
         * up as soon as there is something meaningful to report. Staying awake is also what
         * makes the web app usable at all: a device that sleeps cannot be configured. */
        const esp_err_t se = api_start();
        if (se != ESP_OK) {
            ESP_LOGE(TAG, "API did not start: %s", esp_err_to_name(se));
            api_note_error("boot: API failed to start");
        }
        ESP_LOGI(TAG, "on USB power: API up, staying awake");
        app_serve_loop();   /* never returns */
    }

    /* ---- 8. Sleep, if on battery (FR-9) ---- */
    {
        /* Whole seconds from the config, clamped by the parser to [5, 604800]. */
        const int wake_s = cfg.update_seconds > 0 ? cfg.update_seconds : 900;
        ESP_LOGI(TAG, "battery: sleeping %d s", wake_s);
        epd_sleep();
        esp_sleep_enable_timer_wakeup((uint64_t)wake_s * 1000000ULL);
        esp_deep_sleep_start();      /* does not return */
    }
}

/* ------------------------------------------------------------------ USB serve loop -- */

void app_serve_loop(void)
{
    api_set_refresh_task((void *)xTaskGetCurrentTaskHandle());

    /* Which refresh the serve loop should be doing, so the periodic and the on-demand paths
     * agree. api_take_full_refresh() already consumes a pending full request; the periodic
     * path only fires when that returned 0, so a user-requested full is never downgraded. */
    int want_full = 0;
    int elapsed_ms = 0;
    /* The interval is CACHED, and re-read only when an API request arrives — never on the
     * per-second tick. api_update_seconds() allocates a CFG_JSON_MAX_LEN (16 KB) buffer through
     * cfg_store_get() and frees it; doing that every second would drop a 16 KB block into the
     * heap 900 times between refreshes, which is the exact fragmentation pattern that starves the
     * 78,200-byte framebuffer on this part. A config change always arrives WITH a refresh request
     * (PUT /api/config calls api_request_full_refresh), so re-reading on the request is enough to
     * pick up an interval change and the periodic path never needs to call it. */
    int interval_ms = api_update_seconds() * 1000;

    /* Set after a tick that could not get its framebuffer, to make the NEXT refresh happen in
     * seconds rather than a whole interval — see app_refresh_frame_lost(). Kept SEPARATE from
     * interval_ms so a retry does not overwrite the configured cadence; when the retry fires,
     * interval_ms is untouched and the periodic schedule resumes from the normal interval. */
    int retry_in_ms = 0;

    for (;;) {
        /* POLL EVERY SECOND, and count up to the configured interval. The wait must NOT be the
         * whole interval: factory_reset_poll() needs to run repeatedly to see a 5 s button hold
         * complete, and it is this loop where a plugged-in device is most likely to be held — a
         * 900 s wait would mean the button could never be held long enough to register. A refresh
         * request short-circuits the wait via the task notification, so a request is still served
         * promptly rather than at the next tick. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        elapsed_ms += 1000;

        if (factory_reset_poll()) return;

        if (api_take_full_refresh()) {
            /* A pending request, whether from POST /api/refresh or a config/bitmap change,
             * forces a FULL refresh: the layout changed and a partial cannot clear the old
             * glyphs. Served immediately, not on the interval. The interval is re-read here
             * because a config change is what a request most often accompanies. */
            ESP_LOGI(TAG, "refresh requested via API");
            want_full = 1;
            interval_ms = api_update_seconds() * 1000;
            retry_in_ms = 0;
        } else if (retry_in_ms && elapsed_ms >= retry_in_ms) {
            /* The previous tick dropped its frame. Retry promptly with a full refresh (there is
             * no resident diff base to do a partial against). */
            ESP_LOGW(TAG, "retrying the refresh that could not get a framebuffer");
            want_full = 1;
            retry_in_ms = 0;
        } else if (elapsed_ms >= interval_ms) {
            /* THE TIMER ELAPSED, so this is the periodic refresh.
             *
             * Without this a device on mains — the main deployment — refreshed exactly ONCE, at
             * boot, and never again: update_seconds was consumed only by the deep-sleep timer on
             * the battery path, and the serve loop's sole trigger was an API request. A
             * plugged-in weather display then showed the temperature it woke up with, forever.
             *
             * It carries NO full-refresh request, so refresh_decide() runs normally and the panel
             * gets the partial-refresh behaviour FR-11 asks for. Forcing a full here would make a
             * mains device flicker a full refresh every interval. */
            ESP_LOGI(TAG, "periodic refresh (%d s)", interval_ms / 1000);
            want_full = 0;
        } else {
            continue;       /* not yet due, and nothing else to do this second */
        }

        elapsed_ms = 0;
        app_refresh_tick(POWER_SOURCE_USB, want_full);

        /* A tick that could not obtain its framebuffer drew nothing and left the last good
         * image on the glass, which is correct — but the region can stay fragmented for far
         * longer than the acquire wait (measured: tens of seconds). Retry in 5 s rather than
         * showing a stale reading until the next interval, which on a 900 s config would be
         * fifteen minutes of a wrong number on the wall. */
        if (app_refresh_frame_lost()) {
            ESP_LOGW(TAG, "no framebuffer for that refresh; retrying in 5 s");
            retry_in_ms = 5000;
        }
    }
}
