/**
 * Where a newly added value box goes.
 *
 * WHY THIS IS A MODULE AND NOT A FEW LINES IN main.ts: the placement rule is the difference
 * between "Add box" producing a usable box and producing an invisible one stacked exactly on an
 * existing box. That is a real, testable property — the first spot that does not overlap is
 * either found or it is not — and the project's rule is that the geometry the user depends on
 * lives in a host-tested function rather than in a DOM event handler.
 */

import type { Widget } from '../model/config';
import { PANEL_W, PANEL_H } from './geometry';
import { FACES, FACE_PX } from './atlas-data';

export interface Box { x: number; y: number; w: number; h: number }

/** A grid step for candidate origins. 40 px is coarse enough to reach a free spot in a few
 *  probes on a 920x680 panel and fine enough that the result does not look like it was placed by
 *  a machine. */
const STEP = 40;

/**
 * The first origin at which a `w`x`h` box fits without OVERLAPPING any existing box, or null
 * when the page has no room. `margin` keeps the box off the panel edge, where it would be hard
 * to grab and would sit under the bezel on the glass.
 *
 * OVERLAP, NOT EQUALITY: a box is rejected when it shares any pixels with an existing one, not
 * only when its origin matches. Two boxes at (40,40) and (48,48) overlap almost entirely and
 * would be visually indistinguishable, so an equality test would hand back the second one and
 * the user would see nothing appear.
 *
 * It scans top-to-bottom, left-to-right, so successive adds fill the page in a readable order
 * rather than jumping around.
 */
export function freeSpot(
  existing: Pick<Widget, 'x' | 'y' | 'w' | 'h'>[],
  w: number,
  h: number,
  margin = 40,
): { x: number; y: number } | null {
  for (let y = margin; y + h <= PANEL_H - margin; y += STEP) {
    for (let x = margin; x + w <= PANEL_W - margin; x += STEP) {
      const clash = existing.some((o) =>
        x < o.x + o.w && o.x < x + w && y < o.y + o.h && o.y < y + h);
      if (!clash) return { x, y };
    }
  }
  return null;
}

/**
 * An id that no existing widget uses, and that can never be the reserved rule id.
 *
 * A DUPLICATE ID IS NOT COSMETIC: the editor selects by id and the preview keys values on it, so
 * two widgets sharing one id would move together and show each other's reading. The reserved
 * "@rule" prefix is skipped because `rules` and `widgets` live in the same document and a widget
 * claiming the rule id would confuse anything that addresses a selection by id alone.
 */
export function freshWidgetId(existing: Pick<Widget, 'id'>[], prefix = 'val'): string {
  const used = new Set(existing.map((w) => w.id));
  let n = 1;
  while (used.has(`${prefix}${n}`)) n++;
  return `${prefix}${n}`;
}

/**
 * The sizes "Add box" will try, largest first.
 *
 * WHY A RANGE AND NOT ONE SIZE: the shipped default layout fills the panel, so a fixed 400x96
 * box finds no spot and "Add box" would report "no space" on the very device a new user starts
 * with — a feature that appears broken while the panel is in fact 30% free in smaller gaps. The
 * candidates step down to a size that fits, and the smallest still holds a 64 px numeral plus a
 * label. Every size is drawn by the same renderer, so the box the user gets is a real, usable
 * one at whatever size the gap allows.
 */
export const BOX_SIZES: { w: number; h: number }[] = [
  { w: 360, h: 96 },
  { w: 300, h: 88 },
  { w: 280, h: 80 },
  { w: 240, h: 72 },
  { w: 200, h: 64 },
  { w: 160, h: 56 },
];

/**
 * The first size from BOX_SIZES that has a free spot, with that spot. Returns null only when the
 * page has no room for even the smallest box — a real answer the caller reports rather than
 * placing something unusable.
 *
 * A SMALLER SIZE IS CHOSEN BEFORE A WORSE POSITION: the loop is size-major, so a 360-wide box
 * anywhere beats a 200-wide box somewhere arguably nicer. Size is what makes the reading legible
 * across a room, which is the whole purpose of this panel, so it is the thing to maximise.
 */
export function findPlacement(
  existing: Pick<Widget, 'x' | 'y' | 'w' | 'h'>[],
  margin = 40,
): Box | null {
  for (const s of BOX_SIZES) {
    const spot = freeSpot(existing, s.w, s.h, margin);
    if (spot) return { x: spot.x, y: spot.y, w: s.w, h: s.h };
  }
  return null;
}

/**
 * The largest face whose LINE HEIGHT fits a box `h` tall.
 *
 * WHY A NEW BOX MUST NOT HARDCODE A SIZE: Add box used a fixed 64 px font at every placement
 * size, but the placement step-down can leave only a 72 px-tall gap — and the 64 px face has a
 * 78 px line height, so its numeral overflows the box and is CLIPPED. The user would see a
 * chopped glyph in a box they never chose the size for, which reads as a rendering fault rather
 * than the placement it actually is. Choosing by line height keeps the reading whole and as
 * large as the gap allows.
 *
 * Falls back to the smallest face if even that does not fit, whose ink is clipped honestly
 * rather than silently.
 */
export function fontSizeForBox(h: number): number {
  let best = FACE_PX[0]!;
  for (let i = 0; i < FACE_PX.length; i++) {
    if (FACES[i]!.lineHeight <= h) best = FACE_PX[i]!;
  }
  return best;
}
