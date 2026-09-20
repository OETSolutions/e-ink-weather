#pragma once

#include "esp_http_server.h"

/* Serve the built web app from device flash (FR-18, FR-19).
 *
 * The user chose BOTH delivery modes: `npm run dev` locally for fast editing, and the built app
 * embedded in the device so it serves its own UI with no install and no dev server. This is the
 * second one.
 *
 * Register this AFTER the API handlers. esp_http_server matches the most recently registered
 * handler first, so a catch-all registered before them would shadow the API — the UI would load
 * and then every request it made would return the app shell, which looks like a client bug
 * rather than a routing one. The handler also refuses the API prefix explicitly as a
 * second guard.
 *
 * Returns ESP_OK once the handler is registered. */
esp_err_t webui_mount(httpd_handle_t server);
