#include "gt30.h"
#include "epd.h"
#include "gt30_addr.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "gt30";

/* Decoded from the schematic: `SPI_CS_GT30` lands on the ESP32 module's A13/T3 pin,
 * and SPI_MISO on IO19, wired to the GT30's SO (pin 2). */
#define GT30_PIN_CS   15
#define GT30_PIN_MISO 19

/* Opcode 0x03, "read data bytes" (LibDriver: GT30L32S4W_MODE_READ). There is no
 * read-ID opcode in this chip's command set — see gt30.h. */
#define GT30_CMD_READ 0x03

/* The 8x16 ASCII face's glyph for 'A', byte-for-byte as it appears in LibDriver's
 * `gt30l32s4w_init()` reference buffer. Comparing real content (not just "not 0x00 and
 * not 0xFF") is what makes a floating MISO line detectable: an unconnected input reads
 * as a constant, and a constant cannot match this pattern. */
static const uint8_t GT30_GLYPH_A[GT30_GLYPH_BYTES_8X16] = {
    0x00, 0x10, 0x28, 0x28, 0x28, 0x44, 0x44, 0x7C,
    0x82, 0x82, 0x82, 0x82, 0x00, 0x00, 0x00, 0x00,
};

/* Three-state cache: -1 = not yet probed, 0 = absent, 1 = present. */
static int s_present = -1;

/* Shift one byte in over the shared bus, MSB-first, sampling on the rising edge to
 * match epd_spi_shift_out(). The clock is borrowed from the e-paper transport so the
 * two chips can never disagree about bit timing (HW-2: e-paper CS stays HIGH). */
static uint8_t gt30_read_byte(void)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        gpio_set_level(EPD_PIN_SCLK, 0);
        v = (uint8_t)((v << 1) | (gpio_get_level(GT30_PIN_MISO) ? 1u : 0u));
        gpio_set_level(EPD_PIN_SCLK, 1);
    }
    return v;
}

int gt30_present(void)
{
    if (s_present >= 0) return s_present;      /* probed once per boot */

    s_present = 0;                             /* fail closed: absent unless proven */

    /* The clock and data lines belong to the e-paper transport, and nothing here works
     * unless that transport has configured them as outputs. Calling the bus owner's init
     * makes the probe independent of call order — without it, probing before epd_init()
     * leaves SCLK floating and the chip reads as ABSENT on hardware where it is present
     * (observed on this bench). epd_spi_init() is idempotent. */
    epd_spi_init();

    gpio_reset_pin(GT30_PIN_CS);
    gpio_set_direction(GT30_PIN_CS, GPIO_MODE_OUTPUT);
    gpio_set_level(GT30_PIN_CS, 1);            /* idle high */

    gpio_reset_pin(GT30_PIN_MISO);
    gpio_set_direction(GT30_PIN_MISO, GPIO_MODE_INPUT);
    /* No pull-up: an absent chip leaves this line floating, and a pull-up would bias
     * the read toward a fixed pattern. A floating input cannot reproduce the glyph. */

    uint32_t addr = gt30_ascii_addr(GT30_ADDR_8X16_ASCII, 'A');
    if (addr == GT30_ADDR_INVALID) return s_present;   /* cannot happen for 'A' */

    gpio_set_level(EPD_PIN_CS, 1);             /* e-paper deselected (HW-2) */
    gpio_set_level(GT30_PIN_CS, 0);            /* GT30 selected */

    epd_spi_shift_out(GT30_CMD_READ);
    epd_spi_shift_out((uint8_t)(addr >> 16));
    epd_spi_shift_out((uint8_t)(addr >> 8));
    epd_spi_shift_out((uint8_t)addr);

    uint8_t got[GT30_GLYPH_BYTES_8X16];
    for (unsigned i = 0; i < sizeof(got); i++) got[i] = gt30_read_byte();

    gpio_set_level(GT30_PIN_CS, 1);

    if (memcmp(got, GT30_GLYPH_A, sizeof(got)) == 0) {
        s_present = 1;
        ESP_LOGI(TAG, "font chip present (8x16 'A' verified at 0x%06X)", (unsigned)addr);
    } else {
        s_present = 0;
        ESP_LOGI(TAG, "font chip not detected — using flash font atlas (FR-14)");
    }
    return s_present;
}
