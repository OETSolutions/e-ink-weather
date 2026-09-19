#include "net_wifi.h"
#include "net_http.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Generated per-machine by tools/gen_secrets_header.py from code/.env; the header is
 * gitignored (FR-30). If it is absent the verification build cannot run, which is why the
 * #error below is deliberate rather than a silent no-network build. */
#if __has_include("secrets_build.h")
#include "secrets_build.h"
#else
#error "run tools/gen_secrets_header.py first (needs code/.env with WIFI_SSID and OWM_API_KEY)"
#endif

static const char *TAG = "net_verify";

/* Task 11 on-hardware verification (plan Step 5). The pass/fail criteria are:
 *   - HTTP status 200 for every request
 *   - NO "Stack canary watchpoint triggered" and no reboot
 *   - free heap stable across 20 consecutive requests
 * The stack watermark is the number that justifies NET_TLS_TASK_STACK.
 *
 * Credentials are read from the build, not committed: the values are injected by
 * tools/gen_secrets_header.py into an ignored header at build time (FR-30). */

static char s_resp[8192];

static int one_request(int i, const char *url)
{
    size_t before = esp_get_free_heap_size();
    esp_err_t e = net_http_get_json(url, NULL, s_resp, sizeof(s_resp));
    size_t after = esp_get_free_heap_size();
    ESP_LOGI(TAG, "req %2d: %s | %u bytes | heap %u -> %u (%+d) | tls hwm %u B",
             i, esp_err_to_name(e), (unsigned)strlen(s_resp),
             (unsigned)before, (unsigned)after, (int)after - (int)before,
             net_http_stack_hwm());
    if (e != ESP_OK) return -1;
    /* The response must actually be JSON, not an error page or an empty body — a 200 with
     * an HTML captive-portal page is the classic false pass. */
    if (s_resp[0] != '{' && s_resp[0] != '[') {
        ESP_LOGE(TAG, "body is not JSON: %.60s", s_resp);
        return -1;
    }
    return 0;
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== Task 11 network verification ===");

#ifdef NET_VERIFY_SSID
    esp_err_t e = net_wifi_connect(NET_VERIFY_SSID, NET_VERIFY_PASS, 20000);
#else
    esp_err_t e = ESP_ERR_INVALID_ARG;
#endif
    ESP_LOGI(TAG, "net_wifi_connect: %s", esp_err_to_name(e));
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "cannot verify without a network; stopping");
        return;
    }

#ifdef NET_VERIFY_OWM_URL
    const char *url = NET_VERIFY_OWM_URL;
#else
    const char *url = NULL;
#endif
    if (!url) { ESP_LOGE(TAG, "no URL configured"); net_wifi_disconnect(); return; }

    size_t heap_at_start = esp_get_free_heap_size();
    int failures = 0;
    for (int i = 1; i <= 20; i++) {
        if (one_request(i, url) != 0) failures++;
    }
    size_t heap_at_end = esp_get_free_heap_size();

    ESP_LOGI(TAG, "=== result ===");
    ESP_LOGI(TAG, "failures: %d/20 (expect 0)", failures);
    ESP_LOGI(TAG, "heap start %u end %u (expect no monotonic decline)",
             (unsigned)heap_at_start, (unsigned)heap_at_end);
    ESP_LOGI(TAG, "TLS stack headroom: %u B of %u (lower = closer to the crash class)",
             net_http_stack_hwm(), (unsigned)NET_TLS_TASK_STACK);

    /* Tear the radio down BEFORE any ADC2 use — this is the HW-3 ordering, and it is also
     * the last chance to prove deinit does not panic. */
    net_wifi_disconnect();
    ESP_LOGI(TAG, "radio down, free heap %u", (unsigned)esp_get_free_heap_size());

    /* Prove a second connect works in the same power cycle (the idempotent-init path). */
    e = net_wifi_connect(NET_VERIFY_SSID, NET_VERIFY_PASS, 20000);
    ESP_LOGI(TAG, "reconnect after deinit: %s", esp_err_to_name(e));
    if (e == ESP_OK) {
        one_request(21, url);
        net_wifi_disconnect();
    }
    ESP_LOGI(TAG, "=== done ===");
}
