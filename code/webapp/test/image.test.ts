/**
 * Tests for turning a picture into panel ink.
 *
 * WHAT THESE PROTECT: the dither is the only step that decides WHAT the picture looks like on the
 * glass, and it is pure — pixels in, ink out — so it can be checked exactly here rather than by
 * squinting at the device. The failure mode this guards is not a crash: a wrong luminance weight
 * or an inverted threshold produces a picture that still "works" and simply looks wrong, which is
 * the kind of bug that survives every smoke test.
 */

import { describe, it, expect } from 'vitest';
import { ditherToPatch, fitImageIntoBox, clampPlacement, MAX_IMAGE_PIXELS } from '../src/canvas/image';

/** Build RGBA pixels from a per-pixel grey function. */
function grey(w: number, h: number, f: (x: number, y: number) => number): Uint8ClampedArray {
  const px = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const v = f(x, y);
      const p = (y * w + x) * 4;
      px[p] = v; px[p + 1] = v; px[p + 2] = v; px[p + 3] = 255;
    }
  }
  return px;
}

/** Count set bits (ink) in a patch. */
function inkCount(im: { ink: Uint8Array }): number {
  let n = 0;
  for (const b of im.ink) {
    for (let i = 0; i < 8; i++) if ((b >> i) & 1) n++;
  }
  return n;
}

describe('ditherToPatch', () => {
  it('produces a patch of exactly the requested size with the right pitch', () => {
    const im = ditherToPatch(grey(10, 6, () => 128), 10, 6);
    expect(im.w).toBe(10);
    expect(im.h).toBe(6);
    /* pitch is ceil(w/8): 10 bits needs 2 bytes. A pitch of w/8 would silently drop the last
     * two columns of every row. */
    expect(im.pitch).toBe(2);
    expect(im.ink.length).toBe(2 * 6);
  });

  /* WHITE MUST COME OUT BLANK. The most basic failure — an inverted polarity — would turn a
   * white-background logo into a black rectangle, which is the exact bug the white-flatten step in
   * rasterizeImage() exists to prevent elsewhere. */
  it('leaves white blank and fills black', () => {
    expect(inkCount(ditherToPatch(grey(16, 8, () => 255), 16, 8))).toBe(0);
    expect(inkCount(ditherToPatch(grey(16, 8, () => 0), 16, 8))).toBe(16 * 8);
  });

  /* MID-GREY MUST BECOME A MIX, NOT A FLAT FIELD. This is the whole point of dithering: a plain
   * threshold would make 128 an arbitrary all-black or all-white, and a photograph would lose
   * every mid-tone. A correctly diffused 50% grey lands near half the pixels inked. */
  it('turns mid-grey into roughly half ink', () => {
    const im = ditherToPatch(grey(64, 64, () => 128), 64, 64);
    const frac = inkCount(im) / (64 * 64);
    /* Generous bounds: the exact fraction depends on the serpentine of the error, and pinning it
     * to a number would make the test brittle against a legitimate change in the kernel. What must
     * NOT happen is 0% or 100%. */
    expect(frac).toBeGreaterThan(0.4);
    expect(frac).toBeLessThan(0.6);
  });

  /* A GRADIENT MUST BE MONOTONIC IN INK. This is what catches a wrong sign or a mis-scaled error:
   * a darker strip must never print lighter than a lighter one. */
  it('inks a gradient in proportion to its darkness', () => {
    const dark = inkCount(ditherToPatch(grey(64, 32, () => 60), 64, 32));
    const mid = inkCount(ditherToPatch(grey(64, 32, () => 128), 64, 32));
    const light = inkCount(ditherToPatch(grey(64, 32, () => 200), 64, 32));
    expect(dark).toBeGreaterThan(mid);
    expect(mid).toBeGreaterThan(light);
  });

  /* LUMINANCE IS WEIGHTED, NOT AVERAGED. Green carries most of the perceived brightness, so a
   * fully green pixel must print LIGHTER than a fully blue one of the same channel value — an
   * unweighted average would make them identical and a photo would lose its tonal structure. */
  it('weights green as brighter than blue', () => {
    const mk = (r: number, g: number, b: number): Uint8ClampedArray => {
      const px = new Uint8ClampedArray(16 * 16 * 4);
      for (let p = 0; p < px.length; p += 4) {
        px[p] = r; px[p + 1] = g; px[p + 2] = b; px[p + 3] = 255;
      }
      return px;
    };
    const pureGreen = inkCount(ditherToPatch(mk(0, 255, 0), 16, 16));
    const pureBlue = inkCount(ditherToPatch(mk(0, 0, 255), 16, 16));
    /* Green is luminance 150 -> lighter -> LESS ink. Blue is 29 -> darker -> MORE ink. */
    expect(pureGreen).toBeLessThan(pureBlue);
  });

  /* THE ERROR MUST NOT ESCAPE THE PATCH. Diffusion off the right edge would otherwise wrap into
   * the next row's start (a Float32Array indexed flat), which would print a bright or dark streak
   * down the left edge — a subtle, easily-missed artifact. Drawn into the LEFT half only, the
   * right half must be untouched white. */
  it('does not leak dither error across the patch edge', () => {
    const im = ditherToPatch(grey(32, 16, (x) => (x < 16 ? 100 : 255)), 32, 16);
    for (let y = 0; y < 16; y++) {
      for (let x = 16; x < 32; x++) {
        const bit = (im.ink[y * im.pitch + (x >> 3)]! >> (7 - (x & 7))) & 1;
        expect(bit, `pixel (${x},${y}) should be white`).toBe(0);
      }
    }
  });
});

