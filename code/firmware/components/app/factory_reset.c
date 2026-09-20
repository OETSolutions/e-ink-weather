#include "factory_reset.h"
#include "nvs_keys.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <string.h>

/* Factory reset on the board's user button.
 *
 * WHICH BUTTON: KEY1 on GPIO25, not BOOT. The board has a dedicated user button wired to
 * GPIO25 (verified from the schematic — see the pin-map record), and using it avoids two
 * problems BOOT would bring:
 *
 *   1. GPIO0 is a BOOTSTRAP strap. Holding it through a reset puts the chip into UART download
 *      mode, which is what the flashing tool relies on. Firmware that also reacts to it means a
 *      routine flash — the tool holds BOOT, resets, and releases — can be misread as a factory
 *      reset request.
 *   2. A strap pin is pulled up externally and driven low by the button, so its "pressed" level
 *      is not ours to choose.
 *
 * KEY1 has neither issue. It is a plain input with no alternate function, and nothing else in
 * this firmware uses GPIO25.
 *
 * WHY A HOLD RATHER THAN A PRESS: this is a destructive action that a user might trigger by
 * accident while handling the device, and there is no undo — the WiFi credentials and the
 * weather API key are gone and must be re-entered. Requiring a deliberate multi-second hold is
 * the standard guard, and the elapsed time comes from esp_timer so it is independent of loop
 * timing.
 *
 * WHY IT DOES NOT BLOCK THE BOOT: the check polls as the boot path progresses rather than
 * sitting in a busy-wait at the top. If the button is not held the cost is a couple of
 * microsecond-scale GPIO reads, and holding it for the full duration still only defers
 * provisioning — which is what the user is asking for. */

#define RESET_GPIO        GPIO_NUM_25
#define HOLD_MS           5000      /* deliberate: long enough not to be accidental */
#define POLL_MS           50

static const char *TAG = "factory_reset";

static int s_pressed_since_ms = -1;

static int now_ms(void)
{
    return (int)(esp_timer_get_time() / 1000);
}

void factory_reset_begin(void)
{
    /* Plain input. The button pulls the pin LOW when pressed and an external pull-up holds it
     * HIGH otherwise; internal pull-up is enabled too so a board without the external one still
     * reads HIGH when idle rather than floating. */
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << RESET_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "cannot configure the reset button; factory reset unavailable");
        return;
    }
    s_pressed_since_ms = -1;
}

int factory_reset_poll(void)
{
    /* Active low. */
    const int pressed = (gpio_get_level(RESET_GPIO) == 0);

    if (!pressed) {
        s_pressed_since_ms = -1;
        return 0;
    }
    if (s_pressed_since_ms < 0) {
        s_pressed_since_ms = now_ms();
        ESP_LOGI(TAG, "reset button held; keep holding to erase saved settings");
        return 0;
    }
    if (now_ms() - s_pressed_since_ms < HOLD_MS) {
        return 0;   /* still counting */
    }

    /* Held long enough. Announce it, then erase — the log is the only record, since the device
     * is about to restart and there is no console. */
    ESP_LOGW(TAG, "reset button held %d ms: erasing saved settings", HOLD_MS);
    factory_reset_perform();
    return 1;   /* not reached unless the erase failed; see below */
}

/* Block for the hold window IF the button is already down. Returns 1 if the hold completed
 * (the erase has run and the device is restarting), 0 otherwise.
 *
 * WHY THIS EXISTS SEPARATELY FROM factory_reset_poll(): the non-blocking poll only completes a
 * hold when it is called repeatedly over five seconds, and the boot path calls it at a couple of
 * points about a tenth of a second apart. On an unconfigured device that is the whole story —
 * the boot path then blocks in wifi_prov_mgr_wait() and nothing polls again, so a hold could
 * never complete exactly when a user most needs it (a device stuck in setup). This waits the
 * window itself, so "hold the button while powering on" works from any state.
 *
 * The cost to the normal case is ZERO: if the button is not down at entry this returns after one
 * GPIO read. Only a user actually holding the button pays the wait, and that wait is what they
 * asked for. */
int factory_reset_check_hold(void)
{
    if (gpio_get_level(RESET_GPIO) != 0) return 0;   /* not held: nothing to do */

    ESP_LOGI(TAG, "reset button down at boot; hold for %d ms to erase saved settings", HOLD_MS);
    const int start = now_ms();
    while (now_ms() - start < HOLD_MS) {
        if (gpio_get_level(RESET_GPIO) != 0) {
            ESP_LOGI(TAG, "reset button released; nothing erased");
            return 0;
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
    ESP_LOGW(TAG, "reset button held %d ms: erasing saved settings", HOLD_MS);
    factory_reset_perform();
    return 1;   /* not reached unless the erase failed */
}

/* Erase everything this device was told by a user, then restart into provisioning.
 *
 * This is deliberately the SAME erase the provisioning path would cause: the WiFi credentials
 * and every extra field (OWM key, HA URL and token, location) go, which is what makes the
 * device behave as new. It does NOT erase the whole NVS partition: the WiFi driver keeps its
 * own namespace, and a partial erase that left that behind would let the device reconnect to an
 * old network with no credentials of ours — the confusing half-reset to avoid. So that namespace
 * is cleared explicitly too.
 *
 * Restarts rather than continuing, because the boot path has already read the config into
 * memory and acted on it; the only clean state is a fresh boot with nothing stored. */
void factory_reset_perform(void)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open(DEVENV_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        /* Every user-supplied field. Listing them rather than erasing the namespace keeps this
         * honest if the namespace ever holds something that is not user data. */
        static const char *const keys[] = {
            DEVENV_KEY_WIFI_SSID, DEVENV_KEY_WIFI_PASS,
            DEVENV_KEY_OWM_KEY, DEVENV_KEY_HA_URL, DEVENV_KEY_HA_TOKEN,
            DEVENV_KEY_LOC_LAT, DEVENV_KEY_LOC_LON,
        };
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
            const esp_err_t ke = nvs_erase_key(h, keys[i]);
            if (ke != ESP_OK && ke != ESP_ERR_NVS_NOT_FOUND) {
                ESP_LOGW(TAG, "could not erase %s: %s", keys[i], esp_err_to_name(ke));
            }
        }
        nvs_commit(h);
        nvs_close(h);
    } else if (e != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "cannot open the config namespace: %s", esp_err_to_name(e));
    }

    /* The WiFi driver's own namespace, which the provisioning manager writes. Left behind, the
     * device would auto-reconnect to the old network while our own store says it is unset. */
    nvs_handle_t wh;
    if (nvs_open("nvs.net80211", NVS_READWRITE, &wh) == ESP_OK) {
        nvs_erase_all(wh);
        nvs_commit(wh);
        nvs_close(wh);
    }

    ESP_LOGW(TAG, "saved settings erased; restarting into setup");
    esp_restart();      /* does not return */
}
