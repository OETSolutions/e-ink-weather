#include "epd.h"
#include "epd_hw_spi.h"
#include "driver/gpio.h"
#include "esp_rom_sys.h"

/* The e-paper's transport, ported from the vendor demo's Display_EPD_W21_spi.cpp.
 *
 * The framing (CS/D/C per byte, MSB-first, mode 0) is identical either way; only the
 * shifting differs. When the hardware SPI bus is up it does the shifting — measured 85%
 * faster on a full frame (1468 ms -> 232 ms), which is the difference between a visible
 * flash and a barely perceptible one. When it is down this falls back to the bit-bang
 * loop, which stays the verified reference: it is what the vendor demo does and what
 * every bring-up result before the migration was measured with.
 *
 * Reads are the exception. The panel answers on the same single wire the master drives
 * (see epd_hw_spi.c), so a read must take the pins back to plain GPIO first; the
 * hardware-SPI read does that itself and is safe to call while the bus is up. */

void epd_spi_init(void)
{
    /* SCLK and SDI belong to the SPI peripheral once its bus is up. Reconfiguring them
     * here would tear the pads out of the GPIO matrix and silently disconnect the fast
     * path — measured: writes then went nowhere (a 232 ms "refresh" with no waveform and
     * BUSY never asserting) and every read returned 0xFF. So they are configured here
     * only when there is no hardware bus to defer to. Call order therefore does not
     * matter, which is the point: the transport must not depend on who ran first. */
    const gpio_num_t outs[] = { EPD_PIN_RES, EPD_PIN_DC, EPD_PIN_CS,
                                EPD_PIN_SCLK, EPD_PIN_SDI };
    for (unsigned i = 0; i < sizeof(outs) / sizeof(outs[0]); i++) {
        if (epd_hw_spi_ready() && (outs[i] == EPD_PIN_SCLK || outs[i] == EPD_PIN_SDI)) continue;
        gpio_reset_pin(outs[i]);
        gpio_set_direction(outs[i], GPIO_MODE_OUTPUT);
    }
    /* BUSY is a PLAIN INPUT with no pull-up, matching the vendor demo's
     * `pinMode(A14, INPUT)`. This is deliberate: an internal pull-up would make an
     * absent panel read as "ready" and silently defeat the timeout guard. */
    gpio_reset_pin(EPD_PIN_BUSY);
    gpio_set_direction(EPD_PIN_BUSY, GPIO_MODE_INPUT);

    gpio_set_level(EPD_PIN_CS, 1);
    if (!epd_hw_spi_ready()) gpio_set_level(EPD_PIN_SCLK, 0);
}

void epd_spi_reset(int level) { gpio_set_level(EPD_PIN_RES, level ? 1 : 0); }
int  epd_spi_busy_read(void)  { return gpio_get_level(EPD_PIN_BUSY); }

/* MSB-first, clock idle low, data sampled on the rising edge. */
void epd_spi_shift_out(uint8_t value)
{
    for (int i = 0; i < 8; i++) {
        gpio_set_level(EPD_PIN_SCLK, 0);
        gpio_set_level(EPD_PIN_SDI, (value & 0x80) ? 1 : 0);
        value = (uint8_t)(value << 1);
        gpio_set_level(EPD_PIN_SCLK, 1);
    }
}

/* One entry point for every byte the driver sends, so the transport is chosen in exactly
 * one place and cannot drift between the command and data paths. */
static void spi_write(const uint8_t *data, int n)
{
    if (epd_hw_spi_ready()) {
        epd_hw_spi_write(data, (size_t)n);
        return;
    }
    for (int i = 0; i < n; i++) epd_spi_shift_out(data[i]);
}

void epd_write_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 0);   /* D/C# 0 = command */
    spi_write(&cmd, 1);
    gpio_set_level(EPD_PIN_CS, 1);
}

void epd_write_data(uint8_t data)
{
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 1);   /* D/C# 1 = data */
    spi_write(&data, 1);
    gpio_set_level(EPD_PIN_CS, 1);
}

void epd_write_data_block(const uint8_t *data, int n)
{
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 1);
    spi_write(data, n);
    gpio_set_level(EPD_PIN_CS, 1);
}

/* The vendor demo reconfigures SDA as an input for the read, then back to output.
 * Reproduced here because the SSD2677 drives the same line for the temperature byte.
 * While the hardware bus is up, defer to it: it performs the same borrow but also takes
 * the pins out of the GPIO matrix, which plain gpio_set_direction() does not do. */
int epd_spi_read_byte(void)
{
    if (epd_hw_spi_ready()) return epd_spi_read_byte_hw();
    uint8_t temp = 0;
    gpio_set_direction(EPD_PIN_SDI, GPIO_MODE_INPUT);
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 1);
    for (int i = 0; i < 8; i++) {
        gpio_set_level(EPD_PIN_SCLK, 0);
        temp = (uint8_t)((temp << 1) | (gpio_get_level(EPD_PIN_SDI) ? 1 : 0));
        gpio_set_level(EPD_PIN_SCLK, 1);
        gpio_set_level(EPD_PIN_SCLK, 0);
    }
    gpio_set_direction(EPD_PIN_SDI, GPIO_MODE_OUTPUT);
    return temp;
}

/* ---- Hardware-SPI variants (HW-7). Identical framing; only the shifting differs, so
 * the two transports are drop-in interchangeable and can be compared directly. ---- */

void epd_write_cmd_hw(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 0);
    epd_hw_spi_write(&cmd, 1);
    gpio_set_level(EPD_PIN_CS, 1);
}

void epd_write_data_hw(uint8_t data)
{
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 1);
    epd_hw_spi_write(&data, 1);
    gpio_set_level(EPD_PIN_CS, 1);
}

void epd_write_data_block_hw(const uint8_t *data, int n)
{
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 1);
    epd_hw_spi_write(data, (size_t)n);
    gpio_set_level(EPD_PIN_CS, 1);
}

int epd_spi_read_byte_hw(void)
{
    uint8_t v = 0;
    gpio_set_level(EPD_PIN_CS, 0);
    gpio_set_level(EPD_PIN_DC, 1);
    epd_hw_spi_read(&v, 1);
    gpio_set_level(EPD_PIN_CS, 1);
    return v;
}
