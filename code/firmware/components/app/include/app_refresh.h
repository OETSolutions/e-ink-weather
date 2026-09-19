#pragma once

#include "power.h"
#include "esp_err.h"

/* The render-and-push path (FR-1..FR-4b, FR-10..FR-13).
 *
 * Separated from app_boot.c because the boot path decides WHEN to refresh and this decides
 * WHAT goes on the glass. The boot path's ordering is a hardware constraint; this file's
 * job is the panel protocol, and mixing the two makes both harder to check. */

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
