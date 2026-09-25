#include "api_ota.h"
#include "api_internal.h"
#include "net_http.h"
#include "net_wifi.h"
#include "nvs_keys.h"
#include "otarelease.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "api_ota";

/* The URL is short and bounded. A longer body is refused rather than truncated: a truncated
 * URL would be a DIFFERENT url, and pointing esp_https_ota at the wrong host is exactly the
 * failure this endpoint must not have. */
#define OTA_URL_MAX 512

esp_err_t api_ota_install_and_reboot(httpd_req_t *req, const char *url)
{
    /* Accept only HTTPS. This is a firmware-replacement endpoint, and the device is reached by
     * IP on a LAN — plain http would let anyone who can spoof that address install arbitrary
     * firmware. The certificate bundle is attached below, so the server is authenticated too,
     * not just the transport. Checked here rather than only in the caller because BOTH OTA
     * entry points must enforce it: the url handler validates its client-supplied body, and the
     * release handler's URL is assembled from a manifest — which is remote input too. */
    if (!url || strncmp(url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "refusing non-https OTA url");
        return api_send_err(req, "400 Bad Request", "url must be https");
    }

    ESP_LOGW(TAG, "OTA from %s", url);

    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* verify the server's chain */
        .timeout_ms = 20000,
        .keep_alive_enable = false,
        /* A GitHub release asset 302s to a CDN URL with a long signed query string, and
         * esp_https_ota re-issues the request against THAT url — measured 921 bytes for a release
         * asset, against a 512-byte default TX buffer, which fails with "HTTP_CLIENT: Out of
         * buffer" after a successful handshake. See the same note in net_http.c. */
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    /* The download is the LONGEST TLS connection this firmware makes, and it needs the same
     * contiguous room the handshake does — with the render layer held it cannot even start.
     * Held for the whole download and write, then given back before the response is sent. */
    api_fetch_pause();
    const esp_err_t e = esp_https_ota(&cfg);
    api_fetch_resume();

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(e));
        api_note_error("ota: update failed");
        return api_send_err(req, "500 Internal Server Error", "OTA failed");
    }

    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    ESP_LOGW(TAG, "OTA written to %s; rebooting into a probationary image",
             next ? next->label : "?");

    /* Deliberately NOT calling esp_ota_mark_app_valid_cancel_rollback() here. The new image
     * is on probation until the boot path has proved it can boot AND complete a refresh;
     * marking it valid now would accept a build that boots but cannot draw. See api_ota.h. */
    char body[224];
    snprintf(body, sizeof(body),
             "{\"status\":\"ota applied\",\"reboot\":true,"
             "\"partition\":\"%s\",\"rollback_unless_refresh\":true}",
             next ? next->label : "unknown");
    const esp_err_t sent = api_send_json(req, body, "200 OK");

    /* Give the response time to leave the socket before esp_restart() closes it. Without
     * this the client sees a connection reset and cannot tell a successful update from a
     * crash — which is precisely the case where it needs to know. */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return sent;      /* not reached */
}

esp_err_t api_ota_handler(httpd_req_t *req)
{
    /* Optional bearer auth (FR-31), and this is the endpoint it exists for.
     *
     * WHAT IS AT STAKE HERE: the body is a CLIENT-SUPPLIED URL and this installs
     * firmware from it. Ungated, anyone who can reach the device can replace its
     * firmware — a device takeover, not a data leak. The old comment on the scheme
     * check below acknowledged exactly this ("a firmware-replacement endpoint with no
     * authentication"); the token check is the control that closes it. */
    if (api_auth_gate(req)) return ESP_OK;

    char url[OTA_URL_MAX];
    const int n = api_read_body(req, url, sizeof(url));
    if (n < 0) return ESP_OK;      /* api_read_body already answered */

    /* Reject leading whitespace/control bytes FIRST. The scheme test below is a prefix
     * match, so a body of "\nhttps://evil" would otherwise be accepted as if the client had
     * sent the URL it claims — and the body is entirely client-controlled. */
    if (url[0] == '\0' || (unsigned char)url[0] <= ' ') {
        ESP_LOGW(TAG, "refusing OTA url with leading whitespace or empty");
        return api_send_err(req, "400 Bad Request", "url must begin with https://");
    }

    return api_ota_install_and_reboot(req, url);
}

