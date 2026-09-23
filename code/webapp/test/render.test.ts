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
import { BODY, VALUE, FONT_BODY, FONT_VALUE, FACES, FACE_PX } from '../src/canvas/atlas-data';
import { faceIdForPx } from '../src/canvas/face';

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
      (f: {
        x: number;
        y: number;
        w: number;
        h: number;
        alignH: string;
        alignV: string;
        font: number;
        kind: 't' | 'i';
      }) => ({
        x: f.x,
        y: f.y,
        w: f.w,
        h: f.h,
        alignH: f.alignH,
        alignV: f.alignV,
        /* The fixture carries a PIXEL SIZE, and the renderer takes a face ID — so resolve it
         * with the same shared rule the firmware's golden header used (face_of_px, which
         * mirrors font_face_for_px). Passing the size straight through as an id is what made
         * this test draw nothing: 20 is not a valid face index on a ten-face ladder. */
        fontId: faceIdForPx(f.font),
        /* 't' is text, 'i' is a weather icon. Dropping this made the test render the icon
         * code "04d" as TEXT while the firmware golden drew the picture — a mismatch that
         * looked like an icon bug but was the cross-check silently testing a different
         * thing than the firmware. The fixture has carried `kind` since icons landed. */
        kind: f.kind,
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

/* THE DEGREE SIGN. Without it every imperial temperature renders NOTHING — font_measure()
 * fails on "68.4°F" and the renderer draws no glyph at all, so the panel would be blank where
 * the temperature belongs. This is the primary use case, so it is locked here. */
describe('the degree sign (U+00B0)', () => {
  it('has a glyph in both faces', () => {
    expect(BODY.extra.get(0x00b0)).toBeDefined();
    expect(VALUE.extra.get(0x00b0)).toBeDefined();
    expect(BODY.extra.get(0x00b0)!.advance).toBeGreaterThan(0);
    expect(VALUE.extra.get(0x00b0)!.advance).toBeGreaterThan(0);
  });

  it('measures an imperial temperature instead of failing', () => {
    const m = fontMeasure(FONT_VALUE, '68.4°F');
    expect(m).not.toBeNull();
    /* And the width must include the degree's advance, not silently skip it. */
    const without = fontMeasure(FONT_VALUE, '68.4F')!;
    expect(m!.w).toBeGreaterThan(without.w);
  });

  it('draws ink, so a temperature is not blank on the panel', () => {
    const withDeg = renderPage(blank(), [field()], ['68.4°F']);
    const without = renderPage(blank(), [field()], ['68.4F']);
    const dark = (b: ReturnType<typeof renderPage>) =>
      b.data.reduce((n, v) => n + [7,6,5,4,3,2,1,0].filter((bit) => ((v >> bit) & 1) === 0).length, 0);
    expect(dark(withDeg)).toBeGreaterThan(0);
    /* The ° adds ink of its own, so the two images must differ. */
    expect(Buffer.compare(Buffer.from(withDeg.data), Buffer.from(without.data))).not.toBe(0);
  });

  it('still refuses a character that genuinely has no glyph', () => {
    expect(fontMeasure(FONT_VALUE, '68.4\u20ac')).toBeNull();  /* euro sign: not in the atlas */
  });
});

/* THE UPSCALED FACES (sizes above 128 px). They are k x k BLOCK SCALES of a rasterised face, so
 * text at 256 px must render as the 128 px text expanded, each pixel to a 2 x 2 block. The
 * firmware pins the same property in test_render_golden.c; keeping both means a divergence
 * between the preview and the panel is caught on whichever side drifts (NFR-4). */
describe('upscaled faces are exact block scales (FR-4a)', () => {
  const cases: { px: number; base: number; k: number }[] = [
    { px: 160, base: 80, k: 2 },
    { px: 192, base: 96, k: 2 },
    { px: 256, base: 128, k: 2 },
    { px: 320, base: 80, k: 4 },
    { px: 384, base: 128, k: 3 },
    { px: 512, base: 128, k: 4 },
  ];

  it('reaches 512, and every upscaled size is base * k', () => {
    expect(FACE_PX[FACE_PX.length - 1]).toBe(512);
    for (const { px, base, k } of cases) {
      const id = faceIdForPx(px);
      expect(FACES[id]!.px).toBe(px);
      expect(FACES[id]!.upscale).toBe(k);
      expect(FACES[id]!.upscaleFromPx).toBe(base);
      expect(FACES[id]!.px).toBe(FACES[id]!.upscaleFromPx * FACES[id]!.upscale);
    }
  });

  it('scales the reported measurements by k, not the base', () => {
    const base = fontMeasure(faceIdForPx(128), 'Hg')!;
    const up = fontMeasure(faceIdForPx(256), 'Hg')!;
    expect(up.w).toBe(base.w * 2);
    expect(up.h).toBe(base.h * 2);
    expect(fontAdvance(faceIdForPx(512), '8')).toBe(fontAdvance(faceIdForPx(128), '8') * 4);
  });

  for (const { px, base, k } of cases) {
    it(`draws ${px}px as ${base}px scaled ${k}x, pixel for pixel`, () => {
      const ox = 20, oy = 20, w = 860, h = 600;
      const mk = (pxSize: number): ValueField =>
        ({ x: ox, y: oy, w, h, alignH: 'L', alignV: 'T', fontId: faceIdForPx(pxSize) });
      const up = renderPage(blank(), [mk(px)], ['Hg']);
      const baseBmp = renderPage(blank(), [mk(base)], ['Hg']);

      /* The block expansion is anchored at the BOX origin: the box geometry is unscaled, so an
       * up-pixel at (x,y) corresponds to the base pixel at (ox + (x-ox)/k, oy + (y-oy)/k). */
      let mismatches = 0;
      for (let y = oy; y < oy + h && mismatches === 0; y++) {
        for (let x = ox; x < ox + w; x++) {
          const upInk = getPx(up, x, y);
          const baseInk = getPx(baseBmp, ox + Math.floor((x - ox) / k), oy + Math.floor((y - oy) / k));
          if (upInk !== baseInk) { mismatches++; break; }
        }
      }
      expect(mismatches).toBe(0);
      /* It drew something \u2014 a no-op renderer would trivially match a blank base. */
      expect(up.data.some((v) => v !== 0xff)).toBe(true);
    });
  }
});

/* A HEADING ("label") IS NOT CLIPPED BY A FIXED BOX. Reported defect: "Resizing header text boxes
 * above 40 cut off and don't fit text." Labels are baked into the static layer through the same
 * drawField path, with a clip box that was hardcoded 500x40 — too small the moment the ladder grew
 * past the 40 px face (line height 49). A heading set at 48 px or larger lost its lower half, and a
 * wide heading lost its tail past x+500. The box is now the rest of the panel, so a heading is
 * clipped only by the panel edge. */
describe('headings are not clipped by a fixed box (reported defect)', () => {
  const inkIn = (b: ReturnType<typeof buildStaticLayer>, x0: number, y0: number, x1: number, y1: number) => {
    let n = 0;
    for (let y = y0; y < y1; y++) for (let x = x0; x < x1; x++) if (getPx(b, x, y)) n++;
    return n;
  };

  it('draws a large heading below the old 40 px cut', () => {
    for (const px of [40, 48, 64, 96, 128]) {
      const b = buildStaticLayer([{ x: 40, y: 20, text: 'OUTDOOR', font: px }], []);
      expect(inkIn(b, 40, 40, 400, 100), `a ${px}px heading lost ink below row 40`).toBeGreaterThan(0);
    }
  });

  it('keeps the tail of a heading wider than the old 500 px box', () => {
    const b = buildStaticLayer([{ x: 40, y: 20, text: 'HALLWAY TEMPERATURE NOW', font: 48 }], []);
    /* The old box ended at x = 40 + 500 = 540; ink past 560 proves the text is no longer cut. */
    expect(inkIn(b, 560, 20, 900, 100)).toBeGreaterThan(0);
  });
});

function blank(): Uint8Array {
  return new Uint8Array(FB_BYTES).fill(0xff);
}
function field() {
  return { x: 48, y: 76, w: 420, h: 110, alignH: 'L', alignV: 'T', fontId: FONT_VALUE };
}
