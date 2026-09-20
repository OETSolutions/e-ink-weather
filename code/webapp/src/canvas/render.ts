/**
 * The 1 bpp renderer, mirroring the firmware's lib/layout/src/render.c (NFR-4).
 *
 * WHY THIS MUST MATCH EXACTLY: the config app shows a live preview of the panel, and the
 * device composes the real frame with different code in a different language. If the two
 * disagree — a bearing off by one, a different baseline rule, a glyph placed by its ink box
 * instead of its baseline — the preview is a confident lie, and the user only finds out by
 * looking at the glass. The golden-image test in test/render.test.ts locks the two together.
 *
 * The composition order is the firmware's: blit the static layer, then stamp each field's
 * value on top. Values are painted as INK ONLY (never white), so a glyph box cannot punch a
 * rectangle out of the static art underneath it.
 */

import {
  type Bitmap,
  createBitmap,
  setPx,
} from './bitmap';
import { FACES, type Face, type Glyph } from './atlas-data';

/** A box on the static layer holding one dynamic string. Mirrors value_field_t. */
export interface ValueField {
  x: number;
  y: number;
  w: number;
  h: number;
  /** 'L' | 'C' | 'R' */
  alignH: string;
  /** 'T' | 'M' | 'B' */
  alignV: string;
  /** 0 = body, 1 = value. Mirrors font_id_t. */
  fontId: number;
}

function glyphOf(face: Face, ch: string): Glyph | undefined {
  const idx = ch.charCodeAt(0) - face.firstChar;
  if (idx < 0 || idx >= face.glyphs.length) return undefined;
  return face.glyphs[idx];
}

/** Advance width in pixels. Returns 0 for a character with no glyph, like font_advance(). */
export function fontAdvance(fontId: number, ch: string): number {
  const face = FACES[fontId];
  if (!face) return 0;
  return glyphOf(face, ch)?.advance ?? 0;
}

/**
 * Measure a string: the sum of advances, and the face's LINE HEIGHT (not the tallest glyph's
 * ink height).
 *
 * The height is constant per face on purpose — the firmware's comment explains why, and the
 * consequence of getting it wrong is visible: a line's height would depend on which
 * characters happen to be in it, so a temperature would shift vertically as its digits
 * changed between refreshes.
 *
 * Returns null when a character has no glyph, matching font_measure()'s failure. The caller
 * then draws nothing rather than guessing a width.
 */
export function fontMeasure(fontId: number, s: string): { w: number; h: number } | null {
  const face = FACES[fontId];
  if (!face) return null;
  let w = 0;
  for (const ch of s) {
    const g = glyphOf(face, ch);
    if (!g) return null;
    w += g.advance;
  }
  return { w, h: face.lineHeight };
}

/** Horizontal offset of the text within its box. Unrecognised align falls back to 'L'. */
function offsetH(a: string, boxW: number, textW: number): number {
  if (a === 'C') return Math.floor((boxW - textW) / 2);
  if (a === 'R') return boxW - textW;
  return 0;
}

/** Vertical offset of the line box within its box. Unrecognised falls back to 'T'. */
function offsetV(a: string, boxH: number, lineH: number): number {
  if (a === 'M') return Math.floor((boxH - lineH) / 2);
  if (a === 'B') return boxH - lineH;
  return 0;
}

/**
 * Stamp one glyph, clipped to the field box, painting only ink. Mirrors blit_glyph_clipped().
 *
 * CLIPPING TO THE BOX, not just to the panel, is what stops an over-long reading from
 * scribbling across the surrounding layout — a field that overflows is truncated.
 */
function blitGlyphClipped(
  dst: Bitmap,
  gx: number,
  gy: number,
  face: Face,
  g: Glyph,
  f: ValueField,
): void {
  if (g.w === 0 || g.h === 0) return; /* blank glyph (space): nothing to draw */
  const pitch = (g.w + 7) >> 3;
  const x0 = f.x;
  const x1 = f.x + f.w;
  const y0 = f.y;
  const y1 = f.y + f.h;

  for (let sy = 0; sy < g.h; sy++) {
    const dy = gy + sy;
    if (dy < y0 || dy >= y1) continue;
    const row = g.off + sy * pitch;
    for (let sx = 0; sx < g.w; sx++) {
      const byte = face.bits[row + (sx >> 3)];
      if (byte === undefined) continue;
      /* Atlas bit SET means ink; a clear bit is background and must be skipped, or the glyph
       * box would punch a white rectangle through the static layer beneath it. */
      if ((byte & (0x80 >> (sx & 7))) === 0) continue;
      const dx = gx + sx;
      if (dx < x0 || dx >= x1) continue;
      setPx(dst, dx, dy, true);
    }
  }
}

