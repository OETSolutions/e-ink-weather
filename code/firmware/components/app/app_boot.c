#include "app_boot.h"
#include "app_refresh.h"
#include "api.h"
#include "api_ota.h"
#include "cfg_store.h"
#include "api_store.h"
#include "epd.h"
#include "factory_reset.h"
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
    app_refresh_tick(source);
}

void app_boot_run(void)
{
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
    api_note_vbat(vbat, (int)source);
    ESP_LOGI(TAG, "vbat=%.2fV source=%s%s", vbat,
             source == POWER_SOURCE_USB ? "usb"
             : source == POWER_SOURCE_BATTERY ? "battery" : "unknown",
             (source != detected) ? " (powerMode override)" : "");

    /* ---- 3. Panel: last good image FIRST, before any network work (FR-29) ---- */
    if (epd_init() != ESP_OK) {
        /* A BUSY timeout means the panel did not answer — a loose FPC or an unpowered
         * panel. Log it and carry on: the device must still come up and stay reachable
         * over WiFi rather than hanging or reboot-looping against a wall (Task 6 step 6a). */
        ESP_LOGE(TAG, "panel did not respond (BUSY timeout) — skipping draw, continuing");
        api_note_error("boot: panel not responding");
    } else {
        const esp_err_t r = app_render_last_good();
        if (r != ESP_OK) {
            ESP_LOGW(TAG, "no last-good image to show: %s", esp_err_to_name(r));
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
        /* Whole seconds from the config, clamped by the parser to [30, 604800]. */
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

    /* The API handler sets the flag and notifies this task; the flag is the durable record
     * and the notification is only what makes the wait end promptly. Waking on the flag
     * alone is therefore correct even if a notification is missed — which matters because a
     * missed notification would otherwise leave a refresh request silently unserved. */
    for (;;) {
        /* Long enough that an idle device is not burning CPU, short enough that a refresh
         * request never feels stuck. Every wake re-reads the flag, so a request that lands
         * just after a timeout is served on the next pass regardless of the notification. */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        /* The serve loop is where a plugged-in device spends its life, so this is the most
         * likely place a user actually holds the button. */
        if (factory_reset_poll()) return;
        if (api_take_full_refresh()) {
            ESP_LOGI(TAG, "refresh requested via API");
            app_refresh_tick(POWER_SOURCE_USB);
        }
    }
}