int api_ota_in_probation(void)
{
    /* PENDING_VERIFY means this image booted under rollback protection and has not been
     * marked valid yet. Anything else — VALID, UNDEFINED (rollback disabled), or an error
     * reading the state — is treated as "not on probation", so a query failure cannot cause
     * a spurious validation. */
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) return 0;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return 0;
    return state == ESP_OTA_IMG_PENDING_VERIFY;
}

void api_ota_mark_valid_if_pending(void)
{
    if (!api_ota_in_probation()) return;

    const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    if (e == ESP_OK) {
        ESP_LOGI(TAG, "image validated; rollback cancelled");
    } else {
        /* Not fatal: the image keeps running, it simply stays on probation and will roll
         * back if it resets before a later call succeeds. Logged because a device that can
         * never be validated would roll back forever, and that is worth seeing. */
        ESP_LOGW(TAG, "could not mark valid: %s", esp_err_to_name(e));
    }
}

/* --------------------------------------------------- OTA from the GitHub release (FR-32) -- */

/* The stable "latest release" location. A GitHub release asset is served from this fixed path
 * (releases/latest/download/<name>), so the device never needs to know a tag: fetching it always
 * yields whatever is currently latest, and GitHub issues a redirect to the asset CDN that the
 * HTTP client follows. See the manifest note in lib/otarelease for why a flat manifest rather
 * than the GitHub API. */
#define OTA_RELEASE_BASE  "https://github.com/OETSolutions/e-ink-weather/releases/latest/download"

/* The manifest is a few hundred bytes; 1 KB covers it many times over and keeps this out of the
 * contiguous DRAM the render path needs. See ota_manifest_buf() for why it is shared. */
#define OTA_MANIFEST_MAX  1024

/* The response buffer, SHARED rather than one per caller.
 *
 * It has to be static, not stack: callers run on the httpd task or the boot worker, whose stacks
 * are shared with the deep TLS handshake path, and a 1 KB frame on top of that is exactly what
 * this project's stack budget exists to avoid. But 1 KB in .bss is DRAM the heap never gets, and
 * this part's render window needs ONE large contiguous free block — so one buffer, not three.
 *
 * Safe to share because the boot consumer runs before api_start(), and a single httpd serialises
 * its two handlers; the three callers are never live at once. ALWAYS pass OTA_MANIFEST_MAX as the
 * capacity — sizeof() on the returned pointer is the pointer's size, not the buffer's. */
static char s_manifest[OTA_MANIFEST_MAX];
static char *ota_manifest_buf(void) { return s_manifest; }

/* Bring the station up from the STORED credentials, the same way the refresh path does.
 *
 * WHY THIS IS NOT ALREADY DONE BY THE TIME THE REQUEST ARRIVES: on USB/mains the device stays
 * awake serving the API after a refresh has torn WiFi down (NFR-3), and on a device that has not
 * refreshed yet the radio was never up at all. So an OTA request arrives with no guarantee of a
 * route. Credentials come from NVS exactly as app_refresh.c reads them — never compiled in. */
