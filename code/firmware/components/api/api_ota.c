#include "api_ota.h"
#include "api_internal.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "api_ota";

/* The URL is short and bounded. A longer body is refused rather than truncated: a truncated
 * URL would be a DIFFERENT url, and pointing esp_https_ota at the wrong host is exactly the
 * failure this endpoint must not have. */
#define OTA_URL_MAX 512

esp_err_t api_ota_handler(httpd_req_t *req)
{
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

    /* Accept only HTTPS. This is a firmware-replacement endpoint with no authentication,
     * and the device is reached by IP on a LAN — plain http would let anyone who can spoof
     * that address install arbitrary firmware. The certificate bundle is attached below, so
     * the server is authenticated too, not just the transport. */
    if (strncmp(url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "refusing non-https OTA url");
        return api_send_err(req, "400 Bad Request", "url must be https");
    }

    ESP_LOGW(TAG, "OTA from %s", url);

    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* verify the server's chain */
        .timeout_ms = 20000,
        .keep_alive_enable = false,
    };
    esp_https_ota_config_t cfg = { .http_config = &http };

    const esp_err_t e = esp_https_ota(&cfg);
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
