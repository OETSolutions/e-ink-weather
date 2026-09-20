/* A second provisioning transport, over HTTP, on the setup portal (FR-30).
 *
 * WHY THIS EXISTS: `wifi_prov_setup()` accepts exactly one scheme and a protocomm instance
 * carries exactly one transport, so the manager drives BLE and nothing else. This device wants
 * two: BLE for the ESP BLE Provisioning app, and the setup portal for the ESP SoftAP
 * Provisioning app — which is the official app that CAN show a network list, because it talks
 * HTTP rather than BLE and so never hits the link-timing failure that forced the BLE app to
 * manual SSID entry.
 *
 * Rather than run a second `wifi_prov_mgr` (a second manager would fight over the Wi-Fi
 * driver), this builds a SECOND protocomm instance over the portal's HTTP server and registers
 * the same endpoint handlers the manager uses. Everything downstream — apply-config connecting
 * the station, the WIFI_PROV_CRED_* events, the NVS mirror in prov.c — is shared with the BLE
 * path, so there is exactly one place credentials are stored whichever transport delivered them.
 *
 * THE TRANSPORT IS UNSECURED. Protocomm's HTTP transport serves in the clear: no TLS, no
 * session encryption, and the "prov-session" endpoint uses Security 0 (a no-op). The BLE path
 * uses Security 1 with a proof of possession; this one cannot, and the ESP SoftAP Provisioning
 * app expects that. It is acceptable only because of where it runs — the portal's AP is open
 * and exists solely while the device is unconfigured, at which point anyone in radio range can
 * already configure the device through the portal form (see the security note in prov_ap.c).
 * Adding a handshake here would not close a hole that is not otherwise open.
 *
 * ENDPOINT SET: proto-ver, prov-session, prov-config, prov-scan. The factory-reset endpoint
 * (prov-ctrl) is deliberately NOT exposed, so a passer-by cannot wipe the device this way.
 *
 * ENDPOINTS SIT AT THE ROOT (/proto-ver, /prov-config, ...), not under a prefix: protocomm's
 * HTTP transport derives each handler's URI from the endpoint name itself, and the app and
 * `esp_prov --transport softap` both POST to "/<endpoint>". The portal's own page keeps "/"
 * and its /save handler.
 *
 * THE SCAN CAPABILITY IS ADVERTISED HERE, unlike the BLE path. That asymmetry is the whole
 * point: the BLE app's list is unusable, so the BLE transport hides it; the SoftAP app's list
 * works, so this transport offers it. */

#include "prov_softap_prov.h"

#include "prov.h"
#include "esp_log.h"
#include "esp_err.h"

#include "protocomm.h"
#include "protocomm_httpd.h"
#include "protocomm_security1.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/wifi_config.h"
#include "wifi_provisioning/wifi_scan.h"

#include "cJSON.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "prov_softap_prov";

static protocomm_t *s_pc;

/* protocomm's HTTP transport stores `pc->priv` as a POINTER (it dereferences it as
 * `httpd_handle_t *` when registering each endpoint), but its `ext_handle_provided` path
 * assigns the handle value straight into `pc->priv`. That mismatch traps the first endpoint
 * registration with a LoadProhibited at address 0x23. So the handle is held here and its
 * ADDRESS is what gets passed in. (Upstream is not exercised this way because the SoftAP
 * scheme passes the pointer its own calloc'd copy lives in.) */
static void *s_httpd_handle;

/* Handler tables must outlive their registration: protocomm stores the pointer and calls into
 * it per request, so these are file-static rather than stack locals. */
static wifi_prov_config_handlers_t s_config_handlers;
static wifi_prov_scan_handlers_t   s_scan_handlers;

/* The version/capability document, built once. Kept alive because protocomm keeps the pointer
 * rather than copying it. */
static char *s_ver_json;