/** Draw one field's value. Mirrors draw_field(). */
function drawField(dst: Bitmap, f: ValueField, s: string | undefined): void {
  if (!s) return; /* nothing to show: leave the static layer intact */

  const face = FACES[f.fontId];
  /* An unknown font id is a BUG in the caller, not a rendering condition — the firmware
   * cannot hit this (font_id_t is an enum) and neither should we. Returning quietly is what
   * let a mis-spelled `font` key blank every label silently, with the only symptom being a
   * golden-image mismatch hundreds of bytes away. */
  if (!face) throw new Error(`unknown font id ${f.fontId} for text ${JSON.stringify(s)}`);
  const m = fontMeasure(f.fontId, s);
  if (!m) return; /* a character with no glyph: draw nothing, do not guess */

  /* The pen origin is the top-left of the LINE box; glyph ink is then placed relative to the
   * BASELINE via each glyph's bearing. Placing by ink box alone would float a '.' at the top
   * of the line. */
  let penX = f.x + offsetH(f.alignH, f.w, m.w);
  const penY = f.y + offsetV(f.alignV, f.h, m.h);
  const baseline = penY + face.ascent;

  for (const ch of s) {
    const g = glyphOf(face, ch);
    if (!g) break;
    blitGlyphClipped(dst, penX + g.bx, baseline + g.by, face, g, f);
    penX += g.advance;
  }
}

/**
 * Compose a page: the static layer, then the values stamped into their boxes.
 *
 * `fields` and `values` are parallel, exactly like render_compose(). A missing value (or one
 * the caller could not resolve) is skipped rather than drawn as empty — the firmware behaves
 * the same way, so a failed fetch leaves the static art intact rather than blanking it.
 */
export function renderPage(
  staticLayer: Uint8Array,
  fields: ValueField[],
  values: (string | undefined)[],
): Bitmap {
  const b = createBitmap();
  b.data.set(staticLayer.subarray(0, b.data.length));
  for (let i = 0; i < fields.length; i++) {
    drawField(b, fields[i]!, values[i]);
  }
  return b;
}

/* ------------------------------------------------------------------ static layer ---- */

/** Labels and chrome the web app bakes into the static layer.
 *
 * The property is `font`, not `fontId`, to match the `font` key the firmware's golden
 * generator emits for BOTH labels and fields — one spelling across the fixture, so a consumer
 * cannot read the right key for one and silently miss the other. (It did: renaming this to
 * `fontId` made every label's font undefined, and the labels simply never drew.) */
export interface StaticLabel {
  x: number;
  y: number;
  text: string;
  font: number;
}

/** A divider: full width, inset by `inset` on each side. */
export interface StaticRule {
  y: number;
  thickness: number;
  inset: number;
}

/** Draw a string as a value field — the same path the device uses for a dynamic value, and
 *  the one the firmware's own golden generator uses to bake labels. */
export function drawText(
  b: Bitmap,
  x: number,
  y: number,
  text: string,
  fontId: number,
  w = 500,
  h = 40,
): void {
  drawField(b, { x, y, w, h, alignH: 'L', alignV: 'T', fontId }, text);
}

/**
 * Build the static layer: white, with labels and rules. Mirrors golden_build_static_layer()
 * from the firmware's tools/golden/default_layout.h — deliberately, because the golden test
 * compares this exact image against the device's.
 */
export function buildStaticLayer(labels: StaticLabel[], rules: StaticRule[]): Bitmap {
  const b = createBitmap();
  for (const l of labels) {
    drawText(b, l.x, l.y, l.text, l.font);
  }
  for (const r of rules) {
    for (let t = 0; t < r.thickness; t++) {
      for (let x = r.inset; x < b.width - r.inset; x++) {
        setPx(b, x, r.y + t, true);
      }
    }
  }
  return b;
}
