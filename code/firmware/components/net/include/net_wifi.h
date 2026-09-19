#pragma once
#include "esp_err.h"

/* WiFi station with BOUNDED reconnect (NFR-1). The device wakes, connects, fetches, and
 * sleeps; it has no console and no operator, so "retry forever" is indistinguishable from
 * a hang — and it would hold the radio on and flatten the battery. Every wait here has a
 * deadline, and failure returns rather than looping.
 *
 * The radio is fully torn down in net_wifi_disconnect() before the ADC2 battery read
 * (HW-3: ADC2 is unusable while WiFi is active) and before deep sleep (NFR-3). */

esp_err_t net_wifi_connect(const char *ssid, const char *pass, int timeout_ms);
void net_wifi_disconnect(void);

/* RSSI in dBm, or 0 if not connected. */
int net_wifi_rssi(void);

/* One visible access point. */
typedef struct {
    char ssid[33];
    int  rssi;
    int  channel;
    int  authmode;      /* wifi_auth_mode_t */
} net_wifi_ap_t;

/* Bring the driver up WITHOUT connecting and list visible APs. Returns the number found,
 * or negative on error. Used by the provisioning flow (FR-30) to offer a network list, and
 * it is the only way to tell "wrong password" from "wrong SSID" — both surface as an
 * auth failure. Call net_wifi_disconnect() afterwards. */
int net_wifi_scan(net_wifi_ap_t *out, int max);
