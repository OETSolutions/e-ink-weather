#include "gt30_addr.h"

uint32_t gt30_ascii_addr(uint32_t base, char c)
{
    unsigned char u = (unsigned char)c;
    if (u < 0x20 || u > 0x7E) return GT30_ADDR_INVALID;
    return base + (uint32_t)(u - 0x20) * GT30_GLYPH_BYTES_8X16;
}
