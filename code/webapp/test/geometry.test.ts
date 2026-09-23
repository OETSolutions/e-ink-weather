import { describe, it, expect } from 'vitest';
import {
  hitTest, applyResize, applyDrag, guidesFor, clampToPanel, snap, ruleHit, applyRuleDrag, RULE_GRAB,
  labelHit, applyLabelDrag,
} from '../src/canvas/geometry';
import type { Label, Rule, Widget } from '../src/model/config';
const w = (o: Partial<Widget> = {}): Widget => ({ id:'a', x:100, y:100, w:200, h:100, role:'dynamic', ...o });
const rule = (y: number, o: Partial<Rule> = {}): Rule => ({ y, thickness: 2, inset: 40, ...o });
describe('hit testing', () => {
  it('finds widget under cursor', () => {
    expect(hitTest([w()],200,150)?.id).toBe('a');
    expect(hitTest([w()],50,50)).toBeNull();
  });
  it('reports edge and corner zones', () => {
    expect(hitTest([w()],100,150)?.zone).toBe('w');
    expect(hitTest([w()],100,100)?.zone).toBe('nw');
    expect(hitTest([w()],200,150)?.zone).toBe('move');
  });
  it('prefers topmost', () => {
    const a=w({id:'a'}), b=w({id:'b',x:150,y:120});
    expect(hitTest([a,b],200,150)?.id).toBe('b');
  });
});
describe('resize', () => {
  it('east changes width not x', () => {
    const r=applyResize(w(),'e',{x:300,y:0},50,0,{grid:1,minW:20,minH:20});
    expect(r.x).toBe(100); expect(r.w).toBe(250);
  });
  it('west moves x and shrinks w together', () => {
    const r=applyResize(w(),'w',{x:0,y:0},20,0,{grid:1,minW:20,minH:20});
    expect(r.x).toBe(120); expect(r.w).toBe(180);
  });
  it('never below minimum', () => {
    const r=applyResize(w(),'e',{x:0,y:0},-1000,0,{grid:1,minW:20,minH:20});
    expect(r.w).toBe(20);
  });
});
describe('clamp and snap', () => {
  it('pulls widget inside panel', () => {
    const r=clampToPanel({x:900,y:660,w:200,h:100});
    expect(r.x+r.w).toBeLessThanOrEqual(920);
    expect(r.y+r.h).toBeLessThanOrEqual(680);
  });
  it('snaps to grid', () => {
    expect(snap(103,8,[])).toBe(104);
    expect(snap(99,8,[])).toBe(96);
  });
  it('prefers guide over grid', () => {
    expect(snap(101,8,[100])).toBe(100);
  });
});

