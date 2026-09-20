/* Provisioning orchestration: the BLE transport, the setup AP, and the shared exit (FR-30).
 *
 * See prov.h for why wifi_prov_mgr drives BLE and this file raises the AP separately, rather
 * than the manager doing both. The short version: the manager starts exactly one scheme, and
 * a protocomm instance binds exactly one transport, so "both" has to be assembled here. */

#include "prov.h"

#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "prov";

/* The setup AP's name. Prefixed rather than a bare "eink-weather" so it is identifiable in a
 * list of a dozen APs, and suffixed with the last three MAC bytes so two of these on one
 * bench are distinguishable — a real scenario here, since the user has more than one. It is
 * also the BLE device name, so the same string identifies the device in both transports. */
#define PROV_AP_PREFIX "EINK-WEATHER"

/* Proof-of-Possession for BLE provisioning.
 *
 * Deliberately a FIXED value, and deliberately logged. Security-1 with a PoP is what the
 * official ESP BLE Provisioning app expects; the PoP exists so a stranger in radio range
 * cannot claim the device during the minute it is provisionable, not to defend against
 * someone who can already read the serial log. A random PoP would be strictly worse here:
 * this device has no screen to show it on, so a random one would have to be logged anyway,
 * and it would break the documented bench procedure of "flash, read the log, enter the PoP".
 * It is meaningless once credentials are stored — the transport is torn down with it. */
#define PROV_POP PROV_POP_STRING

/* The longest the station may take to join and get an address before the credentials are
 * declared bad. The manager retries on transient reasons, so this is a ceiling on the whole
 * attempt, not on one association. */
#define PROV_CONNECT_TIMEOUT_MS (30000)

static EventGroupHandle_t s_events;
#define BIT_PROV_DONE  BIT0
#define BIT_PROV_FAIL  BIT1

/* Set while the manager is in its FAIL state, which is the only state from which it can be
 * told to try again. Read by the watchdog task below. */
static volatile int s_in_fail_state;

/* ------------------------------------------------------------------ helpers ---- */

/* A device-unique service name, so two units on a bench are not confused for one another.
 *
 * esp_read_mac() rather than esp_wifi_get_mac(): the MAC is a property of the chip, not of
 * the WiFi driver, and this is reachable before esp_wifi_init(). Reading it through the
 * driver would fail there and silently fall back to a name that collides with any other unit
 * being set up at the same time. */
static void make_service_name(char *out, size_t out_max)
{
    uint8_t mac[6] = {0};
    const esp_err_t e = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (e != ESP_OK) {
        /* Falling back to the base name is better than refusing to provision: an
         * unprovisionable device is useless, and a duplicate name is a nuisance the user can
         * work around by being near only one unit. */
        ESP_LOGW(TAG, "cannot read the MAC: %s", esp_err_to_name(e));
        strlcpy(out, PROV_AP_PREFIX, out_max);
        return;
    }
    snprintf(out, out_max, PROV_AP_PREFIX "-%02X%02X%02X", mac[3], mac[4], mac[5]);
}

/* Public accessor for the name, so the provisioning screen and this component cannot
 * disagree about what the device is called. */
void prov_service_name(char *out, size_t out_max)
{
    if (!out || out_max == 0) return;
    make_service_name(out, out_max);
}

/* ------------------------------------------------------------------ events ---- */

static void on_prov_event(void *user_data, wifi_prov_cb_event_t event, void *event_data)
{
    (void)user_data;
    switch (event) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "provisioning started");
        break;

    case WIFI_PROV_CRED_RECV: {
        /* The event data is the wifi_sta_config_t the app sent. Mirroring it into our own
         * NVS namespace HERE is what makes the manager's copy usable by the rest of the
         * firmware — see prov.h for why the manager's own storage is not enough. Doing it on
         * receipt rather than on success means a device that receives credentials and then
         * fails to join still has them recorded, so the next boot retries them normally
         * instead of raising the setup AP again. */
        const wifi_sta_config_t *sta = (const wifi_sta_config_t *)event_data;
        if (sta) {
            prov_store_credentials((const char *)sta->ssid, (const char *)sta->password);
        }
        /* The SSID is logged, the password never is. The SSID is visible to any scanner
         * anyway, and seeing it confirms the right network was chosen. */
        ESP_LOGI(TAG, "credentials received from the BLE app");
        break;
    }

    case WIFI_PROV_CRED_FAIL:
        /* The state is now FAIL, which is NOT a state wifi_prov_mgr_wait() returns from —
         * it only exits on IDLE. Without the recovery below, one typo in the WiFi password
         * would leave the device sitting in provisioning forever with no way back short of a
         * power cycle. s_in_fail_state lets the watchdog reset the state machine so the user
         * can simply try again. */
        ESP_LOGE(TAG, "credentials rejected — check the password");
        s_in_fail_state = 1;
        break;

    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "credentials verified against the network");
        break;

    case WIFI_PROV_END:
        ESP_LOGI(TAG, "provisioning session ended");
        break;

    default:
        break;
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT) return;

    if (id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a client joined the setup AP");
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGI(TAG, "a client left the setup AP");
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *d = (const wifi_event_sta_disconnected_t *)data;
        if (d) ESP_LOGD(TAG, "station disconnected (reason %d)", d->reason);
    }
}

