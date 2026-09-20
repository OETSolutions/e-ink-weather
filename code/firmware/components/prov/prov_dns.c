/* A DNS server that answers every A query with the setup AP's own address.
 *
 * WHY THIS IS NEEDED FOR "CAPTIVE PORTAL" TO MEAN ANYTHING (FR-30.1): raising an AP and
 * serving a page at http://192.168.4.1 is just an AP. A captive portal is what makes the
 * phone notice: the OS probes a known URL, and if DNS lies to it — answering every name with
 * the AP's address — the probe gets our page instead of the expected response and the OS
 * declares the network captive, opening the page by itself. Without this the user has to be
 * told the IP, which defeats the point.
 *
 * The REPLY CONSTRUCTION is not here — it lives in lib/provdns, where it is host-tested. Two
 * wire-format bugs in it (a missing byte swap on the flags field, and copying the client's
 * EDNS OPT record into the middle of the reply) shipped because there was no test for it and
 * a hand-rolled probe happened to miss both. See lib/provdns/include/provdns.h.
 *
 * What remains here is the part that genuinely needs a radio: resolving the AP's address and
 * owning the socket and task lifetimes. */

#include "prov.h"
#include "provdns.h"

#include <string.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "prov_dns";

#define DNS_PORT   53
#define DNS_MAX_LEN 256

static TaskHandle_t s_task;
static volatile int s_running;

static void dns_task(void *arg)
{
    (void)arg;
    char rx[DNS_MAX_LEN];
    char reply[DNS_MAX_LEN];
    uint32_t ap_ip = 0;

    /* Resolve the AP's address once. It cannot change while the portal is up, and looking it
     * up per query would be a netif call in the packet path for no reason. */
    {
        esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
        esp_netif_ip_info_t info;
        if (ap && esp_netif_get_ip_info(ap, &info) == ESP_OK) ap_ip = info.ip.addr;
    }
    if (ap_ip == 0) {
        ESP_LOGE(TAG, "no AP address; captive portal will not redirect");
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(DNS_PORT),
    };

    const int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "socket failed: errno %d", errno);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "bind to port %d failed: errno %d", DNS_PORT, errno);
        close(sock);
        s_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    /* A short receive timeout rather than a blocking recv, so the task notices s_running
     * going false promptly. A blocking recv would pin this task (and its stack) for as long
     * as the portal was up, and the teardown path could not reclaim either. */
    struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    ESP_LOGI(TAG, "captive-portal DNS up; every name resolves to this device");

    while (s_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        const int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&from, &from_len);
        if (len <= 0) continue;   /* timeout, or an error we ride out */

        const int reply_len = provdns_build_reply((const uint8_t *)rx, (size_t)len,
                                                  (uint8_t *)reply, sizeof(reply), ap_ip);
        if (reply_len > 0) {
            sendto(sock, reply, (size_t)reply_len, 0, (struct sockaddr *)&from, from_len);
        }
    }

    close(sock);
    s_task = NULL;
    vTaskDelete(NULL);
}

void prov_dns_start(void)
{
    if (s_task) return;
    s_running = 1;
    /* 4096 is what IDF's own reference uses for this task; the largest thing on its stack is
     * the 128-byte name buffer plus lwIP's socket call frames. */
    if (xTaskCreate(dns_task, "prov_dns", 4096, NULL, 5, &s_task) != pdPASS) {
        ESP_LOGE(TAG, "cannot start DNS task");
        s_running = 0;
        s_task = NULL;
    }
}

void prov_dns_stop(void)
{
    if (!s_running && !s_task) return;
    s_running = 0;
    /* Wait for the task to notice and exit on its own, rather than vTaskDelete()ing it. The
     * task owns a socket and a stack; deleting it mid-recv leaks the socket and can leave
     * lwIP's internal lock held. It polls once a second, so this is bounded and short. */
    for (int i = 0; i < 20 && s_task; i++) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (s_task) {
        ESP_LOGW(TAG, "DNS task did not stop; deleting it");
        vTaskDelete(s_task);
        s_task = NULL;
    }
}
