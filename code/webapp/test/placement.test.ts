import { describe, it, expect } from 'vitest';
import { freeSpot, freshWidgetId, findPlacement, fontSizeForBox } from '../src/canvas/placement';
import { FACES, FACE_PX } from '../src/canvas/atlas-data';
import { defaultLayout } from '../src/presets/default-layout';
import type { Widget } from '../src/model/config';

const box = (x: number, y: number, w = 100, h = 100, id = 'a'): Widget =>
  ({ id, x, y, w, h, role: 'dynamic' });

describe('placing a new box', () => {
  it('on an empty page, lands at the margin', () => {
    expect(freeSpot([], 400, 96)).toEqual({ x: 40, y: 40 });
  });

  it('does not overlap an existing box', () => {
    /* The whole point: a second box must not be dropped where the first already is. It steps
     * along until it clears. */
    const spot = freeSpot([box(40, 40, 400, 96)], 400, 96)!;
    expect(spot).not.toBeNull();
    const clash = spot.x < 40 + 400 && 40 < spot.x + 400 && spot.y < 40 + 96 && 40 < spot.y + 96;
    expect(clash).toBe(false);
  });

  it('rejects a NEARLY identical box, not just an identical one', () => {
    /* An equality test would let a box at (48,48) land under a box at (40,40) — a placement the
     * user would see as "Add box did nothing". */
    const spot = freeSpot([box(40, 40, 400, 96)], 400, 96)!;
    expect(spot.x === 48 && spot.y === 48).toBe(false);
  });

  it('returns null when the page is full rather than an off-panel box', () => {
    /* A box bigger than the usable area can never fit; null is the honest answer, and the caller
     * reports it instead of placing a box that runs off the glass. */
    expect(freeSpot([], 900, 680)).toBeNull();
  });

  it('stays clear of the margin on every side', () => {
    const spot = freeSpot([box(40, 40, 800, 100)], 200, 100)!;
    expect(spot.x).toBeGreaterThanOrEqual(40);
    expect(spot.y).toBeGreaterThanOrEqual(40);
    expect(spot.x + 200).toBeLessThanOrEqual(920 - 40);
    expect(spot.y + 100).toBeLessThanOrEqual(680 - 40);
  });
});

describe('new widget ids', () => {
  it('does not reuse an existing id', () => {
    expect(freshWidgetId([{ id: 'val1' }, { id: 'val2' }])).toBe('val3');
  });

  it('skips a gap rather than counting', () => {
    /* val1 and val3 exist, so the next must be val2 — filling the gap keeps ids short and makes
     * a deleted-then-added box reuse the obvious name. */
    expect(freshWidgetId([{ id: 'val1' }, { id: 'val3' }])).toBe('val2');
  });

  it('is unique on a fresh page', () => {
    expect(freshWidgetId([])).toBe('val1');
  });
});

describe('choosing a size that fits', () => {
  it('uses the largest size when the page is empty', () => {
    expect(findPlacement([])).toEqual({ x: 40, y: 40, w: 360, h: 96 });
  });

  it('steps DOWN a size rather than reporting no space, on the shipped full layout', () => {
    /* THE BUG THIS PREVENTS: the default layout fills the panel, so a fixed 400x96 request finds
     * no spot and Add box reports "no space" on a device whose panel is in fact ~30% free in
     * smaller gaps. The feature would look broken on first use. */
    const ws = defaultLayout().pages[0]!.widgets;
    const spot = findPlacement(ws);
    expect(spot, 'no placement found on the default layout').not.toBeNull();
    expect(spot!.w).toBeLessThanOrEqual(360);
    /* And the chosen box must not overlap anything. */
    const clash = ws.some((o) =>
      spot!.x < o.x + o.w && o.x < spot!.x + spot!.w &&
      spot!.y < o.y + o.h && o.y < spot!.y + spot!.h);
    expect(clash).toBe(false);
  });

  it('returns null only when even the smallest box cannot fit', () => {
    /* A wall of boxes covering the whole usable area leaves nothing — a real answer. */
    const wall = [{ x: 0, y: 0, w: 920, h: 680 }];
    expect(findPlacement(wall)).toBeNull();
  });
});

describe('font size for a chosen box', () => {
  it('never picks a face whose line height exceeds the box', () => {
    /* THE BUG THIS PREVENTS: Add box used a fixed 64 px font, but the step-down can leave a
     * 72 px-tall gap — and the 64 px face's line height is 78, so its numeral is CLIPPED. The
     * user sees a chopped glyph in a box they did not choose the size of, which reads as a
     * rendering fault. */
    for (const h of [96, 88, 80, 72, 64, 56, 48, 40, 32, 24, 20, 16]) {
      const px = fontSizeForBox(h);
      const face = FACES[FACE_PX.indexOf(px)]!;
      if (px > FACE_PX[0]!) {
        expect(face.lineHeight, `box ${h} chose ${px}px whose line height is ${face.lineHeight}`)
          .toBeLessThanOrEqual(h);
      }
    }
  });

  it('picks the largest that fits', () => {
    /* A 96 px box takes the 64 px face (line height 78). The 80 px face has a line height of
     * 98, which does not fit in 96 — so 64 is the right answer, not 80. */
    expect(fontSizeForBox(96)).toBe(64);
    expect(fontSizeForBox(78)).toBe(64);   /* exactly the 64 px face's line height */
    expect(fontSizeForBox(30)).toBe(24);
  });

  it('falls back to the smallest rather than returning nothing for a sliver of a box', () => {
    expect(fontSizeForBox(2)).toBe(16);
  });
});
