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
import { faceIdForPx } from './face';
import { WEATHER_ICONS } from './weather-icons-data';
import { weatherIconIndex, WeatherIcon } from './weather-icons';

/** A box on the static layer holding one dynamic value. Mirrors value_field_t.
 *
 * `kind` mirrors the firmware's `value_field_t.kind`: 't' means the value is TEXT to set in
 * `fontId`; 'i' means the value is an OpenWeatherMap icon CODE to be drawn as a picture. It
 * travels with the box rather than with the widget because the renderer is handed fields and
 * values only — it never sees a binding. */
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
  /** 't' text (default), 'i' weather icon. */
  kind?: 't' | 'i';
}

/**
 * Decode a string into codepoints, ONE CHARACTER AT A TIME.
 *
 * `for (const ch of s)` already iterates by codepoint in JS, so this is mostly a convenience —
 * but it is the counterpart of the firmware's utf8_next(), and the two must agree about where
 * a character ends. The degree sign is U+00B0; a byte-wise walk would fail to find its glyph
 * and the renderer would draw NOTHING for "68.4°F", leaving a blank where a temperature
 * belongs. */
function codepoints(s: string): number[] {
  const out: number[] = [];
  for (const ch of s) out.push(ch.codePointAt(0) ?? 0);
  return out;
}

/**
 * Look up a glyph by CODEPOINT.
 *
 * ASCII comes from the dense table indexed by (codepoint - firstChar). Anything else — the
 * degree sign is the only one this project needs — comes from the face's `extra` map, which
 * holds the handful of named non-ASCII glyphs rather than the ~80 mostly-empty slots that
 * covering U+00B0 as a contiguous range would require. */
function glyphFor(face: Face, cp: number): Glyph | undefined {
  if (cp >= face.firstChar && cp < face.firstChar + face.glyphs.length) {
    return face.glyphs[cp - face.firstChar];
  }
  return face.extra.get(cp);
}

/**
 * The block-scale factor of a face: 1 for a rasterised face, k >= 2 for an upscaled one. Mirrors
 * font_scale() in the firmware's fonts.c. Every glyph and metric the face reports must be
 * multiplied by this before it is drawn or laid out, or the pen and the ink would disagree; the
 * value is 1 for the whole rasterised ladder, so this is the identity there.
 */
function faceScale(face: Face): number {
  return face.upscale >= 1 ? face.upscale : 1;
}

/** Advance width in pixels, in the face's DRAWN space (so scaled for an upscaled face).
 *  Returns 0 for a character with no glyph, like font_advance(). */
