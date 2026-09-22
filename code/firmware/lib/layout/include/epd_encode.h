#pragma once
#include <stddef.h>
#include <stdint.h>

/* Expand 1 bpp framebuffer data to the SSD2677's 2 bpp (4-grey) wire format,
 * matching the vendor demo's EPD_W21_WriteDATA_1To2() exactly. One input byte
 * becomes exactly TWO output bytes (high nibble first, then low).
 * `out` MUST have room for 2 * n_bytes. */
void epd_expand_1to2(const uint8_t *in, size_t n_bytes, uint8_t *out);

/* Partial updates do NOT use epd_expand_1to2(). The vendor demo's
 * PIC_display_Part_ALL(prev, next) bit-interleaves TWO 1bpp framebuffers —
 * the previous displayed frame and the new one — because the SSD2677 derives
 * each pixel's transition from that pair. `prev` must therefore be the frame
 * currently on the glass, not the new frame. `out` MUST have room for 2*n_bytes.
 * Matches the vendor's bitInterleave() exactly. */
void epd_interleave_1to2(const uint8_t *prev1bpp, const uint8_t *next1bpp,
                         size_t n_bytes, uint8_t *out);

/* Temperature-compensated waveform selector, verbatim from the vendor demo's
 * Write_LUT_All(). Values are the raw bytes written to register 0xE6. */
int epd_lut_value_for_temp(int temp_c);

/* ---- the banded transmit walk (HW-6 / FR-11) ----
 *
 * The driver streams a frame to the panel in horizontal bands so that the previous and the next
 * frame of a PARTIAL refresh can each be composed a few dozen rows at a time, instead of both
 * whole frames being resident at once (which this part's DRAM cannot hold — see epd_write_frame_
 * banded). These two functions are the pure part of that walk, kept in this library rather than in
 * epd.c so the contract can be host-tested: epd.c talks to the SPI peripheral and cannot be.
 *
 * THE CONTRACT, and the reason it is worth a test:
 *   - every row of the panel is visited EXACTLY ONCE (a gap leaves a band of the old picture on the
 *     glass; a repeat shifts every later row and scrambles the image);
 *   - the bands come out in DESCENDING index order, because the panel scans bottom-up and the first
 *     rows transmitted land at the bottom of the glass;
 *   - a band is requested once and then used for all its rows, so the caller composes each band
 *     exactly once. */

/* Which band natural (top-down) row `y` belongs to, given `band_rows` rows per band and `height`
 * rows of panel. */
int epd_band_of_row(int y, int band_rows, int height);

/* How many rows band `band_index` holds. The LAST band is short unless band_rows divides the
 * height exactly, and the driver refuses a provider that reports any other count — so a caller that
 * guesses `band_rows` for every band would have its final band rejected. */
int epd_band_rows_at(int band_index, int band_rows, int height);
