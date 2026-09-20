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
 */
export function setPx(b: Bitmap, x: number, y: number, black: boolean): void {
  if (x < 0 || y < 0 || x >= b.width || y >= b.height) return;
  const idx = y * b.pitch + (x >> 3);
  const mask = 0x80 >> (x & 7);
  if (black) b.data[idx]! &= ~mask & 0xff;
  else b.data[idx]! |= mask;
}

/** Read one pixel. Returns true for black (a CLEAR bit), like canvas_get_px(). */
export function getPx(b: Bitmap, x: number, y: number): boolean {
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