describe('drag', () => {
  const o = { grid: 8, guidesX: [0, 920], guidesY: [0, 680] };

  it('moves a widget by the pointer delta, snapped to the grid', () => {
    /* Grid-aligned dimensions, so BOTH edges can satisfy the grid and the whole box lands
     * on it. (With a height that is not a multiple of the grid the two edges cannot both
     * align — see the bottom-edge test below, which is why that is the honest way to check
     * this rather than asserting x % grid on any widget.) */
    const r = applyDrag(w({ x: 100, y: 100, w: 200, h: 96 }), { x: 0, y: 0 }, 51, 23, o);
    expect(r.x % 8).toBe(0);
    expect(r.y % 8).toBe(0);
    expect(r.w).toBe(200); /* size is untouched by a move */
    expect(r.h).toBe(96);
  });

  /* When the height is NOT a multiple of the grid, only one edge can align, and the code
   * prefers whichever is closer to the pointer. The result must still have an aligned EDGE —
   * asserting the top is aligned would be asserting something the geometry cannot promise. */
  it('aligns an edge even when the widget size is off-grid', () => {
    const r = applyDrag(w({ x: 100, y: 100, w: 200, h: 100 }), { x: 0, y: 0 }, 51, 23, o);
    const topAligned = r.y % 8 === 0;
    const bottomAligned = (r.y + r.h) % 8 === 0;
    expect(topAligned || bottomAligned).toBe(true);
    expect(r.y).toBeGreaterThan(100);  /* it still moved down */
  });

  /* Snapping the RIGHT edge too, or two widgets can never be aligned by their right edges —
   * which is the alignment a right-justified reading needs. */
  it('snaps the right edge against a guide, not only the left', () => {
    /* A guide at 400; widget w=200, so a left of 201 puts the right edge at 401 — 1px off.
     * The right-edge snap should win and land the left at 200. */
    const r = applyDrag(w({ x: 100, y: 100 }), { x: 0, y: 0 }, 101, 0,
                        { grid: 8, guidesX: [400], guidesY: [] });
    expect(r.x + r.w).toBe(400);
  });

  it('never lets a drag push a widget out of the panel', () => {
    const far = applyDrag(w({ x: 100, y: 100 }), { x: 0, y: 0 }, 5000, 5000, o);
    expect(far.x + far.w).toBeLessThanOrEqual(920);
    expect(far.y + far.h).toBeLessThanOrEqual(680);
    const neg = applyDrag(w({ x: 100, y: 100 }), { x: 0, y: 0 }, -5000, -5000, o);
    expect(neg.x).toBeGreaterThanOrEqual(0);
    expect(neg.y).toBeGreaterThanOrEqual(0);
  });

  /* Clamping must happen AFTER the snap: a guide can sit past the panel edge. */
  it('clamps after snapping so a far guide cannot push it out', () => {
    const r = applyDrag(w({ x: 700, y: 100 }), { x: 0, y: 0 }, 219, 0,
                        { grid: 8, guidesX: [960], guidesY: [] });
    expect(r.x + r.w).toBeLessThanOrEqual(920);
  });
});

describe('guides', () => {
  it('includes the panel edges and other widgets, excluding the one being dragged', () => {
    const g = guidesFor([w({ id: 'a', x: 100, y: 100 }), w({ id: 'b', x: 300, y: 50, w: 50, h: 50 })], 'a');
    expect(g.x).toContain(0);
    expect(g.x).toContain(920);
    expect(g.x).toContain(300);   /* b's left */
    expect(g.x).toContain(350);   /* b's right */
    expect(g.x).not.toContain(100); /* a's own left must not be a guide for a */
    expect(g.y).toContain(50);
    expect(g.y).toContain(100);
  });
});

/* RULES (dividers). These are the parts the user cannot reach another way: the drag is the only
 * way to place a line by eye, so the hit tolerance and the clamp are what make the feature work
 * rather than merely exist. */
describe('rule hit testing', () => {
  it('grabs a rule within the tolerance, on either side of the line', () => {
    const r = [rule(200)];
    expect(ruleHit(r, 400, 200)).toBe(0);
    expect(ruleHit(r, 400, 200 + RULE_GRAB)).toBe(0);
    expect(ruleHit(r, 400, 200 - RULE_GRAB)).toBe(0);
    expect(ruleHit(r, 400, 200 + RULE_GRAB + 1)).toBe(-1);
  });

  it('ignores a pointer beyond an end cap', () => {
    /* The rule spans x=inset..PANEL_W-inset. Outside that it is not drawn, so it must not be
     * grabbable there — otherwise a click well past the line would move it with no visual cue. */
    const r = [rule(200, { inset: 100 })];
    expect(ruleHit(r, 99, 200)).toBe(-1);
    expect(ruleHit(r, 100, 200)).toBe(0);
    expect(ruleHit(r, 820, 200)).toBe(0);
    expect(ruleHit(r, 821, 200)).toBe(-1);
  });

  it('picks the nearest of two close rules', () => {
    const r = [rule(200), rule(206)];
    expect(ruleHit(r, 400, 201)).toBe(0);
    expect(ruleHit(r, 400, 205)).toBe(1);
  });
});

describe('rule drag', () => {
  it('moves vertically, snapped to the grid', () => {
    expect(applyRuleDrag(200, 45, 8)).toBe(248);   /* 245 snapped to the nearest 8 */
  });

  it('clamps at the top and one pixel short of the bottom', () => {
    expect(applyRuleDrag(4, -500, 8)).toBe(0);
    /* A rule at 680 would be entirely off the glass, so the bottom stop is 679. */
    expect(applyRuleDrag(100, 5000, 8)).toBe(679);
  });

  it('is unaffected by a horizontal drag', () => {
    /* The function takes only dy — there is no x to move, which is the vertical-only rule
     * enforced by the signature rather than by a comment. */
    expect(applyRuleDrag(200, 0, 8)).toBe(200);
  });
});

