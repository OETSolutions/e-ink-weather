#include "net_wifi.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "net_wifi";

#define BIT_CONNECTED   BIT0
#define BIT_FAILED      BIT1

static EventGroupHandle_t s_events;
static esp_netif_t       *s_netif;
static int                s_connected;
static int                s_last_disconnect_reason;

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = (wifi_event_sta_disconnected_t *)data;
            s_connected = 0;
            s_last_disconnect_reason = d ? d->reason : 0;
            /* Do NOT reconnect from here. The handler runs on the event task, and an
             * unbounded retry loop inside it would spin forever on a bad password — the
             * exact hang NFR-1 forbids. Report the failure and let the bounded wait in
             * net_wifi_connect() decide. */
            xEventGroupSetBits(s_events, BIT_FAILED);
            break;
        }
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_connected = 1;
        xEventGroupSetBits(s_events, BIT_CONNECTED);
    }
}

/* Guarded by s_netif, which is only set once the stack is fully up — so a second call is a
 * no-op rather than a double init. Every step below returns ESP_ERR_INVALID_STATE when it
 * has already run, and that is treated as success: the init is idempotent by design, and
 * treating "already done" as fatal is what would break the second wake in a power cycle. */
esp_err_t net_stack_init(void)
{
    if (s_netif) return ESP_OK;

    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return e;

    s_netif = esp_netif_create_default_wifi_sta();
    if (!s_netif) return ESP_FAIL;

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&ic);
    if (e != ESP_OK) return e;

    /* The station event handlers are registered here, not in net_wifi_connect(), because the
     * API server needs the stack up on a device that has never connected — and a later
     * connect must not re-register them. */
    e = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL, NULL);
    if (e != ESP_OK) return e;
    e = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, on_wifi_event, NULL, NULL);
    if (e != ESP_OK) return e;

    return ESP_OK;
}