/* ------------------------------------------------------------------ recovery ---- */

/* Puts the manager back into a state where it will accept another attempt after a failure.
 *
 * WHY A TASK AND NOT A DIRECT CALL FROM THE EVENT HANDLER: reset_sm_state_on_failure() takes
 * the manager's context lock, and the event callback runs with that lock already held — so
 * calling it from the handler deadlocks the device on the first bad password. The task also
 * gives the phone time to display the failure and lets the portal come back up before the
 * user is asked to try again. */
static void recovery_task(void *arg)
{
    const char *name = (const char *)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!s_in_fail_state) continue;

        s_in_fail_state = 0;
        ESP_LOGW(TAG, "resetting provisioning so the credentials can be re-entered");

        /* The session's teardown stopped the AP, so bring it back before the reset: the
         * user's browser has just been told the password was wrong, and it should be able to
         * try again without hunting for a page that no longer exists. */
        prov_ap_start(name);

        const esp_err_t e = wifi_prov_mgr_reset_sm_state_on_failure();
        if (e != ESP_OK) {
            /* Not fatal: the portal is up, so the user can retry from the browser even if
             * the manager will not accept a second BLE attempt. Logged because it changes
             * what the user can do next. */
            ESP_LOGW(TAG, "could not reset the manager state: %s", esp_err_to_name(e));
        }
    }
}

/* ------------------------------------------------------------------ start ---- */

/* Raise the AP after the manager has finished starting.
 *
 * WHY THIS IS A TASK: the manager forces WIFI_MODE_STA during start_provisioning ("necessary
 * for scanning to work"), so an AP configured before that call is switched off. It must
 * therefore come up after — but wifi_prov_mgr_wait() blocks, so "after" has to be another
 * task.
 *
 * It retries rather than giving up after one attempt because the mode change and the AP
 * config are asynchronous: esp_wifi_set_config() can land before the interface has finished
 * switching to APSTA, in which case the AP comes up with no SSID. Retrying costs a few
 * hundred milliseconds and removes a first-boot failure that would otherwise look like the
 * device ignoring the AP entirely. */
static void portal_task(void *arg)
{
    const char *name = (const char *)arg;

    for (int attempt = 0; attempt < 20; attempt++) {
        if (prov_ap_start(name) == ESP_OK) {
            ESP_LOGI(TAG, "setup AP is up (attempt %d)", attempt + 1);
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    /* The portal is a convenience, not a requirement: the BLE path is still live, so a
     * device that cannot raise an AP is still provisionable. Logged loudly because the user
     * who reaches for a browser will otherwise just see nothing. */
    ESP_LOGE(TAG, "could not raise the setup AP; use the BLE app instead");
    vTaskDelete(NULL);
}

esp_err_t prov_submit_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return ESP_ERR_INVALID_ARG;

    /* Record them first, so a device that never manages to join still knows what the user
     * intended and does not silently lose the setting. */
    const esp_err_t se = prov_store_credentials(ssid, pass);

    wifi_config_t wc = {0};
    strlcpy((char *)wc.sta.ssid, ssid, sizeof(wc.sta.ssid));
    if (pass) strlcpy((char *)wc.sta.password, pass, sizeof(wc.sta.password));
    wc.sta.threshold.authmode = WIFI_AUTH_OPEN;
    /* PMF capable but not required, matching net_wifi_connect(): many modern APs reject a
     * station that does not declare PMF support, even with the right password. */
    wc.sta.pmf_cfg.capable = true;
    wc.sta.pmf_cfg.required = false;

    const esp_err_t e = wifi_prov_mgr_configure_sta(&wc);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "manager would not take the credentials: %s", esp_err_to_name(e));
        return e;
    }

    /* A store failure is reported only if the manager also refused, so a working handover is
     * never turned into an error the user cannot act on. */
    return se == ESP_OK ? ESP_OK : se;
}