export function fontAdvance(fontId: number, ch: string): number {
  const face = FACES[fontId];
  if (!face) return 0;
  const cps = codepoints(ch);
  if (cps.length === 0) return 0;
  return (glyphFor(face, cps[0]!)?.advance ?? 0) * faceScale(face);
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
 * ALL METRICS ARE IN THE FACE'S DRAWN PIXELS, already multiplied by its block-scale factor.
 * fonts.c reports the BASE metrics (so a glyph's w/h matches the bitmap it points at) and the
 * firmware renderer scales them; this mirrors that exactly, so the preview and the panel agree.
 * The factor is 1 for every rasterised face, so this is the identity for all of them.
 *
 * Returns null when a character has no glyph, matching font_measure()'s failure. The caller
 * then draws nothing rather than guessing a width.
 */
export function fontMeasure(fontId: number, s: string): { w: number; h: number } | null {
  const face = FACES[fontId];
  if (!face) return null;
  const k = faceScale(face);
  let w = 0;
  for (const cp of codepoints(s)) {
    const g = glyphFor(face, cp);
    if (!g) return null;   /* no glyph: fail rather than guess a width, like font_measure() */
    w += g.advance;
  }
  return { w: w * k, h: face.lineHeight * k };
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
 *
 * AN UPSCALED FACE DRAWS EACH SOURCE PIXEL AS A k x k BLOCK. `g.w`/`g.h` describe the BASE
 * bitmap (every metric from atlas-data is in base pixels — see faceScale), and each set source
 * bit is painted across the whole k x k block. Exact for 1 bpp, and the same technique the
 * firmware uses, so the preview cannot disagree with the panel (NFR-4). The block is clipped per
 * DESTINATION pixel, so a scaled glyph that overflows its box is truncated cleanly.
 */
function blitGlyphClipped(
  dst: Bitmap,
  gx: number,
  gy: number,
  face: Face,
  g: Glyph,
  k: number,
  f: ValueField,
): void {
  if (g.w === 0 || g.h === 0) return; /* blank glyph (space): nothing to draw */
  const pitch = (g.w + 7) >> 3;
  const x0 = f.x;
  const x1 = f.x + f.w;
  const y0 = f.y;
  const y1 = f.y + f.h;

  for (let sy = 0; sy < g.h; sy++) {
    const row = g.off + sy * pitch;
    for (let sx = 0; sx < g.w; sx++) {
      const byte = face.bits[row + (sx >> 3)];
      if (byte === undefined) continue;
      /* Atlas bit SET means ink; a clear bit is background and must be skipped, or the glyph
       * box would punch a white rectangle through the static layer beneath it. */
      if ((byte & (0x80 >> (sx & 7))) === 0) continue;
      const bx = gx + sx * k;
      const by = gy + sy * k;
      for (let dy = 0; dy < k; dy++) {
        const py = by + dy;
        if (py < y0 || py >= y1) continue;
        for (let dx = 0; dx < k; dx++) {
          const px = bx + dx;
          if (px < x0 || px >= x1) continue;
          setPx(dst, px, py, true);
        }
      }
    }
  }
}

/**
 * Draw a weather icon into its box, painting only ink. Mirrors draw_icon() in render.c.
 *
 * Scaled to fit the box with NEAREST-NEIGHBOUR sampling: the source is 1 bpp, so there is no
 * grey to interpolate, and any smoother filter would blur the thin strokes that make a snowflake
 * a snowflake. Aspect ratio is preserved by using the smaller box dimension.
 */
function drawIcon(dst: Bitmap, f: ValueField, code: string | undefined): void {
  const idx = weatherIconIndex(code);
  if (idx === WeatherIcon.UNKNOWN) return;
  const ic = WEATHER_ICONS[idx];
  if (!ic) return;

  const side = Math.min(f.w, f.h);
  if (side <= 0) return;
  const offX = f.x + offsetH(f.alignH, f.w, side);
  const offY = f.y + offsetV(f.alignV, f.h, side);
  const srcPitch = (ic.w + 7) >> 3;

  for (let dy = 0; dy < side; dy++) {
    const sy = Math.floor((dy * ic.h) / side);
    if (sy < 0 || sy >= ic.h) continue;
    const py = offY + dy;
    if (py < f.y || py >= f.y + f.h) continue;
    for (let dx = 0; dx < side; dx++) {
      const sx = Math.floor((dx * ic.w) / side);
      if (sx < 0 || sx >= ic.w) continue;
      if ((ic.bits[sy * srcPitch + (sx >> 3)]! & (0x80 >> (sx & 7))) === 0) continue;
      const px = offX + dx;
      if (px < f.x || px >= f.x + f.w) continue;
      setPx(dst, px, py, true);
    }
  }
}

/** Draw one field's value. Mirrors draw_field(). */
function drawField(dst: Bitmap, f: ValueField, s: string | undefined): void {
  if (!s) return; /* nothing to show: leave the static layer intact */

  /* An icon box draws a PICTURE from the value, not the string itself: the value is the OWM
   * icon code, and rendering it as text would put "04n" on the glass. */
  if (f.kind === 'i') {
    drawIcon(dst, f, s);
    return;
  }

  const face = FACES[f.fontId];
  /* An unknown font id is a BUG in the caller, not a rendering condition — the firmware
   * cannot hit this (font_id_t is an enum) and neither should we. Returning quietly is what
   * let a mis-spelled `font` key blank every label silently, with the only symptom being a
   * golden-image mismatch hundreds of bytes away. */
  if (!face) throw new Error(`unknown font id ${f.fontId} for text ${JSON.stringify(s)}`);
  const m = fontMeasure(f.fontId, s);
  if (!m) return; /* a character with no glyph: draw nothing, do not guess */

  /* The block-scale factor. fontMeasure already applied it to the width and line height; the
   * baseline, bearings and advances below apply it here, so pen and ink stay in step. */
  const k = faceScale(face);

  /* The pen origin is the top-left of the LINE box; glyph ink is then placed relative to the
   * BASELINE via each glyph's bearing. Placing by ink box alone would float a '.' at the top
   * of the line. */
  let penX = f.x + offsetH(f.alignH, f.w, m.w);
  const penY = f.y + offsetV(f.alignV, f.h, m.h);
  const baseline = penY + face.ascent * k;

  for (const cp of codepoints(s)) {
    const g = glyphFor(face, cp);
    if (!g) break;
    blitGlyphClipped(dst, penX + g.bx * k, baseline + g.by * k, face, g, k, f);
    penX += g.advance * k;
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
 * `font` is a PIXEL SIZE, not a face index, and that distinction is load-bearing. The fixture
 * and the presets used to carry a bare face index, which was stable only while the panel had
 * exactly two faces: index 0 meant "the body face". When the ladder grew, index 0 became the
 * 16 px face, and every label authored as 0 silently shrank — the fixture, the preset and the
 * golden all disagreed with what the author meant, and the mismatch showed up as a byte
 * difference hundreds of pixels from the cause. A pixel size means the same thing forever, and
 * the ladder resolves it in ONE place (faceIdForPx). */
export interface StaticLabel {
  x: number;
  y: number;
  text: string;
  /** Pixel size; resolved to a ladder face by buildStaticLayer. */
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
    drawText(b, l.x, l.y, l.text, faceIdForPx(l.font));
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
