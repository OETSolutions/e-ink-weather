#pragma once

#include "power.h"
#include "esp_err.h"

/* The render-and-push path (FR-1..FR-4b, FR-10..FR-13).
 *
 * Separated from app_boot.c because the boot path decides WHEN to refresh and this decides
 * WHAT goes on the glass. The boot path's ordering is a hardware constraint; this file's
 * job is the panel protocol, and mixing the two makes both harder to check. */

/* Claim the two framebuffers now, and keep them for the life of the process.
 *
 * WHY THIS MUST RUN BEFORE ANY TASK IS CREATED: each framebuffer is ONE contiguous 76.4 KiB
 * allocation, and on this part (320 KB RAM, no PSRAM — NFR-2) the largest free block is only
 * about 108 KiB. There is exactly one region big enough for the second framebuffer, and it is
 * an 80 KiB region that a single 16 KiB task stack will otherwise split in half. Once that
 * happens the allocation can never succeed again: the largest remaining block is ~62 KiB and
 * the failure is permanent for that boot.
 *
 * This was found the hard way — the app worker task's stack landing in that region is why
 * "cannot allocate 2 x 78200 bytes" appeared with 199 KB of free heap. The total was never
 * the problem; the largest contiguous block was. Reserving the framebuffers first makes the
 * outcome independent of what else the boot path allocates.
 *
 * Idempotent: returns ESP_OK immediately if the framebuffers are already held. */
esp_err_t app_fbs_reserve(void);

/* Give the framebuffers back to the heap. The image stays on the glass — the panel is
 * bistable and is put to sleep after every push — so nothing is lost visually.
 *
 * WHY THE BOOT PATH NEEDS THIS: provisioning runs after the last-good image has been pushed
 * and before any network stack exists, and it is the single most memory-hungry thing this
 * firmware does — NimBLE's host and controller plus a SoftAP plus the WiFi driver. Holding
 * 152.7 KiB of framebuffers across it starves esp_wifi_init(), which fails with
 * ESP_ERR_NO_MEM ("Expected to init 10 rx buffer, actual is 7"). Provisioning ends in
 * esp_restart() on success, so the framebuffers are rebuilt on the next boot anyway.
 *
 * Safe to call when nothing is held. The next render re-allocates through ensure_fbs().
 *
 * NOT for the normal refresh path: releasing between refreshes would discard the previous
 * frame, and a partial refresh needs it (see the two-framebuffer note in app_refresh.c). */
void app_fbs_release(void);

/* Compose the stored static layer with the last known values and push it, without touching
 * the network.
 *
 * This is the FR-29 requirement: after a power cut or a failed fetch the panel must show
 * the last good image rather than going blank. If no static layer has ever been uploaded
 * the built-in default layout is used, so a fresh device shows something meaningful.
 *
 * Returns ESP_OK if the panel was updated, or an error if it could not be (no image, or a
 * BUSY timeout). A failure is NOT fatal — the device still comes up and stays reachable. */
esp_err_t app_render_last_good(void);

/* One full refresh cycle: connect, fetch the current readings, render, push, then record
 * the result. Called from the boot path after the panel has shown its last good image.
 *
 * Chooses full vs partial refresh via refresh_decide() — the tested policy — using the
 * partial budget from the config and the time since the last full refresh. A partial needs
 * the frame currently on the glass, so the previous frame is kept in the second framebuffer
 * (partial takes TWO framebuffers; see epd.h).
 *
 * Never returns an error that stops the boot: a failed fetch leaves the last good image up
 * and is recorded for /api/status (FR-33). */
void app_refresh_tick(power_source_t source);
