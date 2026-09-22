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

/* Load the waveform that matches the WIRE FORMAT `partial` selects, re-initialising only when the
 * controller is not already holding the right one. Returns ESP_OK when the correct waveform is
 * loaded.
 *
 * WHY THIS EXISTS AS ITS OWN CALL: the two waveforms are not interchangeable, and driving the
 * controller in one mode while sending the other mode's byte stream is SILENT — the panel accepts
 * the bytes and shows the wrong thing (grey, half-transitioned pixels where a value changed).
 * Whether the right one is loaded depends on history: epd_init() loads the temperature LUT ladder,
 * epd_init_partial() loads the partial OTP, and epd_wake() re-loads the full ladder ONLY when the
 * panel was successfully put to sleep. So a FAILED epd_sleep() (a BUSY timeout, which the driver
 * deliberately reports rather than retrying forever) leaves the partial OTP loaded and the panel
 * awake — and the next tick, if the policy asks for a full refresh, would then draw a full frame
 * with the partial waveform.
 *
 * Tracking the loaded waveform here rather than at the call site is what makes that unreachable:
 * the driver owns the register state, so it is the only place that can know it. `partial` selects
 * which, matching the `partial` flag of epd_write_frame_banded(). */
esp_err_t epd_ensure_waveform(int partial);

esp_err_t epd_write_frame(const uint8_t *fb1bpp);          /* full update */
/* Partial update takes the frame CURRENTLY ON THE GLASS plus the new frame; the
 * controller derives each pixel's transition from the pair (vendor PIC_display_Part_ALL). */
esp_err_t epd_write_frame_partial(const uint8_t *prev1bpp, const uint8_t *next1bpp);

/* ---- banded update: full AND partial ----
 *
 * WHY THIS EXISTS: the pair-taking call above needs BOTH whole frames resident, and this part
 * cannot hold two 78,200-byte framebuffers at once (measured: the only region large enough for one
 * is 113,840 bytes; the pair needs 156,400). Measured on the bench, the second buffer therefore
 * failed to allocate on EVERY refresh and `/api/status` showed partials_since_full: 0 against
 * fulls_total: 6 — FR-11's partial path never ran, and every refresh was a full panel flash.
 *
 * The banded writer streams the panel in horizontal bands instead, so the caller holds ONE full
 * buffer (the static layer) plus two small band buffers, whatever the refresh kind. That is what
 * makes a partial possible at all on this part.
 *
 * The caller supplies a band provider rather than frames:
 *
 *   fill(ctx, band_index, prev_out, next_out, rows_out)
 *
 * writes the band's rows into the buffers and reports how many rows it produced. Bands are
 * requested in DESCENDING index order (the panel scans bottom-up, so the first rows sent are the
 * last natural rows) and each exactly once — a provider that assumes ascending order will produce
 * a vertically scrambled panel.
 *
 * `partial` selects the wire format the controller expects: a partial interleaves the previous and
 * the next frame because the SSD2677 derives each pixel's TRANSITION from the pair, while a full
 * expands the next frame alone. The provider must still fill `next_out` for a full; `prev_out` is
 * then unused and may be ignored.
 *
 * `band_rows` MUST be > 0, and `prev_band` / `next_band` each hold band_rows * EPD_PITCH bytes.
 *
 * THE CALLER OWNS THE BAND BUFFERS, deliberately. Holding them as driver statics would make them
 * RESIDENT for the life of the device — and this part has one region large enough for the static
 * layer, so a permanently-held band pair is memory the radio cannot have at boot, which is the
 * exact starvation that stops the device being provisioned (see app_refresh.c). The app allocates
 * them for the push and frees them after, so they exist only inside the render window.
 *
 * `prev_band` may be NULL when `partial` is 0: a full update expands the next frame alone and
 * never reads the previous one. Returns ESP_OK on success. */
typedef int (*epd_band_fn)(void *ctx, int band_index, uint8_t *prev_out,
                           uint8_t *next_out, int *rows_out);

esp_err_t epd_write_frame_banded(epd_band_fn fill, void *ctx,
                                 uint8_t *prev_band, uint8_t *next_band,
                                 int band_rows, int partial);
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
