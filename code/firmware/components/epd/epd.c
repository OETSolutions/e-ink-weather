#include "epd.h"
#include "epd_hw_spi.h"
#include "epd_encode.h"
#include "canvas.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/* The register sequence below is VENDOR-PROPRIETARY (HW-5): registers 0x00 PSR,
 * 0x30, 0x62 HTOTAL, 0x65 GSST, 0xE0, 0xE6, 0xE7, 0xE9 do NOT appear in the
 * public SSD2677 datasheet. Reproduced verbatim from
 * GDEH0576T81_Arduino_demo_code/Display_EPD_W21.cpp. DO NOT "clean this up". */

/* BUSY is active-low on this panel (demo: `if (isEPD_W21_BUSY==1) break;` — it breaks
 * when BUSY reads HIGH). The vendor demo waits in an unbounded `while(1)`. We must NOT:
 * a loose FPC or unpowered panel would hang the device forever, with no watchdog and no
 * error surfaced (FR-29 requires the failure be visible, not a wedged panel). Bound it.
 *
 * The bound is measured against the WALL CLOCK, not a loop-iteration count. An earlier
 * version counted iterations with `vTaskDelay(pdMS_TO_TICKS(1))`; with the default
 * CONFIG_FREERTOS_HZ=100 that converts to ZERO ticks, so the delay never blocked and a
 * nominal 20 s bound expired in ~228 ms — on a HEALTHY panel, mid-refresh. Every refresh
 * then reported ESP_ERR_TIMEOUT and nothing was ever drawn. Verified on hardware. */
esp_err_t epd_wait_ready_ms(int timeout_ms)
{
    if (timeout_ms < 0) return ESP_ERR_INVALID_ARG;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (epd_spi_busy_read() != 1) {          /* 1 = released/idle */
        if (esp_timer_get_time() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);                          /* exactly one tick; never rounds to 0 */
    }
    return ESP_OK;
}

/* Generous: a full refresh is ~2-4 s, so 20 s cannot trip on a healthy panel but still
 * fails fast enough to stay inside the boot path's error budget. */
esp_err_t epd_wait_ready(void) { return epd_wait_ready_ms(20000); }

/* BUSY is asserted a short but nonzero time AFTER a refresh command, and the hardware
 * transport returns from the command so fast that polling immediately still sees BUSY
 * high — which reads as "refresh already finished". Measured: the full refresh reported
 * 0 ms of waveform, i.e. the bounded wait was not gating anything. Wait for the panel to
 * claim BUSY first, then for it to release it. If BUSY never asserts within the window
 * the refresh has either already completed or the panel is not responding, so fall
 * through to the release wait, which is the one that carries the timeout. */
static esp_err_t wait_refresh(void)
{
    int64_t deadline = esp_timer_get_time() + 100 * 1000;   /* vendor demo's 100 ms */
    while (epd_spi_busy_read() != 0) {
        if (esp_timer_get_time() >= deadline) break;
        esp_rom_delay_us(50);
    }
    return epd_wait_ready();
}

static void delay_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

static void write_lut_all(void)
{
    int t = 0;
    (void)epd_read_temp(&t);
    epd_write_cmd(0xE0);
    epd_write_data(0x02);
    epd_write_cmd(0xE6);
    epd_write_data((uint8_t)epd_lut_value_for_temp(t));
    epd_write_cmd(0xA5);
    epd_wait_ready();
    delay_ms(10);
}

