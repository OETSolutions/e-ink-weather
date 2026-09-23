/**
 * The 1 bpp framebuffer primitives, and the fractional-coordinate trap.
 *
 * WHY THIS FILE EXISTS. `setPx` computes its byte index as `y * pitch + (x >> 3)`. With a
 * FRACTIONAL y that is not an integer, and a typed-array store to a non-integer index is a silent
 * NO-OP — no exception, no warning, just a pixel that was never written. Every value box whose
 * geometry carried a fraction therefore rendered as an empty box, and the user saw the reading
 * disappear from the preview when they resized or dragged a widget. The firmware truncates these
 * coordinates to int, so the GLASS was correct; only the preview was blank, which made it look
 * like a preview bug rather than a coordinate bug.
 *
 * Note that x was safe by accident: `x >> 3` truncates. y had no such protection, so the failure
 * looked position-dependent — reported as "the values disappear depending on where you place them".
 *
 * The fix is to floor both axes on the way in, which is exactly what the device's (int) cast does.
 */

import { describe, it, expect } from 'vitest';
import { createBitmap, setPx, getPx, PANEL_PITCH } from '../src/canvas/bitmap';

describe('setPx / getPx rounding', () => {
  it('writes a pixel at a fractional coordinate instead of dropping it', () => {
    const b = createBitmap();
    setPx(b, 100.4, 100.6, true);
    /* The whole point: the ink must actually land. Before the fix this box drew nothing. */
    expect(getPx(b, 100, 100)).toBe(true);
  });

  it('floors toward the same pixel the device would use', () => {
    /* The firmware parses x/y with (int)valuedouble, which truncates. For the non-negative
     * coordinates a box can have, floor and trunc are identical — so flooring is what makes the
     * preview agree with the glass. */
    const b = createBitmap();
    setPx(b, 200.99, 300.99, true);
    expect(getPx(b, 200, 300)).toBe(true);
    expect(getPx(b, 201, 301)).toBe(false);
  });

  it('does not crash reading a fractional coordinate', () => {
    /* getPx had the same flaw and threw rather than returning false: `b.data[NaN]` is undefined,
     * so the bitwise AND raised. A read that crashes on input a write ignores is worse than the
     * write, because it takes the whole render down. */
    const b = createBitmap();
    expect(() => getPx(b, 10.5, 20.5)).not.toThrow();
    expect(getPx(b, 10.5, 20.5)).toBe(false);
  });

  it('round-trips a whole row of fractional writes', () => {
    const b = createBitmap();
    for (let x = 40; x < 80; x++) setPx(b, x + 0.37, 500.61, true);
    for (let x = 40; x < 80; x++) expect(getPx(b, x, 500)).toBe(true);
  });

  it('still ignores out-of-range coordinates rather than clamping them', () => {
    /* Clamping would squash a partly-off-panel glyph against the edge; the renderer relies on
     * these being dropped so a glyph is CLIPPED at the panel boundary. */
    const b = createBitmap();
    setPx(b, -1.5, 10, true);
    setPx(b, 920.5, 10, true);
    setPx(b, 10, -0.5, true);
    setPx(b, 10, 680.5, true);
    expect(getPx(b, -1, 10)).toBe(false);
    expect(getPx(b, 920, 10)).toBe(false);
    /* And a valid pixel is untouched by the rejected writes. */
    setPx(b, 10, 10, true);
    expect(getPx(b, 10, 10)).toBe(true);
  });

  it('keeps the pitch and index arithmetic intact for a whole coordinate', () => {
    /* A guard against "fixing" the rounding by breaking the index: a known pixel must land in the
     * byte the format says it does. */
    const b = createBitmap();
    setPx(b, 8, 1, true);
    const idx = 1 * PANEL_PITCH + 1;      /* x=8 -> byte 1, bit 0 (MSB first) */
    expect(b.data[idx]! & 0x80).toBe(0);  /* black is a CLEAR bit */
  });
});

describe('renderPage is total over fractional geometry', () => {
  it('draws a value in a box with a fractional y', async () => {
    const { renderPage } = await import('../src/canvas/render');
    const blank = new Uint8Array((920 * 680) / 8).fill(0xff);
    const field = { x: 40, y: 64.5, w: 360, h: 120, alignH: 'L' as const, alignV: 'T' as const,
                    fontId: 1, kind: 't' as const };
    const withValue = renderPage(blank, [field], ['12345']).data;
    const without = renderPage(blank, [field], [undefined]).data;
    let ink = 0;
    for (let i = 0; i < withValue.length; i++) {
      /* black = a bit CLEARED by the value pass */
      ink += popcount((~withValue[i]! & 0xff) & without[i]!);
    }
    expect(ink).toBeGreaterThan(0);
  });
});

function popcount(b: number): number {
  let n = 0;
  while (b) { n += b & 1; b >>= 1; }
  return n;
}
