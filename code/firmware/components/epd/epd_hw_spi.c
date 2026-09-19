#include "epd_hw_spi.h"
#include "epd.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "soc/gpio_sig_map.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include <string.h>

static const char *TAG = "epd_hw_spi";

/* The e-paper shares its clock and data with the microSD and the GT30 font chip (HW-2),
 * so this bus is a shared resource: SPI2_HOST is chosen to keep it separate from any
 * future use of SPI3. */
#define EPD_SPI_HOST   SPI2_HOST

/* The data line is ONE wire (see epd_hw_spi.h). Aliasing MOSI and MISO to the same GPIO
 * is what lets the driver drive it and then listen on it. */
#define EPD_HW_PIN_MOSI  EPD_PIN_SDI   /* 23 */
#define EPD_HW_PIN_MISO  EPD_PIN_SDI   /* 23 — same wire, deliberately */

static spi_device_handle_t s_dev;
static spi_device_interface_config_t s_dev_cfg;
static int s_ready;
static int s_actual_hz;
static int s_logged;

/* Chunk the payload so a single transaction never needs more than this much staging.
 * 4 KB keeps DMA descriptors small and bounds the stack/heap cost of a transfer while
 * still amortising the per-transaction overhead over a useful amount of data. */
#define EPD_HW_CHUNK 4096

int epd_hw_spi_ready(void)      { return s_ready; }
int epd_hw_spi_actual_hz(void)  { return s_actual_hz; }

/* Take the shared clock and data pins out of the GPIO matrix so plain GPIO owns them.
 * gpio_set_direction() alone does NOT do this: while a pad is routed to a peripheral the
 * peripheral's output-enable keeps driving it, and gpio_set_level() on it only writes a
 * register bit nobody reads. Measured: with only SDI detached the read still returned
 * 0x00 because the bit-bang clocked nothing. Both pins must come back to GPIO. */
void epd_hw_spi_detach_pins(void)
{
    esp_rom_gpio_connect_out_signal(EPD_HW_PIN_MOSI, SIG_GPIO_OUT_IDX, false, false);
    esp_rom_gpio_connect_out_signal(EPD_PIN_SCLK,   SIG_GPIO_OUT_IDX, false, false);
}

/* Hand the shared pins back to the SPI peripheral after a borrowed read.
 *
 * Do NOT hand-wire the signal indices here. A first version connected the clock and data
 * pads to guessed SPI signal constants; the wrong index does not fail loudly, it just
 * leaves the pad disconnected from the peripheral. The symptom was precise and expensive
 * to find: a hardware write was a full 1137 ms refresh before any read, and only 333 ms
 * (command accepted, no waveform, glass unchanged) after one. Letting the driver re-route
 * its own signals is both correct and immune to that mistake. */
void epd_hw_spi_reattach_pins(void)
{
    /* Also called from epd_spi_init()/epd_init() to re-arm after they reconfigure pins,
     * so it must be safe when no bus exists yet. */
    if (!s_ready) return;
    gpio_set_direction(EPD_HW_PIN_MOSI, GPIO_MODE_OUTPUT);
    gpio_set_direction(EPD_PIN_SCLK, GPIO_MODE_OUTPUT);
    /* The bus IO routing is established by spi_bus_initialize() and is NOT redone by
     * spi_bus_add_device(), so a device-level re-attach is not enough. */
    int hz = s_actual_hz;
    spi_bus_remove_device(s_dev);
    spi_bus_free(EPD_SPI_HOST);
    s_dev = NULL;
    s_ready = 0;
    esp_err_t e = epd_hw_spi_init(hz);
    if (e != ESP_OK) ESP_LOGE(TAG, "re-attach failed: %s", esp_err_to_name(e));
}