/* Common prefix of EPD_init() and EPD_init_Part(): identical in the vendor demo. */
static esp_err_t init_common(void)
{
    delay_ms(10);
    epd_spi_reset(0); delay_ms(10);
    epd_spi_reset(1); delay_ms(10);
    /* RES is not a shared pin, but re-arming here keeps the fast path's ownership of the
     * clock and data pins explicit at every point the panel is touched. */
    epd_hw_spi_reattach_pins();

    /* NOTE: unlike the vendor demo, do NOT `return` early on timeout here. The vendor
     * sequence must still be written in full, because a partially-initialised controller
     * may latch undefined state. Record the first failure and keep going; the caller
     * decides (FR-29: show the error, keep the last good image). */
    esp_err_t first_err = epd_wait_ready();

    epd_write_cmd(0x00); epd_write_data(0x27); epd_write_data(0x0E);
    epd_wait_ready();
    epd_write_cmd(0x06);
    epd_write_data(0x0F); epd_write_data(0x8B);
    epd_write_data(0x9C); epd_write_data(0xC1);
    epd_write_cmd(0xE7); epd_write_data(0xC1);
    epd_write_cmd(0x30); epd_write_data(0x08);
    epd_write_cmd(0x50); epd_write_data(0x77);
    epd_write_cmd(0x61);
    epd_write_data(EPD_WIDTH / 256);  epd_write_data(EPD_WIDTH % 256);
    epd_write_data(EPD_HEIGHT / 256); epd_write_data(EPD_HEIGHT % 256);
    epd_write_cmd(0x62);
    static const uint8_t htotal[8] = { 0x98,0x98,0x98,0x75,0xCA,0xB2,0x98,0x7E };
    for (int i = 0; i < 8; i++) epd_write_data(htotal[i]);
    epd_write_cmd(0x65);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_data(0x00); epd_write_data(0x00);
    epd_write_cmd(0xE9); epd_write_data(0x01);
    return first_err;
}

/* 1 once the panel has been put into deep sleep and has not been woken since. */
static int s_asleep;

esp_err_t epd_init(void)
{
    s_asleep = 0;   /* init_common() pulses RES, which is what wakes a sleeping controller */
    /* Bring the hardware transport up first so every byte below goes through it. The
     * bit-bang path remains available and is what runs if this fails, so a bus problem
     * degrades speed rather than correctness. Measured on a full frame: shifting drops
     * from 1468 ms to 232 ms, taking a whole refresh from 2389 ms to 1137 ms. */
    (void)epd_hw_spi_init(EPD_HW_SPI_DEFAULT_HZ);
    epd_spi_init();
    esp_err_t first_err = init_common();
    write_lut_all();
    epd_write_cmd(0x04);           /* power on */
    if (epd_wait_ready() != ESP_OK && first_err == ESP_OK) first_err = ESP_ERR_TIMEOUT;
    return first_err;
}

esp_err_t epd_init_partial(void)
{
    s_asleep = 0;
    epd_spi_init();
    esp_err_t first_err = init_common();
    /* Partial OTP instead of the temperature LUT ladder. */
    epd_write_cmd(0xE0); epd_write_data(0x00);
    epd_write_cmd(0xA5);
    epd_wait_ready();
    epd_write_cmd(0x04);           /* power on */
    if (epd_wait_ready() != ESP_OK && first_err == ESP_OK) first_err = ESP_ERR_TIMEOUT;
    return first_err;
}

/* THE PANEL'S ROW ORDER IS BOTTOM-UP.
 *
 * Everywhere else in this firmware a framebuffer is in NATURAL order: row 0 is the top of the
 * picture, which is the order the web canvas editor draws in, the order the golden image is
 * generated in, and the order a human reads a byte dump in. The controller displays the FIRST
 * row it receives at the BOTTOM of the glass — its scan is bottom-up — so a frame transmitted
 * top-first appears upside down.
 *
 * This stayed hidden because the only frames ever pushed were symmetric (all-black, all-white)
 * or the vendor's own gImage_1, which is STORED pre-rotated to compensate for exactly this. So
 * the vendor image looked right and proved nothing. The first naturally-oriented content — the
 * composed static layer plus the temperature glyphs — came out mirrored on the glass, which is
 * how this was finally caught. Confirmed by dumping gImage_1 from src/gimage1_data.c and
 * rotating it: the vertical flip is the logo the panel displays.
 *
 * The flip lives HERE, in the driver, rather than in the renderer, because bottom-up scanning
 * is a property of this panel. Keeping the framebuffer natural leaves the golden test, the
 * upload format and the canvas editor all in the order a human expects — only the byte stream
 * to the glass is reordered. Note it is a ROW flip, not a 180-degree rotation: the columns are
 * already correct, verified by the same dump (the flipped image reads left to right). */
