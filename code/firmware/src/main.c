#include "app_boot.h"
#include "app_refresh.h"
#include "api.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* Bench verification only. Generated per-machine by tools/gen_secrets_header.py from
 * code/.env and gitignored; a normal clone has no such header, so this block does not
 * exist and the device waits to be provisioned (FR-30). */
#if __has_include("secrets_build.h")
#include "secrets_build.h"
#define HAVE_BENCH_SECRETS 1
#endif

static const char *TAG = "main";

/* app_main runs on the IDF main task, whose stack is CONFIG_ESP_MAIN_TASK_STACK_SIZE =
 * 3584 bytes on this board. The boot path does a TLS handshake (net_http.c), an OTA, an
 * NVS write and a 4 KB body parse — the Arduino loopTask overflow at ~8 KB is the exact
 * crash class this project exists to avoid (spec §9.1). So the work runs on a SIZED worker
 * task and app_main only starts it and returns; IDF keeps the main task alive afterwards.
 *
 * 16 KB matches NET_TLS_TASK_STACK, because the boot path's deepest frame is the same
 * handshake net_http.c already sized for. */
#define APP_TASK_STACK 16384
#define APP_TASK_PRIO  5

static void app_task(void *arg)
{
    (void)arg;
#ifdef HAVE_BENCH_SECRETS
    /* Seed before the boot path reads NVS for credentials. Never overwrites (see
     * app_seed_wifi), so a provisioned device is unaffected. */
    app_seed_wifi(NET_VERIFY_SSID, NET_VERIFY_PASS);
    app_seed_owm_key(NET_VERIFY_OWM_KEY);
    /* Home Assistant is optional: an empty .env value compiles to "" and this no-ops. */
    app_seed_ha(NET_VERIFY_HA_URL, NET_VERIFY_HA_TOKEN);
#endif
    app_boot_run();
    /* app_boot_run() returns only on USB power, where the device must stay awake to serve
     * the API. The task parks rather than exiting: a FreeRTOS task that returns from its
     * entry point aborts unless it deletes itself first, and deleting the task that owns
     * the boot path's local state is not worth the saving on a device that is mains-
     * powered in this branch. */
    for (;;) vTaskDelay(pdMS_TO_TICKS(10000));
}

void app_main(void)
{
    ESP_LOGI(TAG, "e-ink weather display starting");

    /* BEFORE the worker task, and before anything else allocates: the two framebuffers need
     * one contiguous 76.4 KiB block each, and this part has no PSRAM. Creating a 16 KiB task
     * first splits the only region large enough for the second one, which makes the render
     * path fail permanently with a misleading "out of memory" while 199 KB is still free.
     * See app_fbs_reserve() for the full explanation. */
    if (app_fbs_reserve() != ESP_OK) {
        /* Not fatal: the device still boots, serves the API and can be provisioned — it just
         * cannot draw. Restarting would loop forever against the same constraint, so this is
         * reported and the boot continues. */
        ESP_LOGE(TAG, "framebuffers unavailable; the panel cannot be updated this boot");
    }

    BaseType_t ok = xTaskCreate(app_task, "app", APP_TASK_STACK, NULL, APP_TASK_PRIO, NULL);
    if (ok != pdPASS) {
        /* Without this the device would sit in app_main doing nothing, which looks exactly
         * like a hang. Say so loudly and restart into a clean state instead. */
        ESP_LOGE(TAG, "cannot create app task (%d bytes)", APP_TASK_STACK);
        esp_restart();
    }
}
