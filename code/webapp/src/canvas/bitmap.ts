/**
 * The 1 bpp panel framebuffer (HW-6), matching the firmware's canvas.c bit for bit.
 *
 * CONVENTION: 1 = white, 0 = black, MSB-first within each byte, row-major, pitch 115.
 * This is the vendor's own 1 bpp layout, and it is the OPPOSITE of the font atlases (where a
 * set bit is ink). Both conventions are real and both are load-bearing, so the inversion
 * happens in exactly one place — `blitGlyph` in render.ts — and nowhere else.
 */

export const PANEL_WIDTH = 920;
export const PANEL_HEIGHT = 680;
export const PANEL_PITCH = PANEL_WIDTH / 8; /* 115 bytes per row */
export const FB_BYTES = PANEL_PITCH * PANEL_HEIGHT; /* 78,200 */

export interface Bitmap {
  data: Uint8Array;
  width: number;
  height: number;
  pitch: number;
}

/** A framebuffer initialised to WHITE, like canvas_fill(c, 0) on the device. */
export function createBitmap(): Bitmap {
  const data = new Uint8Array(FB_BYTES);
  data.fill(0xff);
  return { data, width: PANEL_WIDTH, height: PANEL_HEIGHT, pitch: PANEL_PITCH };
}

/**
 * Set one pixel. `black` true clears the bit; false sets it.
 *
 * Out-of-range coordinates are IGNORED rather than clamped, matching canvas_set_px(). That
 * matters: a glyph or widget partially off-panel must be clipped, not squashed against the
 * edge, which is what clamping would do.
 *
 * COORDINATES ARE FLOORED, AND THAT IS LOAD-BEARING. The index is `y * pitch + (x >> 3)`, and a
 * FRACTIONAL y makes that a non-integer — `b.data[1234.5]` is a property lookup that does not
 * exist, so the store is silently DISCARDED. A widget whose geometry carried a fraction (the
 * editor's resize maths divides a pointer delta by a fractional display scale) therefore drew
 * NOTHING at all: the value vanished from the preview the moment a box was resized, with no error
 * anywhere. Reported as "the values disappear depending on where you place them".
 *
 * Flooring matches what the DEVICE does — the firmware parses x/y/w/h with (int)valuedouble, which
 * truncates toward zero and is identical to floor for the non-negative coordinates a box can have.
 * So the preview and the glass now agree for a fractional box instead of the preview showing a
 * hole where the panel shows a reading. `x >> 3` already truncates, which is why only y bit the
 * caller: the same expression was half-protected by an operator that floors by nature. */
export function setPx(b: Bitmap, x: number, y: number, black: boolean): void {
  x = Math.floor(x);
  y = Math.floor(y);
  if (x < 0 || y < 0 || x >= b.width || y >= b.height) return;
  const idx = y * b.pitch + (x >> 3);
  const mask = 0x80 >> (x & 7);
  if (black) b.data[idx]! &= ~mask & 0xff;
  else b.data[idx]! |= mask;
}

/** Read one pixel. Returns true for black (a CLEAR bit), like canvas_get_px().
 *
 * Floored for the same reason as setPx: without it a fractional y indexes past the end of the
 * typed array and reads `undefined`, so `b.data[idx] & mask` throws rather than reporting a
 * pixel — a read that crashes on input a write quietly ignores. */
export function getPx(b: Bitmap, x: number, y: number): boolean {
  x = Math.floor(x);
  y = Math.floor(y);
  if (x < 0 || y < 0 || x >= b.width || y >= b.height) return false;
  const idx = y * b.pitch + (x >> 3);
  const mask = 0x80 >> (x & 7);
  return (b.data[idx]! & mask) === 0;
}

/**
 * Blit a 1 bpp source, skipping WHITE source pixels (`transparent`).
 *
 * The device's canvas_blit_1bpp_masked() with transparent_white=1 does the same, and it is
 * what lets a glyph land on top of the static art without punching a white rectangle through
 * it. Here the source is a GLYPH ATLAS, so a set source bit is ink and must become a BLACK
 * framebuffer pixel — the inversion described at the top of this file.
 */
export function blitMasked(
  dst: Bitmap,
  src: Uint8Array,
  x: number,
  y: number,
  w: number,
  h: number,
): void {
  const pitch = (w + 7) >> 3;
  for (let sy = 0; sy < h; sy++) {
    const row = sy * pitch;
    for (let sx = 0; sx < w; sx++) {
      const byte = src[row + (sx >> 3)];
      if (byte === undefined) continue;
      const ink = (byte & (0x80 >> (sx & 7))) !== 0;
      if (!ink) continue; /* atlas bit clear = not ink: leave the destination alone */
      setPx(dst, x + sx, y + sy, true);
    }
  }
}
