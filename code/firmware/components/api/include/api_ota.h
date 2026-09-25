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

/* OTA from the project's GitHub RELEASES (FR-32), as opposed to a URL the caller supplies.
 *
 * GET  /api/ota/check   — reports {"current","latest","update_available"}. UNGATED: it only
 *                         reads and changes nothing, so the bearer token would make the "is
 *                         there an update?" question unaskable for no security gain.
 * POST /api/ota/update  — fetches the latest release's manifest, and if it names a version
 *                         newer than this image, installs it and reboots. GATED like
 *                         POST /api/ota, because it takes over the device.
 *
 * The manifest is a flat JSON asset at the release's stable
 * releases/latest/download/manifest.json path, so the device needs no tag and no GitHub API
 * token. Parsing and version comparison live in lib/otarelease and are host-tested. */
esp_err_t api_ota_check_handler(httpd_req_t *req);
esp_err_t api_ota_update_handler(httpd_req_t *req);

/* POST /api/firmware — install a firmware image from the REQUEST BODY.
 *
 * WHY THIS EXISTS (the local dev flash path): the other two installers both need a reachable HTTPS
 * source — POST /api/ota takes a URL the device dials, and POST /api/ota/update pulls from GitHub.
 * Neither is usable when the image is a freshly-built firmware.bin on the developer's laptop and
 * the board is on the bench with no USB: standing up a CA-trusted HTTPS server just to flash is
 * disproportionate. This endpoint takes the bytes directly over the LAN, so `curl --data-binary
 * @firmware.bin http://<device>/api/firmware` is the whole flow.
 *
 * IT IS GATED LIKE THE OTHER INSTALLERS (FR-31), because it is the SAME capability — arbitrary
 * firmware replacement, i.e. device takeover — and an ungated upload endpoint would be a worse hole
 * than the URL one, since it needs no external host at all.
 *
 * The body is streamed straight to the OTA partition in chunks; it is never buffered whole (the
 * image is ~1.8 MB and this part has no PSRAM). Content-Length is REQUIRED and bounded by the OTA
 * slot size, so a truncated or oversized upload fails before anything is written.
 *
 * The new image is left on PROBATION exactly as the other paths leave it: the boot path proves it
 * can boot and refresh before marking it valid (see api_ota_mark_valid_if_pending). */
esp_err_t api_firmware_handler(httpd_req_t *req);

/* Boot-time auto-update. Called from the boot worker BEFORE the first render, and only when the
 * user enabled it. Returns 1 if it installed an update and is about to reboot (never returns in
 * that case); 0 to continue the normal boot. `enabled` is the parsed `firmware_auto_update`
 * opt-in from layout_config_t — 0 is an immediate no-op, so a caller need not gate the call. */
int api_ota_auto_update_if_enabled(int enabled);

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