static esp_err_t ota_ensure_wifi(void)
{
    if (net_wifi_connected()) return ESP_OK;

    char ssid[64] = {0};
    char pass[128] = {0};
    size_t slen = sizeof(ssid), plen = sizeof(pass);
    nvs_handle_t h;
    if (nvs_open(DEVENV_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_str(h, DEVENV_KEY_WIFI_SSID, ssid, &slen);
        nvs_get_str(h, DEVENV_KEY_WIFI_PASS, pass, &plen);
        nvs_close(h);
    }
    if (ssid[0] == '\0') return ESP_ERR_INVALID_STATE;   /* not provisioned */
    return net_wifi_connect(ssid, pass, 20000);
}

static void release_manifest_url(char *out, size_t cap)
{
    snprintf(out, cap, "%s/manifest.json", OTA_RELEASE_BASE);
}

/* Fetch and parse the latest release manifest. Returns ESP_OK and fills `out`, or an error with
 * `*err_msg` set to a short human string for the caller to return. Assumes WiFi is up. */
static esp_err_t fetch_release_manifest(otarelease_manifest_t *out, const char **err_msg)
{
    char murl[192];
    release_manifest_url(murl, sizeof(murl));

    char *manifest = ota_manifest_buf();
    /* THE LAYER MUST BE OUT OF THE WAY FIRST. This runs on the httpd task, and the render task
     * holds its 78,200-byte static layer resident between ticks (USB/mains). The TLS handshake
     * needs the one contiguous DRAM region large enough for that layer, so with it held the
     * handshake fails outright — measured on the bench: `mbedtls_ssl_setup returned -0x7F00`
     * (ALLOC_FAILED), every check answering 502 in ~0.12 s. The pair is a no-op when nothing is
     * registered, which is what the host tests run against. */
    api_fetch_pause();

    /* OTA_MANIFEST_MAX, NOT sizeof(manifest): manifest is a pointer, so sizeof is the pointer's
     * size (8) and the fetch would be capped at 8 bytes and always overflow. */
    const esp_err_t e = net_http_get_json(murl, NULL, manifest, OTA_MANIFEST_MAX);

    /* Resume BEFORE the early returns below: the layer lock is still held on the way out of a
     * paused fetch, so a return that skipped this would wedge every later render. */
    api_fetch_resume();

    if (e != ESP_OK) {
        ESP_LOGE(TAG, "manifest fetch failed: %s", esp_err_to_name(e));
        *err_msg = "could not fetch release manifest";
        return e;
    }
    if (otarelease_parse(manifest, out) != 0) {
        ESP_LOGE(TAG, "release manifest did not parse");
        *err_msg = "release manifest unparseable";
        return ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}

esp_err_t api_ota_update_handler(httpd_req_t *req)
{
    /* Same gate as POST /api/ota: this also installs firmware and therefore also takes over the
     * device. Read-only callers should use GET /api/ota/check, which is ungated. */
    if (api_auth_gate(req)) return ESP_OK;

    const esp_app_desc_t *desc = esp_app_get_description();
    const char *current = desc ? desc->version : "0.0.0";

    /* Fetching over TLS needs working DNS and a route, and on a cold boot the WiFi stack may
     * not be up yet. This is the same path the refresh takes before its first fetch. */
    const esp_err_t w = ota_ensure_wifi();
    if (w != ESP_OK) {
        ESP_LOGE(TAG, "no WiFi for update check: %s", esp_err_to_name(w));
        return api_send_err(req, "503 Service Unavailable", "no WiFi");
    }

    otarelease_manifest_t m;
    const char *emsg = NULL;
    if (fetch_release_manifest(&m, &emsg) != ESP_OK) {
        api_note_error("ota: release manifest failed");
        return api_send_err(req, "502 Bad Gateway", emsg);
    }

    /* The comparison is numeric (see lib/otarelease): as strings, "0.10.0" < "0.9.0", so a
     * string compare would hide every double-digit release. */
    if (!otarelease_is_newer(current, m.version)) {
        char body[224];
        snprintf(body, sizeof(body),
                 "{\"status\":\"up to date\",\"current\":\"%s\",\"latest\":\"%s\"}",
                 current, m.version);
        return api_send_json(req, body, "200 OK");
    }

    char url[256];
    if (otarelease_firmware_url(OTA_RELEASE_BASE, &m, url, sizeof(url)) != 0) {
        return api_send_err(req, "502 Bad Gateway", "bad firmware url in release manifest");
    }
    ESP_LOGW(TAG, "updating %s -> %s from %s", current, m.version, url);
    return api_ota_install_and_reboot(req, url);
}

esp_err_t api_ota_check_handler(httpd_req_t *req)
{
    /* UNGATED on purpose: this reads and reports, it changes nothing and installs nothing, so a
     * token would only make the "is there an update?" question unaskable. The endpoint that
     * actually installs (POST /api/ota/update) is gated. */
    const esp_app_desc_t *desc = esp_app_get_description();
    const char *current = desc ? desc->version : "0.0.0";

    const esp_err_t w = ota_ensure_wifi();
    if (w != ESP_OK) return api_send_err(req, "503 Service Unavailable", "no WiFi");

    otarelease_manifest_t m;
    const char *emsg = NULL;
    if (fetch_release_manifest(&m, &emsg) != ESP_OK) {
        return api_send_err(req, "502 Bad Gateway", emsg);
    }

    const int newer = otarelease_is_newer(current, m.version);
    char body[256];
    snprintf(body, sizeof(body),
             "{\"current\":\"%s\",\"latest\":\"%s\",\"update_available\":%s}",
             current, m.version, newer ? "true" : "false");
    return api_send_json(req, body, "200 OK");
}

/* ---- boot-time auto-update (FR-32) ----
 *
 * Runs from the boot worker task (not httpd), so it is a plain function with no req. Returns 1
 * when it COMMITTED to an update and the device is about to reboot; 0 to continue normally.
 *
 * SAFETY: it only ever installs a STRICTLY NEWER release, and rollback protection (FR-32) is the
 * backstop — a build that installs here must still boot and complete a refresh before it is
 * validated, or the bootloader reverts to the previous image. It is also gated by the user's
 * opt-in (`firmwareAutoUpdate`), which defaults to OFF, so an unattended device is not updated
 * behind the owner's back unless they asked. */
int api_ota_auto_update_if_enabled(int enabled)
{
    /* Opt-in only. A 0 (the default for an absent or non-true flag) means "do not touch my
     * firmware". */
    if (!enabled) return 0;

    const esp_app_desc_t *desc = esp_app_get_description();
    const char *current = desc ? desc->version : "0.0.0";

    const esp_err_t w = ota_ensure_wifi();
    if (w != ESP_OK) {
        ESP_LOGW(TAG, "auto-update: no WiFi (%s)", esp_err_to_name(w));
        return 0;
    }

    otarelease_manifest_t m;
    const char *emsg = NULL;
    if (fetch_release_manifest(&m, &emsg) != ESP_OK) {
        ESP_LOGW(TAG, "auto-update: %s", emsg);
        return 0;
    }
    if (!otarelease_is_newer(current, m.version)) {
        ESP_LOGI(TAG, "auto-update: %s is current (latest %s)", current, m.version);
        return 0;
    }

    char url[256];
    if (otarelease_firmware_url(OTA_RELEASE_BASE, &m, url, sizeof(url)) != 0) return 0;

    ESP_LOGW(TAG, "auto-update: %s -> %s; installing before the first render", current, m.version);
    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 20000,
        .keep_alive_enable = false,
        /* Same redirect-buffer requirement as the handler path above. */
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };
    esp_https_ota_config_t oc = { .http_config = &http };
    /* Same reason as the handler path: the layer must be out of the way for the handshake and
     * the download. On this boot path the layer is normally already released (see the boot
     * ordering), so this is typically a no-op — it is here so the pair is not silently missing
     * if that ordering ever changes. */
    api_fetch_pause();
    const esp_err_t e = esp_https_ota(&oc);
    api_fetch_resume();

    if (e != ESP_OK) {
        /* DO NOT reboot on failure: the last good image is already good, and a failed update must
         * leave the device running rather than in a boot loop. Report and continue this boot. */
        ESP_LOGE(TAG, "auto-update failed: %s; continuing on the current image", esp_err_to_name(e));
        api_note_error("ota: auto-update failed");
        return 0;
    }

    ESP_LOGW(TAG, "auto-update installed; rebooting into a probationary image");
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_restart();
    return 1;   /* not reached */
}

/* --------------------------------------------------- LAN firmware upload (local dev flash) -- */

/* The receive chunk. 4 KB: large enough that a ~1.8 MB image is ~450 reads (not thousands of
 * tiny ones), small enough to be a stack-safe, non-fragmenting transient — it is taken from the
 * heap, not .bss, and released before the reboot. */
#define FW_CHUNK_BYTES 4096

esp_err_t api_firmware_handler(httpd_req_t *req)
{
    /* GATED (FR-31): this replaces the firmware outright, so it is the same capability as
     * POST /api/ota and needs the same token check. */
    if (api_auth_gate(req)) return ESP_OK;

    /* Content-Length is REQUIRED. Without it the body length is unknown and a truncated upload
     * could be written as a complete-looking image; the OTA partition would then hold a bad image
     * that only fails at the next boot. */
    const size_t total = req->content_len;
    if (total == 0) {
        return api_send_err(req, "411 Length Required", "Content-Length required");
    }

    /* Bound BEFORE allocating or erasing anything: the target slot is the app partition size, and
     * an image that cannot fit must be refused rather than partially written. */
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        api_note_error("firmware: no OTA partition");
        return api_send_err(req, "500 Internal Server Error", "no OTA partition");
    }
    if (total > part->size) {
        ESP_LOGE(TAG, "upload of %u bytes exceeds the %u-byte OTA slot",
                 (unsigned)total, (unsigned)part->size);
        return api_send_err(req, "413 Payload Too Large", "image larger than the OTA slot");
    }

    /* THE RENDER LAYER MUST BE OUT OF THE WAY for the whole write, for the same reason the OTA
     * fetch needs it: this runs on the httpd task while the render task holds the one contiguous
     * DRAM region, and the OTA buffer below has to come from somewhere. Held for the write, given
     * back before the reboot. */
    esp_ota_handle_t handle = 0;
    esp_err_t e = esp_ota_begin(part, total, &handle);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(e));
        api_note_error("firmware: ota begin failed");
        return api_send_err(req, "500 Internal Server Error", "OTA begin failed");
    }

    char *chunk = malloc(FW_CHUNK_BYTES);
    if (!chunk) {
        esp_ota_abort(handle);
        return api_send_err(req, "500 Internal Server Error", "oom");
    }

    api_fetch_pause();
    size_t received = 0;
    esp_err_t werr = ESP_OK;
    while (received < total) {
        const int n = httpd_req_recv(req, chunk, FW_CHUNK_BYTES);
        if (n <= 0) {
            /* A timeout or a dropped connection mid-upload. Abort and DO NOT reboot: nothing has
             * been swapped in, so the running image is untouched and the device stays up. */
            ESP_LOGE(TAG, "upload receive failed after %u of %u bytes (%d)",
                     (unsigned)received, (unsigned)total, n);
            werr = ESP_FAIL;
            break;
        }
        if ((werr = esp_ota_write(handle, chunk, (size_t)n)) != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(werr));
            break;
        }
        received += (size_t)n;
    }
    api_fetch_resume();
    free(chunk);

    if (werr != ESP_OK) {
        esp_ota_abort(handle);
        api_note_error("firmware: upload failed");
        return api_send_err(req, "500 Internal Server Error", "firmware write failed");
    }

    /* A short read that did not error (the socket closed cleanly early) would leave a truncated
     * image; refuse it here rather than let esp_ota_end accept a short write. */
    if (received != total) {
        esp_ota_abort(handle);
        ESP_LOGE(TAG, "upload truncated: %u of %u bytes", (unsigned)received, (unsigned)total);
        return api_send_err(req, "400 Bad Request", "upload truncated");
    }

    e = esp_ota_end(handle);
    if (e != ESP_OK) {
        /* The image was written but is not a valid app (wrong magic, bad checksum). The running
         * firmware is still the old one because set_boot_partition has not run. */
        ESP_LOGE(TAG, "esp_ota_end rejected the image: %s", esp_err_to_name(e));
        api_note_error("firmware: invalid image");
        return api_send_err(req, "400 Bad Request", "not a valid firmware image");
    }
    if ((e = esp_ota_set_boot_partition(part)) != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(e));
        return api_send_err(req, "500 Internal Server Error", "could not select the new image");
    }

    ESP_LOGW(TAG, "firmware uploaded (%u bytes) to %s; rebooting into a probationary image",
             (unsigned)received, part->label);

    /* NOT marked valid here — the boot path does that after a successful refresh, exactly as the
     * network OTA paths leave it (see api_ota_mark_valid_if_pending). */
    char body[192];
    snprintf(body, sizeof(body),
             "{\"status\":\"firmware applied\",\"reboot\":true,"
             "\"partition\":\"%s\",\"bytes\":%u,\"rollback_unless_refresh\":true}",
             part->label, (unsigned)received);
    const esp_err_t sent = api_send_json(req, body, "200 OK");

    /* Let the response leave the socket before esp_restart() closes it, so the client can tell a
     * successful upload from a crash. Same reason as api_ota_install_and_reboot(). */
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return sent;      /* not reached */
}
