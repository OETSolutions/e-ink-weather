/* A second provisioning transport over the setup portal's HTTP server (FR-30). See the .c for
 * why this exists (the SoftAP app's list works where the BLE app's does not) and why it is
 * deliberately unsecured. */

#pragma once

#include "esp_err.h"

/* Start the HTTP provisioning transport on an already-running HTTP server. `httpd_handle` is
 * the portal's `httpd_handle_t`. Idempotent; safe to call when already running. */
esp_err_t prov_softap_prov_start(void *httpd_handle);

/* Tear it down. Safe when not running. */
void prov_softap_prov_stop(void);