esp_err_t net_wifi_connect(const char *ssid, const char *pass, int timeout_ms)
{
    if (!ssid || !*ssid) return ESP_ERR_INVALID_ARG;
    if (timeout_ms <= 0) timeout_ms = 15000;

    if (s_events) {
        /* Already initialised by a previous wake in this power cycle. If the station is
         * ALREADY associated, this is a no-op: the API stays reachable across refresh
         * requests on USB power, so reconnecting is not just unnecessary, it actively
         * breaks. esp_wifi_connect() while connected logs "sta is connected, disconnect
         * before connecting to new ap" and then fails with reason 0 — which the retry
         * filter below correctly refuses to retry — so every refresh after the first would
         * be dropped and the panel would silently stop updating. */
        if (s_connected) return ESP_OK;
        /* Otherwise retry from a clean slate: clear the stale result bits before asking the
         * driver to connect again, or a failure from the previous attempt would be read as
         * this attempt's outcome. */
        xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED);
        esp_wifi_connect();
    } else {
        s_events = xEventGroupCreate();
        if (!s_events) return ESP_ERR_NO_MEM;

        /* The WiFi driver persists calibration and PHY data through NVS, so NVS must be up
         * before esp_wifi_init(). Without this the driver fails with
         * ESP_ERR_NVS_NOT_INITIALIZED. ESP_ERR_NVS_NO_FREE_PAGES means the partition is
         * full or corrupt — recoverable only by erasing it, which loses provisioning, so
         * it is done once and retried rather than failing the wake. */
        esp_err_t ne = nvs_flash_init();
        if (ne == ESP_ERR_NVS_NO_FREE_PAGES || ne == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS unusable (%s); erasing and retrying", esp_err_to_name(ne));
            ESP_ERROR_CHECK(nvs_flash_erase());
            ne = nvs_flash_init();
        }
        if (ne != ESP_OK) return ne;

        /* The stack (esp_netif, the default event loop, the WiFi driver and the event
         * handlers) is brought up by net_stack_init() so the HTTP API can listen on a
         * device that has never connected. */
        esp_err_t e = net_stack_init();
        if (e != ESP_OK) return e;

        wifi_config_t wc;
        memset(&wc, 0, sizeof(wc));
        /* Credentials come from provisioning (FR-30) and are already in NVS; keeping the
         * station config in RAM avoids rewriting flash on every wake (NFR-3). */
        e = esp_wifi_set_storage(WIFI_STORAGE_RAM);
        if (e != ESP_OK) return e;
        /* Truncation is rejected, not silently clipped: a 33-byte SSID would connect to
         * the wrong network or fail with a misleading reason. */
        if (strlen(ssid) >= sizeof(wc.sta.ssid) ||
            (pass && strlen(pass) >= sizeof(wc.sta.password))) {
            ESP_LOGE(TAG, "ssid/password longer than the driver allows");
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(wc.sta.ssid, ssid, strlen(ssid));
        if (pass) memcpy(wc.sta.password, pass, strlen(pass));
        wc.sta.threshold.authmode = WIFI_AUTH_OPEN;   /* allow open networks too */
        /* 802.11w: many modern APs advertise Protected Management Frames. A station that
         * does not declare PMF capability can have its association rejected outright
         * (observed here as WIFI_REASON_ASSOC_EXPIRE, reason 4) even with a correct
         * password. `capable` but not `required` accepts both PMF and non-PMF APs. */
        wc.sta.pmf_cfg.capable = true;
        wc.sta.pmf_cfg.required = false;

        e = esp_wifi_set_mode(WIFI_MODE_STA);
        if (e != ESP_OK) return e;
        e = esp_wifi_set_config(WIFI_IF_STA, &wc);
        if (e != ESP_OK) return e;
        /* Power save off. The device is awake only long enough to fetch and render, so
         * saving power here buys nothing, and modem sleep is a known source of
         * association/auth timing failures (reason 2/15) on a first connect. */
        e = esp_wifi_set_ps(WIFI_PS_NONE);
        if (e != ESP_OK) return e;

        e = esp_wifi_start();   /* triggers STA_START -> connect */
        if (e != ESP_OK) return e;
    }

    /* Retry within the deadline, but only for reasons that a retry can fix. A wrong
     * password must fail immediately — retrying it just holds the radio on and drains the
     * battery (NFR-3) while producing the same error. Association reasons (4, 17, 34, 200,
     * 201, 203) are often transient, so those are worth another attempt. */
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(timeout_ms);
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_events, BIT_CONNECTED | BIT_FAILED,
                                               pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
        if (bits & BIT_CONNECTED) {
            ESP_LOGI(TAG, "connected, rssi=%d", net_wifi_rssi());
            return ESP_OK;
        }

        int reason = s_last_disconnect_reason;
        /* 2 (AUTH_EXPIRE) and 15 (4WAY_HANDSHAKE_TIMEOUT) are included because they are
         * frequently timing artefacts rather than bad credentials — power-save and a slow
         * first association both produce them. 202 (AUTH_FAIL) is deliberately NOT
         * retryable: that one means the AP rejected the key. */
        int retryable = (reason == 2 || reason == 4 || reason == 15 || reason == 17 ||
                         reason == 34 || reason == 200 || reason == 201 || reason == 203);
        if (!retryable) {
            ESP_LOGW(TAG, "connect rejected (reason %d) — not retrying", reason);
            return ESP_ERR_INVALID_STATE;
        }
        if (xTaskGetTickCount() >= deadline) {
            ESP_LOGW(TAG, "connect timed out after %d ms (last reason %d)", timeout_ms, reason);
            return ESP_ERR_TIMEOUT;
        }

        ESP_LOGW(TAG, "association failed (reason %d); retrying", reason);
        xEventGroupClearBits(s_events, BIT_CONNECTED | BIT_FAILED);
        esp_wifi_connect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void net_wifi_disconnect(void)
{
    if (!s_events) return;
    /* Stop and deinit the radio for real. This is required, not tidiness: ADC2 cannot be
     * used while WiFi is active (HW-3), and an active radio in deep sleep would drain the
     * battery (NFR-3). */
    esp_wifi_disconnect();
    esp_wifi_stop();
    esp_wifi_deinit();
    s_connected = 0;
    vEventGroupDelete(s_events);
    s_events = NULL;
    if (s_netif) {
        esp_netif_destroy_default_wifi(s_netif);
        s_netif = NULL;
    }
}

int net_wifi_rssi(void)
{
    if (!s_connected) return 0;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return 0;
    return ap.rssi;
}

int net_wifi_connected(void)
{
    return s_connected ? 1 : 0;
}

int net_wifi_scan(net_wifi_ap_t *out, int max)
{
    if (!out || max <= 0) return -1;

    /* Scanning needs the driver up but NOT associated. Reuse the init path by connecting
     * with an empty SSID is not possible, so initialise the station directly. */
    esp_err_t e = nvs_flash_init();
    if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        e = nvs_flash_init();
    }
    if (e != ESP_OK) return -1;
    e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -1;
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) return -1;
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (!s_netif) return -1;
    }
    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    e = esp_wifi_init(&ic);
    if (e != ESP_OK) return -1;
    e = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (e != ESP_OK) return -1;
    e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e != ESP_OK) return -1;
    e = esp_wifi_start();
    if (e != ESP_OK) return -1;

    /* An active scan; show_hidden so a hidden SSID can still be provisioned. */
    wifi_scan_config_t sc = { .show_hidden = true };
    e = esp_wifi_scan_start(&sc, true);     /* true = block until done */
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "scan failed: %s", esp_err_to_name(e));
        return -1;
    }

    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    if (found > (uint16_t)max) found = (uint16_t)max;
    if (found == 0) return 0;

    wifi_ap_record_t *recs = calloc(found, sizeof(*recs));
    if (!recs) return -1;
    uint16_t n = found;
    e = esp_wifi_scan_get_ap_records(&n, recs);
    if (e != ESP_OK) {
        free(recs);
        return -1;
    }
    for (uint16_t i = 0; i < n; i++) {
        /* The driver does not NUL-terminate when an SSID fills all 32 bytes. */
        memcpy(out[i].ssid, recs[i].ssid, sizeof(out[i].ssid) - 1);
        out[i].ssid[sizeof(out[i].ssid) - 1] = '\0';
        out[i].rssi = recs[i].rssi;
        out[i].channel = recs[i].primary;
        out[i].authmode = recs[i].authmode;
    }
    free(recs);
    return (int)n;
}