/* LABELS (headings). These were hard-coded per-page artwork until they became editable, so the
 * hit test and the drag are what make "NOW"/"HALLWAY"/"TODAY HIGH" reachable in the editor at
 * all. `measure` is injected because a label's box is its rendered text, and the real measurer
 * lives in the renderer. */
describe('label hit testing', () => {
  const label = (o: Partial<Label> = {}): Label => ({ x: 40, y: 32, text: 'NOW', font: 20, ...o });
  /* A fake measurer: 6 px per character, one line 20 px tall. Deterministic and independent of
   * the font ladder, so the test is about the hit test and not about glyph advances. */
  const measure = (text: string): { w: number; h: number } => ({ w: text.length * 6, h: 20 });

  it('finds a label inside its measured box', () => {
    expect(labelHit([label()], 50, 40, measure)).toBe(0);
    expect(labelHit([label()], 40, 32, measure)).toBe(0);
    /* "NOW" is 18 px wide, so x=62 is just past the right edge. */
    expect(labelHit([label()], 70, 40, measure)).toBe(-1);
  });

  it('floors the box so a one-character heading is still grabbable', () => {
    /* "1" measures 6 px wide — below the 16 px floor. A sliver a few pixels wide would be
     * effectively unclickable, and the near miss would land on whatever sits behind it. */
    const l = [label({ text: '1' })];
    expect(labelHit(l, 40 + 14, 32 + 4, measure)).toBe(0);
    expect(labelHit(l, 40 + 20, 32 + 4, measure)).toBe(-1);
  });

  it('prefers the topmost (most recently added) label when two overlap', () => {
    const a = label({ text: 'A', y: 32 });
    const b = label({ text: 'B', y: 32 });
    expect(labelHit([a, b], 45, 40, measure)).toBe(1);
  });

  it('reports no hit past the end of the list', () => {
    expect(labelHit([], 45, 40, measure)).toBe(-1);
    expect(labelHit([label()], 500, 500, measure)).toBe(-1);
  });
});

describe('label drag', () => {
  const measure = (text: string): { w: number; h: number } => ({ w: text.length * 6, h: 20 });
  const l = (o: Partial<Label> = {}): Label => ({ x: 40, y: 32, text: 'NOW', font: 20, ...o });

  it('moves by the pointer delta, snapped to the grid', () => {
    const r = applyLabelDrag(l(), 51, 23, 8, measure);
    expect(r.x % 8).toBe(0);
    expect(r.y % 8).toBe(0);
    expect(r.text).toBe('NOW');   /* the text is untouched by a move */
  });

  it('clamps so the whole label stays on the panel', () => {
    /* Dragged far right, the label's LEFT edge is clamped so its text still fits — a heading
     * that hung off the edge would be silently clipped by the renderer, losing characters. */
    const m = measure('NOW');
    const r = applyLabelDrag(l(), 5000, 5000, 8, measure);
    expect(r.x + m.w).toBeLessThanOrEqual(920);
    expect(r.y + m.h).toBeLessThanOrEqual(680);
    const neg = applyLabelDrag(l(), -5000, -5000, 8, measure);
    expect(neg.x).toBeGreaterThanOrEqual(0);
    expect(neg.y).toBeGreaterThanOrEqual(0);
  });

  it('keeps a wide label on the panel rather than letting it overflow both ends', () => {
    const wide = l({ text: 'X'.repeat(200), x: 0 });
    const m = measure(wide.text);
    const r = applyLabelDrag(wide, 60, 0, 8, measure);
    /* The text is wider than the panel, so it cannot fit; the clamp must still not produce a
     * NEGATIVE x (which would be worse than the overflow). */
    expect(r.x).toBeGreaterThanOrEqual(0);
    expect(m.w).toBeGreaterThan(920);   /* documents the case this guards */
  });
});
