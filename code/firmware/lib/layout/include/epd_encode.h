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
