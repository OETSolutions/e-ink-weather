#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

/* Hardware-SPI transport for the e-paper panel (HW-7).
 *
 * WHY THIS EXISTS: the software bit-bang path moves 156,400 wire bytes per frame at
 * ~2 GPIO writes per bit. Measured on hardware, that is **1471 ms of the 2379 ms full
 * refresh — 62%**, against only ~910 ms of irreducible waveform time. So the transfer is
 * the part worth removing, and this module removes it.
 *
 * THE CONSTRAINT THAT SHAPES THE DESIGN: the SSD2677's serial data pin (`SDA`) is a
 * SINGLE BIDIRECTIONAL line — the EPD FPC exposes only one data pin (pin 14, `SDI`), and
 * the vendor demo has to flip GPIO23 to an INPUT to read the temperature byte. A
 * conventional MOSI/MISO pair therefore does not exist here. The bus is driven in
 * HALF-DUPLEX with MOSI and MISO aliased onto the same GPIO; the driver is expected to
 * tri-state MOSI before any read. That is exactly the arrangement the vendor demo
 * emulates by hand.
 *
 * SPI MODE: the SSD2677 datasheet (Rev 1.1 §4.2, Table 4-2) specifies data shifted in on
 * the **rising** edge with the clock idle low — SPI mode 0.
 *
 * CLOCK SPEED: the datasheet's entire SPI timing table reads "TBD" (HW-7), so there is
 * no published maximum. 10 MHz is the value that has actually been verified end to end.
 * An earlier sweep appeared to pass at up to 40 MHz, but it ran while writes were not
 * landing at all, so it proved nothing. Raise this only with the alternating-image check
 * (see epd_hw_spi_read below) — never from a timer. */

/* Verified on hardware: full refresh 1289 ms (233 ms push + ~1056 ms waveform). */
#define EPD_HW_SPI_DEFAULT_HZ  (10 * 1000 * 1000)

/* Bring up the bus and register the panel device. Idempotent. `speed_hz` of 0 uses
 * EPD_HW_SPI_DEFAULT_HZ. Returns ESP_OK on success. */
esp_err_t epd_hw_spi_init(int speed_hz);

/* Release the bus. Safe to call when not initialised. */
void epd_hw_spi_deinit(void);

/* True once epd_hw_spi_init() has succeeded. */
int epd_hw_spi_ready(void);

/* Report the clock the driver actually achieved (may differ from the request). */
int epd_hw_spi_actual_hz(void);

/* Raw transfers, MSB-first, mode 0. CS and D/C are driven by the caller through the same
 * GPIOs the software path uses, so the two transports are interchangeable. */
void epd_hw_spi_write(const uint8_t *data, size_t n);

/* Read `n` bytes from the panel. NOT an SPI transaction: the panel answers on the same
 * single wire the master drives, so this bit-bangs with the bus freed and re-initialised
 * around the read. Bit alignment was established by measurement — sampling before each
 * 0->1 pulse reproduces REV (0x70) = 0x07. */
void epd_hw_spi_read(uint8_t *out, size_t n);

/* Take the shared pins out of the GPIO matrix / hand them back to the peripheral. Called
 * automatically around a read; also called by epd_spi_init() so the transport does not
 * depend on call order. */
void epd_hw_spi_detach_pins(void);
void epd_hw_spi_reattach_pins(void);

/* Write `n` bytes from a constant byte, avoiding a staging buffer for bulk fills. */
void epd_hw_spi_write_fill(uint8_t value, size_t n);
