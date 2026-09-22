#pragma once

/* The ordered wake path (FR-28, FR-29, FR-8, FR-9).
 *
 * The ORDER is a requirement, not a style choice, and it is here rather than in app_main so
 * it can be read in one place. Two steps are ordered for a hardware reason:
 *
 *   1. NVS, then the config — everything downstream needs the config.
 *   2. Battery sense with the WiFi radio OFF. The VBAT divider lands on GPIO26 =
 *      ADC2_CH9, and ADC2 is unusable while WiFi is active (HW-3). Read it after the
 *      network comes up and you get a plausible-looking wrong voltage, which then drives
 *      the wrong power-source decision and the wrong sleep behaviour.
 *   3. Panel: show the last good image BEFORE any network work, so a network failure never
 *      leaves the panel blank or stale (FR-29).
 *   4-6. Network, fetch, render.
 *   7. Mark the image good, if it is on probation (FR-32).
 *   8. Provisioning, if never configured (FR-30) — and this runs LAST, after the framebuffers
 *      have been released, because provisioning is the most memory-hungry step and there is
 *      no PSRAM on this part (NFR-2). See the comment at the call site.
 *   9. Sleep, if on battery.
 */

/* Run one wake: read state, draw, fetch, refresh, then sleep if on battery. Does not
 * return when it decides to deep-sleep. */
void app_boot_run(void);

/* Serve the API until reset, performing a refresh whenever one is requested.
 *
 * Called instead of returning on USB power. Needed because the boot path refreshes exactly
 * once: without this loop, POST /api/refresh and a completed bitmap upload would both set a
 * "refresh now" flag that nothing ever reads, so the panel would keep showing the previous
 * image while the API reported success. Battery operation does not use this — there the
 * wake/sleep cycle IS the refresh cadence, and staying awake would flatten the pack.
 *
 * Never returns. */
void app_serve_loop(void);

/* Bench-only: store WiFi credentials in NVS if nothing is there yet.
 *
 * WHY THIS EXISTS: the production path is provisioning (FR-30) — a captive portal and BLE —
 * and the device deliberately does NOT connect until someone configures it. That is correct
 * for a shipped device and useless for a bench verification, where the point is to reach the
 * API without a phone. This seeds NVS from the gitignored secrets_build.h so the API has a
 * network to be reached over.
 *
 * It NEVER overwrites existing credentials, so it cannot clobber a provisioned device, and
 * it is called only when secrets_build.h was generated — a normal build has no such header
 * and the call is not compiled in. Nothing here reaches a tracked file (FR-30). */
void app_seed_wifi(const char *ssid, const char *pass);

/* Bench-only: store the OpenWeatherMap API key in NVS if none is stored.
 *
 * Same contract as app_seed_wifi() and for the same reasons: the production path is
 * provisioning (FR-30), a bench build needs the key without a phone, and it must NEVER
 * overwrite an existing key or it would replace a real owner's whenever this image is flashed.
 * Only compiled in when secrets_build.h exists. */
void app_seed_owm_key(const char *key);

/* Bench-only: store the Home Assistant URL and token in NVS if none is stored.
 *
 * A URL without a token (or vice versa) is stored as neither — HA needs both, and half a
 * credential pair would make fetch_ha() believe it could talk to HA and then fail at the
 * request. Same never-overwrite rule as the other seeders. Only compiled in when
 * secrets_build.h exists, and only writes when code/.env supplies BOTH values. */
void app_seed_ha(const char *url, const char *token);