#define EPD_TRANSMIT_ROW(y) ((size_t)(EPD_HEIGHT - 1 - (y)) * EPD_PITCH)

/* Returns ESP_OK only if the whole frame — including the refresh completing — succeeded.
 * On timeout the panel is left alone rather than retried, so the previous image stays. */
esp_err_t epd_write_frame(const uint8_t *fb1bpp)
{
    static uint8_t line2[EPD_PITCH * 2];   /* one 2bpp row, 230 bytes */
    epd_write_cmd(0x10);
    if (epd_wait_ready() != ESP_OK) return ESP_ERR_TIMEOUT;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        epd_expand_1to2(&fb1bpp[EPD_TRANSMIT_ROW(y)], EPD_PITCH, line2);
        epd_write_data_block(line2, sizeof(line2));
    }
    epd_write_cmd(0x12);           /* DRF: display refresh */
    epd_write_data(0x00);
    return wait_refresh();         /* ~910 ms of actual e-paper update happens here */
}

/* Partial refresh: the controller derives each pixel's transition from the pair
 * (prev on the glass, next desired), so both frames are required — the vendor demo's
 * PIC_display_Part_ALL() bit-interleaves them byte by byte. */
esp_err_t epd_write_frame_partial(const uint8_t *prev1bpp, const uint8_t *next1bpp)
{
    static uint8_t line2[EPD_PITCH * 2];
    epd_write_cmd(0x10);
    if (epd_wait_ready() != ESP_OK) return ESP_ERR_TIMEOUT;
    for (int y = 0; y < EPD_HEIGHT; y++) {
        const size_t off = EPD_TRANSMIT_ROW(y);
        epd_interleave_1to2(&prev1bpp[off], &next1bpp[off], EPD_PITCH, line2);
        epd_write_data_block(line2, sizeof(line2));
    }
    epd_write_cmd(0x12);
    epd_write_data(0x00);
    return wait_refresh();
}

esp_err_t epd_sleep(void)
{
    epd_write_cmd(0x02); epd_write_data(0x00);   /* power off */
    esp_err_t err = epd_wait_ready();
    epd_write_cmd(0x07); epd_write_data(0xA5);   /* deep sleep (FR-12) */
    if (err == ESP_OK) s_asleep = 1;
    return err;
}

/* See epd.h: the controller ignores commands while in deep sleep, so anything that draws
 * after a sleep must reset it first. Tracked here rather than left to the caller because a
 * missed reset does not fail loudly — it times out on BUSY, which looks like a wiring fault.
 * Note the flag is only set when the sleep command actually went out, so a failed sleep does
 * not force a needless re-init. */
esp_err_t epd_wake(void)
{
    if (!s_asleep) return ESP_OK;
    s_asleep = 0;
    return epd_init();
}

/* Hardware-SPI temperature read, same sequence as the software version. */
esp_err_t epd_read_temp_hw(int *out_c)
{
    if (!out_c) return ESP_ERR_INVALID_ARG;
    epd_write_cmd_hw(0x40);
    if (epd_wait_ready() != ESP_OK) return ESP_ERR_TIMEOUT;
    int raw = epd_spi_read_byte_hw();
    if (raw > 127) raw -= 256;
    *out_c = raw;
    return ESP_OK;
}

esp_err_t epd_read_temp(int *out_c)
{
    if (!out_c) return ESP_ERR_INVALID_ARG;
    epd_write_cmd(0x40);
    if (epd_wait_ready() != ESP_OK) return ESP_ERR_TIMEOUT;
    int raw = epd_spi_read_byte();     /* datasheet: returns degC directly */
    if (raw > 127) raw -= 256;         /* two's complement */
    *out_c = raw;
    return ESP_OK;
}