describe('fitImageIntoBox', () => {
  /* FIT IS WHAT AVOIDS DISTORTION. A 4:3 photo in a square box must letterbox, not stretch, and
   * the fit is computed here rather than by the picture being drawn twice. */
  it('fits a wide picture to the box width', () => {
    expect(fitImageIntoBox(400, 300, 200, 200)).toEqual({ w: 200, h: 150 });
  });

  it('fits a tall picture to the box height', () => {
    expect(fitImageIntoBox(300, 400, 200, 200)).toEqual({ w: 150, h: 200 });
  });

  it('is a no-op for a picture already the box shape', () => {
    expect(fitImageIntoBox(100, 50, 300, 150)).toEqual({ w: 300, h: 150 });
  });

  /* A degenerate size must not produce a zero-dimension patch — a 0-wide patch would blit nothing
   * and the box would silently stay blank. */
  it('never returns a zero dimension', () => {
    const f = fitImageIntoBox(1000, 1, 10, 10);
    expect(f.w).toBeGreaterThan(0);
    expect(f.h).toBeGreaterThan(0);
  });

  it('returns zero for a degenerate input rather than throwing', () => {
    expect(fitImageIntoBox(0, 100, 50, 50)).toEqual({ w: 0, h: 0 });
    expect(fitImageIntoBox(100, 100, 0, 50)).toEqual({ w: 0, h: 0 });
  });
});

describe('clampPlacement', () => {
  it('rounds to whole pixels and keeps the box on the panel', () => {
    expect(clampPlacement(10.6, 20.4, 100.5, 50.5)).toEqual({ x: 11, y: 20, w: 101, h: 51 });
  });

  /* A box hanging off the edge must be SHRUNK to fit, not left hanging: the device clamps the same
   * way, and a box the editor shows must be the box the device stores. */
  it('shrinks a box that runs off the panel', () => {
    const c = clampPlacement(900, 660, 200, 200);
    expect(c.x + c.w).toBeLessThanOrEqual(920);
    expect(c.y + c.h).toBeLessThanOrEqual(680);
    expect(c.w).toBe(20);
    expect(c.h).toBe(20);
  });

  it('never produces a zero-size box', () => {
    const c = clampPlacement(919, 679, 0, 0);
    expect(c.w).toBeGreaterThan(0);
    expect(c.h).toBeGreaterThan(0);
  });

  it('pulls a negative origin to the panel edge', () => {
    const c = clampPlacement(-50, -50, 100, 100);
    expect(c.x).toBe(0);
    expect(c.y).toBe(0);
    expect(c.w).toBe(100);
    expect(c.h).toBe(100);
  });
});

describe('the size limit', () => {
  /* The limit exists to give the user a sentence rather than an artwork-upload failure at Save, so
   * it must be far above the panel's own 920x680 and still finite. */
  it('is far larger than the panel but not unbounded', () => {
    expect(MAX_IMAGE_PIXELS).toBeGreaterThan(920 * 680);
    expect(Number.isFinite(MAX_IMAGE_PIXELS)).toBe(true);
  });
});