static esp_err_t add_version_endpoint(void)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *prov = cJSON_CreateObject();
    if (!root || !prov) { cJSON_Delete(root); cJSON_Delete(prov); return ESP_ERR_NO_MEM; }

    cJSON_AddItemToObject(root, "prov", prov);
    cJSON_AddStringToObject(prov, "ver", "v1.1");
    cJSON_AddNumberToObject(prov, "sec_ver", 1);   /* Security 1, same PoP as the BLE path */
    cJSON_AddStringToObject(prov, "sec_patch_ver", "0");
    cJSON *cap = cJSON_AddArrayToObject(prov, "cap");
    cJSON_AddItemToArray(cap, cJSON_CreateString("wifi_scan"));

    s_ver_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!s_ver_json) return ESP_ERR_NO_MEM;

    return protocomm_set_version(s_pc, "proto-ver", s_ver_json);
}

esp_err_t prov_softap_prov_start(void *httpd_handle)
{
    if (!httpd_handle) return ESP_ERR_INVALID_ARG;
    if (s_pc) return ESP_OK;                 /* already running */

    s_pc = protocomm_new();
    if (!s_pc) {
        ESP_LOGE(TAG, "cannot allocate protocomm instance");
        return ESP_ERR_NO_MEM;
    }

    /* Attach to the portal's HTTP server FIRST. protocomm registers each endpoint as a URI
     * handler on the server as it is added, so the transport has to own the server handle
     * before any endpoint is registered — the other order silently registers nothing. The
     * handle is passed by ADDRESS; see the note on s_httpd_handle. */
    s_httpd_handle = httpd_handle;
    protocomm_httpd_config_t cfg = {
        .ext_handle_provided = true,
        .data.handle = &s_httpd_handle,
    };
    esp_err_t e = protocomm_httpd_start(s_pc, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "cannot attach the HTTP transport: %s", esp_err_to_name(e));
        protocomm_delete(s_pc); s_pc = NULL;
        return e;
    }

    /* Security 1, with the same proof of possession the BLE path uses. The app opens a session
     * before anything else, so this endpoint must exist and use the scheme the app expects;
     * Security 0 would make it fail the handshake ("security scheme and proof of possession").
     * The params struct is static because protocomm keeps the pointer, not a copy. */
    static protocomm_security1_params_t s_sec1 = {
        .data = (const uint8_t *)PROV_POP_STRING,
        .len  = sizeof(PROV_POP_STRING) - 1,
    };
    e = protocomm_set_security(s_pc, "prov-session", &protocomm_security1, &s_sec1);
    if (e != ESP_OK) goto fail;

    e = add_version_endpoint();
    if (e != ESP_OK) goto fail;

    if (wifi_prov_get_config_handlers(&s_config_handlers) != ESP_OK ||
        wifi_prov_get_scan_handlers(&s_scan_handlers) != ESP_OK) {
        ESP_LOGE(TAG, "cannot obtain the provisioning handlers");
        e = ESP_FAIL; goto fail;
    }

    /* No wifi_prov_mgr_wifi_scan_start() call is wired here: the manager's prov-scan handlers
     * drive the scan themselves (wifi_prov_scan_cmd_dispatcher -> wifi_prov_mgr_wifi_scan_*),
     * which is exactly what the app's list needs. */
    e = protocomm_add_endpoint(s_pc, "prov-config",
                               wifi_prov_config_data_handler, &s_config_handlers);
    if (e != ESP_OK) goto fail;

    e = protocomm_add_endpoint(s_pc, "prov-scan",
                               wifi_prov_scan_handler, &s_scan_handlers);
    if (e != ESP_OK) goto fail;

    ESP_LOGI(TAG, "SoftAP provisioning live at http://192.168.4.1 (ESP SoftAP Provisioning app)");
    return ESP_OK;

fail:
    protocomm_delete(s_pc);
    s_pc = NULL;
    return e;
}

void prov_softap_prov_stop(void)
{
    if (!s_pc) return;
    protocomm_httpd_stop(s_pc);
    protocomm_delete(s_pc);
    s_pc = NULL;
    free(s_ver_json);
    s_ver_json = NULL;
}
