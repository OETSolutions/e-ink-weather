#pragma once
#include <stdint.h>

/* Address map of the GT30L32S4W (Genitop) font ROM, taken verbatim from LibDriver's
 * `driver_gt30l32s4w.h`. The chip is 32 Mbit = 4 MB = 0x400000, so every base below
 * fits with room to spare. Only the 8x16 ASCII face is listed: it is the one the probe
 * reads, and transcribing the rest would be unused constants. */
#define GT30_ROM_BYTES          0x400000u
#define GT30_ADDR_8X16_ASCII    0x1DD780u

/* Glyphs are fixed-width and packed in code-point order starting at the base, so a
 * character's address is base + (c - 0x20) * <bytes per glyph>. LibDriver's own
 * `gt30l32s4w_init()` self-test computes the 'A' address exactly this way. */
#define GT30_GLYPH_BYTES_8X16   16u

#define GT30_ADDR_INVALID       0xFFFFFFFFu

/* Returns GT30_ADDR_INVALID for characters outside the font's printable range
 * (0x20..0x7E). This is a pure function precisely so the arithmetic can be pinned by
 * a host test: a wrong address makes the chip read back the wrong glyph, which is
 * indistinguishable at runtime from the chip being absent — the exact failure that
 * would silently disable the font path forever. */
uint32_t gt30_ascii_addr(uint32_t base, char c);
