import { describe, it, expect } from 'vitest';
import { readFileSync, existsSync } from 'node:fs';
import { FB_BYTES, createBitmap, setPx, getPx, PANEL_PITCH } from '../src/canvas/bitmap';
import {
  renderPage,
  buildStaticLayer,
  fontMeasure,
  fontAdvance,
  type ValueField,
} from '../src/canvas/render';
import { BODY, VALUE, FONT_BODY, FONT_VALUE } from '../src/canvas/atlas-data';

describe('1-bit bitmap (HW-6)', () => {
  it('has the exact panel size', () => {
    expect(createBitmap().data.length).toBe(FB_BYTES);
    expect(FB_BYTES).toBe(78_200);
    expect(PANEL_PITCH).toBe(115);
  });

  it('starts all-white', () => {
    expect(createBitmap().data.every((v) => v === 0xff)).toBe(true);
  });

  it('writes MSB-first, 1 = white and 0 = black', () => {
    const b = createBitmap();
    setPx(b, 0, 0, true); /* black => CLEAR the MSB of byte 0 */
    expect(b.data[0]).toBe(0x7f);
    expect(getPx(b, 0, 0)).toBe(true);
    expect(getPx(b, 1, 0)).toBe(false);
  });

  it('puts the byte at the right offset for a mid-row pixel', () => {
    /* x=8 is the MSB of byte 1 on row 0 — catches a pitch or shift error. */
    const b = createBitmap();
    setPx(b, 8, 0, true);
    expect(b.data[0]).toBe(0xff);
    expect(b.data[1]).toBe(0x7f);
  });

  /* Out-of-range writes are IGNORED, not clamped — a glyph half off the panel must be
   * clipped, not squashed against the edge (canvas_set_px behaves this way). */
  it('ignores out-of-range pixels', () => {
    const b = createBitmap();
    const before = b.data.slice();
    setPx(b, -1, 5, true);
    setPx(b, b.width, 5, true);
    setPx(b, 5, -1, true);
    setPx(b, 5, b.height, true);
    expect(b.data).toEqual(before);
  });
});

/* The atlases are generated from the firmware's own headers, and these checks would catch a
 * generator that quietly lost or reordered glyphs. */
describe('font atlas', () => {
  it('has all 95 printable ASCII glyphs in each face', () => {
    expect(BODY.glyphs).toHaveLength(95);
    expect(VALUE.glyphs).toHaveLength(95);
    expect(BODY.firstChar).toBe(32);
  });

  it('reports the firmware metrics', () => {
    expect(BODY.ascent).toBe(20);
    expect(BODY.lineHeight).toBe(25);
    expect(VALUE.ascent).toBe(62);
    expect(VALUE.lineHeight).toBe(78);
  });

  it('measures a string as the sum of advances, with a constant height', () => {
    const m = fontMeasure(FONT_VALUE, '68.4');
    expect(m).not.toBeNull();
    /* 6,8,'.',4 are all in the value face; the height is the LINE height, not the ink height,
     * so it does not change with the string's contents. */
    expect(m!.h).toBe(VALUE.lineHeight);
    const sum = ['6', '8', '.', '4'].reduce((n, c) => n + fontAdvance(FONT_VALUE, c), 0);
    expect(m!.w).toBe(sum);
    expect(fontMeasure(FONT_VALUE, '1')!.h).toBe(m!.h);
  });

  it('fails rather than guessing for a character with no glyph', () => {
    expect(fontMeasure(FONT_VALUE, '£')).toBeNull();
  });

  it('gives a space a non-zero advance but no ink', () => {
    const sp = BODY.glyphs[' '.charCodeAt(0) - 32]!;
    expect(sp.advance).toBeGreaterThan(0);
    expect(sp.w).toBe(0);
    expect(sp.h).toBe(0);
  });
});

/* THE TEST THIS WHOLE MODULE EXISTS FOR.
 *
 * The device produced golden-default.bin with its own renderer, in C, from the layout in
 * golden-default.json. If this reproduces it byte for byte, the preview cannot lie about
 * what the panel shows (NFR-4). If it does not, ONE OF THE TWO RENDERERS IS WRONG — fix the
 * code, never the fixture. */
describe('renderer vs the firmware golden image (NFR-4, NFR-9)', () => {
  const goldenBin = 'test/fixtures/golden-default.bin';
  const goldenJson = 'test/fixtures/golden-default.json';

  function load() {
    return {
      golden: new Uint8Array(readFileSync(goldenBin)),
      layout: JSON.parse(readFileSync(goldenJson, 'utf8')),
    };
  }

  it('has the fixture available', () => {
    expect(existsSync(goldenBin)).toBe(true);
    expect(existsSync(goldenJson)).toBe(true);
  });

  it('matches the firmware golden image byte-for-byte', () => {
    const { golden, layout } = load();

    const staticLayer = buildStaticLayer(layout.labels, layout.rules).data;

    const fields: ValueField[] = layout.fields.map(
      (f: { x: number; y: number; w: number; h: number; alignH: string; alignV: string; font: number }) => ({
        x: f.x,
        y: f.y,
        w: f.w,
        h: f.h,
        alignH: f.alignH,
        alignV: f.alignV,
        fontId: f.font,
      }),
    );
    const values = layout.fields.map((f: { sample: string }) => f.sample);

    const out = renderPage(staticLayer, fields, values);
    expect(out.data.length).toBe(FB_BYTES);

    /* Locate the FIRST differing byte so a failure is debuggable rather than "not equal". */
    if (Buffer.compare(Buffer.from(out.data), Buffer.from(golden)) !== 0) {
      let first = -1;
      for (let i = 0; i < FB_BYTES; i++) {
        if (out.data[i] !== golden[i]) { first = i; break; }
      }
      const y = Math.floor(first / PANEL_PITCH);
      const x = (first % PANEL_PITCH) * 8;
      throw new Error(
        `first difference at byte ${first} (x=${x}..${x + 7}, y=${y}): ` +
        `web=0x${out.data[first]!.toString(16)} golden=0x${golden[first]!.toString(16)}`,
      );
    }
  });
});