esp_err_t prov_run_if_unconfigured(void)
{
    if (prov_is_configured()) {
        ESP_LOGI(TAG, "already provisioned; skipping");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "no WiFi credentials — starting provisioning");

    /* NVS must be up before the manager, which persists its own state there. Same recovery as
     * the rest of the firmware: a full partition is fixable only by erasing it. */
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    if (e != ESP_OK) return e;

    /* Provisioning brings up its own network stack, so the netif and the default event loop
     * must exist first — the same ordering constraint the HTTP API has. */
    e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_events = xEventGroupCreate();
    if (!s_events) return ESP_ERR_NO_MEM;

    /* The WiFi driver must be up before the manager: the manager calls into esp_wifi_* (mode,
 * storage, scan) from the moment provisioning starts, and every one of those fails with
 * ESP_ERR_WIFI_NOT_INIT if the driver was never initialised. The manager does NOT init it
 * itself — IDF's own provisioning example calls esp_wifi_init() before wifi_prov_mgr_init().
 *
 * This is safe to call here because provisioning only runs on a device that has never been
 * configured, and the normal boot path that also calls esp_wifi_init() (net_wifi_connect)
 * is unreachable from here: this function restarts the device on success. */
    wifi_init_config_t wic = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&wic);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(e));
        return e;
    }

    /* The station netif is ours to create — the manager does not make one. */
    if (!esp_netif_create_default_wifi_sta()) {
        ESP_LOGE(TAG, "cannot create the station interface");
        return ESP_FAIL;
    }

    e = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL);
    if (e != ESP_OK) return e;

    const wifi_prov_mgr_config_t cfg = {
        /* BLE is the manager's scheme (FR-30.2). The AP is raised separately — see prov.h. */
        .scheme = wifi_prov_scheme_ble,
        /* Release the BT controller's memory once the session ends. This device has no other
         * use for Bluetooth, and holding ~70 KB of controller RAM on a battery-powered
         * display for the rest of its life would be waste (NFR-3). */
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
        .app_event_handler = {
            .event_cb  = on_prov_event,
            .user_data = NULL,
        },
    };

    e = wifi_prov_mgr_init(cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %s", esp_err_to_name(e));
        return e;
    }

    bool provisioned = false;
    e = wifi_prov_mgr_is_provisioned(&provisioned);
    if (e != ESP_OK) {
        wifi_prov_mgr_deinit();
        return e;
    }
    if (provisioned) {
        /* The manager's own record says yes but our NVS key says no. Trust ours and start
         * provisioning anyway: ours is what the refresh path reads, so a device the manager
         * considers done but which cannot fetch anything is broken from the user's point of
         * view. Re-provisioning is the fix. */
        ESP_LOGW(TAG, "the manager reports provisioned but no SSID is stored; re-provisioning");
    }

    char service_name[32];
    make_service_name(service_name, sizeof(service_name));

    /* The AP's name is fixed, not derived per-attempt, so portal_task and the recovery task
     * cannot disagree about what the network is called. Static because the task reads it
     * after this function's frame is gone. */
    static char ap_ssid[32];
    strlcpy(ap_ssid, service_name, sizeof(ap_ssid));

    ESP_LOGI(TAG, "=== BLE: device \"%s\", proof of possession \"%s\" ===",
             service_name, PROV_POP);
    ESP_LOGI(TAG, "=== or connect to WiFi \"%s\" and open http://192.168.4.1 ===",
             service_name);

    /* NULL service key: for the BLE scheme the key is ignored (it is the SoftAP password in
     * the SoftAP scheme), and passing one here would suggest the AP is protected when it is
     * not — the security note in prov_ap.c explains why it deliberately is not. */
    e = wifi_prov_mgr_start_provisioning(WIFI_PROV_SECURITY_1, PROV_POP, service_name, NULL);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "cannot start provisioning: %s", esp_err_to_name(e));
        wifi_prov_mgr_deinit();
        return e;
    }

    /* Only now can the AP be raised: the manager has just forced WIFI_MODE_STA. ap_ssid is
     * static, so the task can safely read it after this frame is gone. */
    xTaskCreate(portal_task, "prov_portal", 4096, ap_ssid, 5, NULL);
    xTaskCreate(recovery_task, "prov_recover", 3072, ap_ssid, 5, NULL);

    /* Block until the session succeeds. This does NOT return on a bad password — the manager
     * parks in its FAIL state — which is why recovery_task exists to dig it back out. */
    wifi_prov_mgr_wait();

    ESP_LOGI(TAG, "provisioning succeeded");

    /* Stop the portal before restarting. The restart would clean it up anyway, but leaving an
     * open AP broadcasting for even a moment after the device is configured is the exact
     * thing this design promises not to do. */
    prov_ap_stop();
    wifi_prov_mgr_deinit();

    /* Restart rather than returning: the manager leaves the WiFi driver initialised, and the
     * normal boot path calls esp_wifi_init() again, which fails with ESP_ERR_WIFI_INIT_STATE.
     * A device that came up "provisioned" but unable to use the network would be worse than
     * one more reboot. This also guarantees the BLE stack and the setup AP are truly gone. */
    ESP_LOGI(TAG, "restarting into normal operation");
    vTaskDelay(pdMS_TO_TICKS(500));   /* let the log reach the UART */
    esp_restart();

    return ESP_OK;   /* not reached */
}
