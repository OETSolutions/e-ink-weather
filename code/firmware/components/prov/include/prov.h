#pragma once

#include "esp_err.h"

/* First-boot provisioning (FR-30). The device has no console, no keyboard, and no
 * compiled-in credentials, so it has to be told how to join the network by someone
 * standing next to it.
 *
 * BOTH TRANSPORTS RUN AT ONCE, which is what the requirement asks for:
 *   - BLE, via wifi_prov_mgr, for the official ESP BLE Provisioning app (FR-30.2);
 *   - a SoftAP with a captive portal, for anything with a browser (FR-30.1).
 *
 * WHY THE MANAGER DRIVES BLE AND NOT THE AP: wifi_prov_mgr starts exactly ONE scheme
 * (manager.c: `scheme->prov_start(pc, ...)`, and IDF's own Kconfig says the component
 * "offers both, SoftAP and BLE transports. Choose any one."). A second transport cannot be
 * bolted onto the manager's protocomm instance either: `struct protocomm` holds a SINGLE
 * `add_endpoint` pointer, and each transport overwrites it (protocomm_httpd.c and
 * protocomm_nimble.c both assign `pc->add_endpoint`), so starting a second transport
 * hijacks the first and leaves it with no endpoints. The manager also exposes no getter for
 * its instance, and its endpoint handlers live in a private header.
 *
 * So BLE is the manager's scheme and the AP + config page are ours, started alongside it.
 * Both paths converge on wifi_prov_mgr_configure_sta(), the API IDF documents for
 * "credentials supplied via a different mode than protocomm". That gives one connect-and-
 * verify path, one place the failure is detected, and one exit from provisioning. */

/* Run provisioning if the device has no WiFi credentials.
 *
 * IMPORTANT — this is NOT a fallback for a failed connection. It triggers ONLY when NVS has
 * no SSID at all. A device that IS configured but cannot reach its AP (moved house, AP
 * renamed, router down) must NOT raise an access point: silently turning a working device
 * into an open AP because the network was briefly down is a hostile surprise, and it would
 * leave the real problem — the wrong credentials — undiscovered. Those cases surface as
 * errors in /api/status and are fixed by the user re-running provisioning deliberately.
 *
 * ON SUCCESS THIS DOES NOT RETURN: it restarts the device. That is not laziness, it is the
 * only clean handover. Provisioning leaves the WiFi driver initialised and the station
 * connected, and the normal boot path calls esp_wifi_init() again — which fails with
 * ESP_ERR_WIFI_INIT_STATE, so the device would come up "provisioned" and unable to use the
 * network. Restarting also guarantees the BLE stack, the setup AP and the DNS server are
 * genuinely gone rather than mostly torn down, and it is what the plan's verification
 * expects ("the device reboots into normal operation and the AP is gone").
 *
 * Returns only on failure to start provisioning, or ESP_OK if the device was already
 * configured (in which case nothing was touched and the caller carries on as normal). */
esp_err_t prov_run_if_unconfigured(void);

/* 1 if NVS holds a non-empty WiFi SSID. */
int prov_is_configured(void);

/* The AP name / BLE device name this unit will advertise, e.g. "EINK-WEATHER-2045AC".
 *
 * Exposed so the provisioning SCREEN can print it. Deriving it a second time at the display
 * site would be a bug waiting to happen — the name depends on the MAC and would drift if the
 * two derivations ever disagreed, leaving a screen that tells the user to join a network the
 * device is not offering. Callers pass a buffer of at least 32 bytes. */
void prov_service_name(char *out, size_t out_max);

/* The BLE proof of possession, for the provisioning screen. See provscreen.h for why printing
 * it is not the security hole it looks like. */
#define PROV_POP_STRING "eink1234"

/* Wipe the stored WiFi credentials and restart into provisioning. Exposed for the API so a
 * user can deliberately re-provision a device that has been moved to another network, and
 * so a device stuck with a wrong password is recoverable without a full erase. */
esp_err_t prov_forget(void);

/* ---- the pieces prov_run_if_unconfigured() assembles, exposed for testing and reuse ---- */

/* Start the setup AP, its config page and the DNS catch-all that makes the page appear
 * automatically. `ap_ssid` is the SSID to broadcast.
 *
 * MUST BE CALLED AFTER wifi_prov_mgr_start_provisioning(): the manager forces
 * WIFI_MODE_STA while starting up ("necessary for scanning to work"), so an AP raised
 * before it is silently switched off. Idempotent, so the failure path can call it again to
 * bring the portal back. */
esp_err_t prov_ap_start(const char *ap_ssid);

/* Stop the setup AP, its HTTP server and the DNS catch-all. Safe when not running. */
void prov_ap_stop(void);

/* 1 while the setup AP is up. */
int prov_ap_is_running(void);

/* Publish credentials into our own NVS namespace.
 *
 * WHY A SECOND COPY IS NEEDED: the manager persists WiFi credentials through the driver
 * (`esp_wifi_set_storage(WIFI_STORAGE_FLASH)`), which writes them to the WiFi driver's own
 * namespace — nvs.net80211, confirmed by the string table in libnet80211.a — and NOT to
 * ours. app_refresh.c reads our namespace, so without this mirror a device would be
 * successfully provisioned by the manager and still look unconfigured to everything else,
 * and would re-enter provisioning on every boot. */
esp_err_t prov_store_credentials(const char *ssid, const char *pass);

/* Store the non-WiFi config fields the portal collects: OWM API key, Home Assistant URL and
 * token, and the location. NULL or empty strings leave that field unchanged, so the page can
 * submit only what the user filled in. Returns 0 on success. */
int prov_store_extra_config(const char *owm_key, const char *ha_url,
                            const char *ha_token, double lat, double lon,
                            int have_location);
