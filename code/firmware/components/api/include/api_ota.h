#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

/* OTA over HTTPS with rollback (FR-32).
 *
 * Kept separate from the rest of the API because its failure mode is unlike every other
 * endpoint's: a bad OTA does not produce a bad response, it produces a device that never
 * comes back. The two halves of that — refusing to accept an unverified transport, and
 * refusing to accept the new image as good before it has proven itself — are both here. */

/* The POST /api/ota handler. Registered by api_start(). */
esp_err_t api_ota_handler(httpd_req_t *req);

/* Tell the bootloader this firmware is good, cancelling the rollback.
 *
 * MUST be called from the boot path and ONLY after the image has proved it can boot AND
 * complete a refresh. CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is set (sdkconfig.defaults), so
 * a new image runs on probation: if it crashes or reboots before this is called, the
 * bootloader falls back to the previous partition automatically. Calling this from the OTA
 * handler instead — the obvious-looking place — would defeat the entire mechanism, because
 * it would accept a build that boots but cannot draw.
 *
 * Calling it when no rollback is pending is not an error (the device has simply already
 * been marked valid this boot), so its return value is logged rather than propagated. */
void api_ota_mark_valid_if_pending(void);

/* Is this boot running a probationary (not-yet-validated) image? Useful for the boot path's
 * logging, and for /api/status's "did the last OTA roll back" question. */
int api_ota_in_probation(void);