esp_err_t epd_hw_spi_init(int speed_hz)
{
    if (s_ready) return ESP_OK;
    if (speed_hz <= 0) speed_hz = EPD_HW_SPI_DEFAULT_HZ;

    /* CS is driven manually (the vendor sequence toggles it per byte), so the driver
     * must NOT own it — spics_io_num = -1 leaves it to us. */
    spi_bus_config_t bus = {
        .mosi_io_num     = EPD_HW_PIN_MOSI,
        .miso_io_num     = EPD_HW_PIN_MISO,
        .sclk_io_num     = EPD_PIN_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = EPD_HW_CHUNK,
    };

    esp_err_t e = spi_bus_initialize(EPD_SPI_HOST, &bus, SPI_DMA_CH_AUTO);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "bus init failed: %s", esp_err_to_name(e));
        return e;
    }

    spi_device_interface_config_t dev = {
        .clock_speed_hz = speed_hz,
        .mode           = 0,          /* datasheet §4.2: rising edge, clock idle low */
        .spics_io_num   = -1,         /* CS is ours to drive (see above) */
        .queue_size     = 1,
        /* HALF-DUPLEX is mandatory here: the single SDA wire cannot be driven and
         * sampled at once. This is the documented ESP32 arrangement for one shared
         * data line, and it is what the vendor demo emulates by flipping the pin. */
        .flags          = SPI_DEVICE_HALFDUPLEX,
    };

    s_dev_cfg = dev;               /* kept so the bus can be re-attached after a read */
    e = spi_bus_add_device(EPD_SPI_HOST, &dev, &s_dev);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "add_device failed: %s", esp_err_to_name(e));
        spi_bus_free(EPD_SPI_HOST);
        return e;
    }

    int real_khz = 0;
    if (spi_device_get_actual_freq(s_dev, &real_khz) == ESP_OK) {
        s_actual_hz = real_khz * 1000;
    } else {
        s_actual_hz = speed_hz;
    }

    /* The bus must leave the data line as an output after init: the very next thing that
     * happens is a write. A half-duplex read tri-states it and the following write
     * re-arms it, so no stale input state can leak across. */
    s_ready = 1;
    /* A read frees and re-initialises this bus to re-route the pins, so logging every init
     * would spam the console once per read. Report only the first. */
    if (!s_logged) {
        s_logged = 1;
        ESP_LOGI(TAG, "hardware SPI up: requested %d Hz, actual %d Hz", speed_hz, s_actual_hz);
    }
    return ESP_OK;
}

void epd_hw_spi_deinit(void)
{
    if (!s_ready) return;
    spi_bus_remove_device(s_dev);
    spi_bus_free(EPD_SPI_HOST);
    s_dev = NULL;
    s_ready = 0;
    s_actual_hz = 0;
}

void epd_hw_spi_write(const uint8_t *data, size_t n)
{
    if (!s_ready || n == 0) return;
    while (n) {
        size_t chunk = (n > EPD_HW_CHUNK) ? EPD_HW_CHUNK : n;
        spi_transaction_t t = {
            .length    = chunk * 8,
            .tx_buffer = data,
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_dev, &t));
        data += chunk;
        n    -= chunk;
    }
}

void epd_hw_spi_write_fill(uint8_t value, size_t n)
{
    if (!s_ready || n == 0) return;
    uint8_t buf[256];
    memset(buf, value, sizeof(buf));
    while (n) {
        size_t chunk = (n > sizeof(buf)) ? sizeof(buf) : n;
        spi_transaction_t t = {
            .length    = chunk * 8,
            .tx_buffer = buf,
        };
        ESP_ERROR_CHECK(spi_device_polling_transmit(s_dev, &t));
        n -= chunk;
    }
}

void epd_hw_spi_read(uint8_t *out, size_t n)
{
    if (!s_ready || n == 0) return;

    /* The panel answers on the SAME single wire the master drives, so this cannot be an
     * SPI transaction: the peripheral would keep driving the line and fight the response.
     * The shared pins are handed to plain GPIO for the read and returned afterwards.
     *
     * The bit alignment was established by measurement, not by reading the datasheet.
     * Three independent observations pinned it down: every arrangement that clocked first
     * returned a one-bit-shifted byte (REV 0x70 read 0x0E, and 0x01 read 0x02); adding a
     * dummy leading clock shifted it one bit further (0x1C); and sampling *before* each
     * edge reproduced the datasheet's power-on value 0x07 exactly. So the panel presents
     * its first bit as soon as CS goes low — the first sample must precede any clock edge
     * — and each following edge presents the next bit. */
    epd_hw_spi_detach_pins();
    gpio_set_direction(EPD_PIN_SDI, GPIO_MODE_INPUT);
    gpio_set_direction(EPD_PIN_SCLK, GPIO_MODE_OUTPUT);
    gpio_set_level(EPD_PIN_SCLK, 0);
    for (size_t i = 0; i < n; i++) {
        uint8_t v = 0;
        for (int b = 0; b < 8; b++) {
            v = (uint8_t)((v << 1) | (gpio_get_level(EPD_PIN_SDI) ? 1u : 0u));
            /* Sample first, THEN pulse 0->1. The pulse order is load-bearing: with the
             * opposite order (1->0) the byte comes back one bit early (REV 0x0E), which is
             * what a first, wrong version of this function did. Every bit after the first
             * is presented by the rising edge preceding it, so no pulse follows the very
             * last bit of the transfer — otherwise a multi-byte read would arrive one bit
             * early from the second byte on. */
            if (!(i + 1 == n && b == 7)) {
                gpio_set_level(EPD_PIN_SCLK, 0);
                gpio_set_level(EPD_PIN_SCLK, 1);
            }
        }
        out[i] = v;
    }
    epd_hw_spi_reattach_pins();
}
