#include "epd_encode.h"

/* One input nibble -> ONE output byte. Nibble bit (3-b) sets output bits
 * (2*(3-b)) and (2*(3-b)+1), reproducing the vendor demo's inner loop:
 *   for k in 0..3: if MSB set, temp3 |= 0x03; if k<=2, temp3 <<= 2
 * Verified exhaustively against the vendor reference for all 256 byte values. */
static uint8_t emit_nibble(uint8_t n)
{
    uint8_t r = 0;
    for (int b = 0; b < 4; b++) {
        if (n & (0x8u >> b)) {
            r |= (uint8_t)(0x3u << (2 * (3 - b)));
        }
    }
    return r;
}

void epd_expand_1to2(const uint8_t *in, size_t n_bytes, uint8_t *out)
{
    for (size_t i = 0; i < n_bytes; i++) {
        out[2 * i]     = emit_nibble((uint8_t)((in[i] >> 4) & 0x0F));
        out[2 * i + 1] = emit_nibble((uint8_t)(in[i] & 0x0F));
    }
}

int epd_lut_value_for_temp(int temp_c)
{
    if (temp_c <= 5)   return 232;
    if (temp_c <= 10)  return 235;
    if (temp_c <= 20)  return 238;
    if (temp_c <= 30)  return 241;
    if (temp_c <= 127) return 244;
    return 232;
}

/* Port of the vendor demo's bitInterleave() (Display_EPD_W21.cpp:351-368). It emits the
 * interleaved result for one byte PAIR as two wire bytes (high byte first), so one call
 * consumes one byte of each input and produces exactly two output bytes — the same 1:2
 * ratio as epd_expand_1to2, which is why the loop below matches its shape.
 *
 * Bit layout per bit position i (0 = MSB): prev lands on odd output bits, next on even.
 * Verified exhaustively against the vendor function over all 65,536 byte pairs. */
static void emit_interleaved(uint8_t prev, uint8_t next, uint8_t *out)
{
    uint16_t r = 0;
    for (int i = 0; i < 8; i++) {
        r |= (uint16_t)(((prev >> (7 - i)) & 1u) << (2 * (7 - i) + 1));
        r |= (uint16_t)(((next >> (7 - i)) & 1u) << (2 * (7 - i)));
    }
    out[0] = (uint8_t)(r >> 8);
    out[1] = (uint8_t)r;
}

void epd_interleave_1to2(const uint8_t *prev1bpp, const uint8_t *next1bpp,
                         size_t n_bytes, uint8_t *out)
{
    for (size_t i = 0; i < n_bytes; i++) {
        emit_interleaved(prev1bpp[i], next1bpp[i], &out[2 * i]);
    }
}

/* See epd_encode.h for the contract these two serve. They are trivial arithmetic; they live here
 * because the walk they describe is the part of the banded transmit that is easy to get subtly
 * wrong (an off-by-one leaves a stripe of the previous picture on the glass, which is exactly the
 * kind of defect that "reports success" and is only caught by looking at the panel). */
int epd_band_of_row(int y, int band_rows, int height)
{
    if (band_rows <= 0 || y < 0 || y >= height) return -1;
    return y / band_rows;
}

int epd_band_rows_at(int band_index, int band_rows, int height)
{
    if (band_rows <= 0 || band_index < 0) return -1;
    const int y0 = band_index * band_rows;
    if (y0 >= height) return -1;
    const int remaining = height - y0;
    return (remaining < band_rows) ? remaining : band_rows;
}
