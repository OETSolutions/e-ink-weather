/* NVS storage for the fields provisioning collects.
 *
 * Kept separate from the transport code so there is exactly ONE place that writes
 * credentials. Both transports — the BLE app through the manager, and our captive portal
 * directly — end up here, so a bug in key naming or a missing commit cannot affect only one
 * of them and be missed in testing on the other. */

#include "prov.h"
#include "nvs_keys.h"

#include <string.h>
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "prov_store";

/* NVS string values are read back into fixed buffers by the rest of the firmware
 * (app_refresh.c uses 64 for the SSID and 128 for the password), so anything longer is
 * rejected here rather than silently truncated into a wrong-but-plausible credential. */
#define PROV_MAX_SSID 32
#define PROV_MAX_PASS 64

static esp_err_t open_devcfg(nvs_handle_t *h, nvs_open_mode_t mode)
{
    esp_err_t e = nvs_open(DEVENV_NVS_NAMESPACE, mode, h);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "cannot open NVS namespace \"%s\": %s",
                 DEVENV_NVS_NAMESPACE, esp_err_to_name(e));
    }
    return e;
}

int prov_is_configured(void)
{
    nvs_handle_t h;
    /* Opened READONLY on purpose: this is called on every boot, and a read that created the
     * namespace as a side effect would turn "has never been configured" into "has an empty
     * config" — a distinction the rest of this file depends on. */
    esp_err_t e = nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h);
    /* ESP_ERR_NVS_NOT_FOUND means the namespace does not exist yet, which is exactly the
     * unconfigured first-boot state. It is the NORMAL path on a new device, so it is not
     * logged as an error — doing so would put a scary red line in the boot log of every unit
     * the user ever sets up. */
    if (e == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "cannot open NVS namespace \"%s\": %s",
                 DEVENV_NVS_NAMESPACE, esp_err_to_name(e));
        return 0;
    }

    char ssid[PROV_MAX_SSID + 1] = {0};
    size_t len = sizeof(ssid);
    const esp_err_t g = nvs_get_str(h, DEVENV_KEY_WIFI_SSID, ssid, &len);
    nvs_close(h);
    /* A zero-length SSID counts as unconfigured: an earlier revision could write an empty
     * string, and treating that as "configured" would leave the device unreachable with no
     * way to fix it except a full erase. */
    return (g == ESP_OK && ssid[0] != '\0') ? 1 : 0;
}

esp_err_t prov_store_credentials(const char *ssid, const char *pass)
{
    if (!ssid || !*ssid) return ESP_ERR_INVALID_ARG;
    if (strlen(ssid) > PROV_MAX_SSID) {
        ESP_LOGE(TAG, "SSID is %u bytes; the limit is %d", (unsigned)strlen(ssid), PROV_MAX_SSID);
        return ESP_ERR_INVALID_ARG;
    }
    if (pass && strlen(pass) > PROV_MAX_PASS) {
        ESP_LOGE(TAG, "password is %u bytes; the limit is %d",
                 (unsigned)strlen(pass), PROV_MAX_PASS);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t e = open_devcfg(&h, NVS_READWRITE);
    if (e != ESP_OK) return e;

    e = nvs_set_str(h, DEVENV_KEY_WIFI_SSID, ssid);
    /* An open network has no password. Storing an empty string rather than skipping the
     * write is deliberate: otherwise a re-provision from a secured network to an open one
     * would leave the OLD password in flash and the device would keep failing to associate. */
    if (e == ESP_OK) e = nvs_set_str(h, DEVENV_KEY_WIFI_PASS, pass ? pass : "");
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);

    if (e == ESP_OK) {
        /* The SSID is logged, the password never is. The SSID is visible to any scanner
         * anyway, and seeing it confirms the right network was chosen. */
        ESP_LOGI(TAG, "stored WiFi credentials for \"%s\"", ssid);
    } else {
        ESP_LOGE(TAG, "storing credentials failed: %s", esp_err_to_name(e));
    }
    return e;
}

esp_err_t prov_forget(void)
{
    nvs_handle_t h;
    esp_err_t e = open_devcfg(&h, NVS_READWRITE);
    if (e != ESP_OK) return e;

    /* ESP_ERR_NVS_NOT_FOUND means it was already absent — the desired end state, not a
     * failure, and reporting it would make "forget" fail on an unconfigured device. */
    e = nvs_erase_key(h, DEVENV_KEY_WIFI_SSID);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_erase_key(h, DEVENV_KEY_WIFI_PASS);
    if (e == ESP_ERR_NVS_NOT_FOUND) e = ESP_OK;
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    return e;
}

int prov_store_extra_config(const char *owm_key, const char *ha_url,
                            const char *ha_token, double lat, double lon,
                            int have_location)
{
    nvs_handle_t h;
    if (open_devcfg(&h, NVS_READWRITE) != ESP_OK) return -1;

    int rc = 0;
    esp_err_t e = ESP_OK;

    /* Each field is written only when supplied. The portal submits the whole form, but the
     * user may leave fields blank on a later visit to change only one thing — and treating a
     * blank as "erase this" would wipe the API key because they came back to fix the
     * location. */
    if (owm_key && *owm_key)  e = nvs_set_str(h, DEVENV_KEY_OWM_KEY, owm_key);
    if (e == ESP_OK && ha_url && *ha_url)    e = nvs_set_str(h, DEVENV_KEY_HA_URL, ha_url);
    if (e == ESP_OK && ha_token && *ha_token) e = nvs_set_str(h, DEVENV_KEY_HA_TOKEN, ha_token);
    if (e == ESP_OK && have_location) {
        /* Stored as raw doubles, not strings: the value is used as a number to build the OWM
         * query, and a string would need locale-dependent parsing at every fetch. */
        e = nvs_set_blob(h, DEVENV_KEY_LOC_LAT, &lat, sizeof(lat));
        if (e == ESP_OK) e = nvs_set_blob(h, DEVENV_KEY_LOC_LON, &lon, sizeof(lon));
    }
    if (e == ESP_OK) e = nvs_commit(h);

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "storing extra config failed: %s", esp_err_to_name(e));
        rc = -1;
    }
    nvs_close(h);
    return rc;
}
