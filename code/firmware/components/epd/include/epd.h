#pragma once
#include <stdint.h>
#include "esp_err.h"

/* Pin map verified from the schematic (HW-2 / spec §2.3). BUSY/RES/DC/CS are Arduino
 * ANALOG aliases on the sheet (A14/A15/A16/A17) but are ordinary GPIOs. */
#define EPD_PIN_BUSY  13
#define EPD_PIN_RES   12
#define EPD_PIN_DC    14
#define EPD_PIN_CS    27
#define EPD_PIN_SCLK  18
#define EPD_PIN_SDI   23

/* Every hardware entry point returns esp_err_t. BUSY waits are BOUNDED (see epd.c):
 * ESP_ERR_TIMEOUT means the panel did not release BUSY in time — a loose FPC, an
 * unpowered panel, or a stalled controller. Callers must surface this and keep the
 * last good image (FR-29); they must never spin forever. */
esp_err_t epd_init(void);                                  /* full-update init (HW-5) */
esp_err_t epd_init_partial(void);                          /* partial-OTP init */
esp_err_t epd_write_frame(const uint8_t *fb1bpp);          /* full update */
/* Partial update takes the frame CURRENTLY ON THE GLASS plus the new frame; the
 * controller derives each pixel's transition from the pair (vendor PIC_display_Part_ALL). */
esp_err_t epd_write_frame_partial(const uint8_t *prev1bpp, const uint8_t *next1bpp);
esp_err_t epd_sleep(void);                                 /* power-off + deep sleep (FR-12) */

/* Wake the panel after epd_sleep(), which puts the CONTROLLER into deep sleep (0x07/0xA5).
 *
 * While asleep the controller ignores everything except a hardware reset, so a write issued
 * after epd_sleep() does not fail loudly — it times out on BUSY, which is indistinguishable
 * from a loose FPC. That is exactly what happened when the USB serve loop re-rendered after
 * the boot path had already slept the panel: every refresh reported ESP_ERR_TIMEOUT and the
 * display never changed.
 *
 * Cheap when the panel is already awake (returns immediately), so callers that are unsure
 * whether they slept it can call this unconditionally before drawing. */
esp_err_t epd_wake(void);
esp_err_t epd_read_temp(int *out_c);
esp_err_t epd_read_temp_hw(int *out_c);                       /* cmd 0x40, returns degC */
esp_err_t epd_wait_ready(void);                            /* bounded BUSY wait (FR-13) */
esp_err_t epd_wait_ready_ms(int timeout_ms);               /* explicit timeout */

/* Transport, used by the driver and by the BUSY regression test. */
void    epd_spi_init(void);
void    epd_spi_reset(int level);
int     epd_spi_busy_read(void);
void    epd_write_cmd(uint8_t cmd);
void    epd_write_data(uint8_t data);
void    epd_write_data_block(const uint8_t *data, int n);
int     epd_spi_read_byte(void);

/* Same framing as epd_write_cmd/data/block, but shifted through the hardware SPI
 * transport instead of the bit-bang loop. D/C and CS are still driven by hand because
 * the SSD2677 needs them asserted per byte. Requires epd_hw_spi_init() to have run. */
void    epd_write_cmd_hw(uint8_t cmd);
void    epd_write_data_hw(uint8_t data);
void    epd_write_data_block_hw(const uint8_t *data, int n);
int     epd_spi_read_byte_hw(void);

/* Raw MSB-first shift with NO chip-select or D/C handling. This file owns the shared
 * bus's bit timing, so other chips on it (the GT30 font chip) drive their own CS and
 * shift through these rather than reimplementing the clock. Callers must keep the
 * e-paper's CS HIGH for the whole transaction (HW-2). */
void    epd_spi_shift_out(uint8_t value);
int     epd_spi_shift_in_on(int gpio_num);
