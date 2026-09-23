import type { Label, Rule, Widget } from '../model/config';

export const PANEL_W = 920;
export const PANEL_H = 680;
const EDGE = 8;

/** How close, in panel pixels, a pointer must be to a rule's line to grab it. A rule is a
 *  hairline; without a tolerance it would be nearly impossible to hit. 10 px is under two
 *  millimetres on the glass and comfortably larger than the 2 px line at fit scale. */
export const RULE_GRAB = 10;

export type Zone = 'move' | 'n' | 's' | 'e' | 'w' | 'ne' | 'nw' | 'se' | 'sw';
export interface Rect { x: number; y: number; w: number; h: number }
export interface ResizeOpts { grid: number; minW: number; minH: number }

export function hitTest(widgets: Widget[], x: number, y: number): { id: string; zone: Zone } | null {
  for (let i = widgets.length - 1; i >= 0; i--) {
    const wd = widgets[i]!;
    const inX = x >= wd.x && x <= wd.x + wd.w;
    const inY = y >= wd.y && y <= wd.y + wd.h;
    if (!inX || !inY) continue;
    const left = x <= wd.x + EDGE;
    const right = x >= wd.x + wd.w - EDGE;
    const top = y <= wd.y + EDGE;
    const bottom = y >= wd.y + wd.h - EDGE;
    if (top && left) return { id: wd.id, zone: 'nw' };
    if (top && right) return { id: wd.id, zone: 'ne' };
    if (bottom && left) return { id: wd.id, zone: 'sw' };
    if (bottom && right) return { id: wd.id, zone: 'se' };
    if (left) return { id: wd.id, zone: 'w' };
    if (right) return { id: wd.id, zone: 'e' };
    if (top) return { id: wd.id, zone: 'n' };
    if (bottom) return { id: wd.id, zone: 's' };
    return { id: wd.id, zone: 'move' };
  }
  return null;
}

export function applyResize(wd: Widget, zone: Zone, _start: {x:number;y:number}, dx: number, dy: number, o: ResizeOpts): Widget {
  let { x, y, w, h } = wd;
  if (zone.includes('e')) w = Math.max(o.minW, wd.w + dx);
  if (zone.includes('s')) h = Math.max(o.minH, wd.h + dy);
  if (zone.includes('w')) {
    const nw = Math.max(o.minW, wd.w - dx);
    x = wd.x + (wd.w - nw);
    w = nw;
  }
  if (zone.includes('n')) {
    const nh = Math.max(o.minH, wd.h - dy);
    y = wd.y + (wd.h - nh);
    h = nh;
  }
  /* FLOORED, like clampToPanel and snap. `dx`/`dy` are fractional (they come from a pointer delta
   * divided by a fractional display scale), so a west or north resize — the two that compute a new
   * ORIGIN by subtracting, rather than just a new size — produced a fractional x or y. clampToPanel
   * floors it again on the editor's path, but this function is exported and host-tested on its own,
   * so it must not hand a fraction to any other caller either. */
  return { ...wd, x: Math.floor(x), y: Math.floor(y), w: Math.floor(w), h: Math.floor(h) };
}

export interface DragOpts {
  grid: number;
  /** Panel-space x positions to align to: panel edges and the edges of other widgets. */
  guidesX: number[];
  guidesY: number[];
}

/**
 * Move a widget by a pointer delta, snapping and clamping it.
 *
 * TWO SNAPS ARE TRIED, and the better one wins: the widget's LEFT edge against the guides,
 * then its RIGHT edge. Without the second, aligning two widgets by their right edges is
 * impossible — you can only ever align left edges — which is the wrong half of the job for a
 * right-aligned reading.
 *
 * The clamp runs LAST, after snapping. Snapping can push a widget past the panel edge (the
 * nearest guide to a widget at x=910 might be 920), and clamping before the snap would let
 * that through. Clamping after means the result is always inside the panel, whatever the snap
 * chose.
 */
export function applyDrag(
  wd: Widget,
  _start: { x: number; y: number },
  dx: number,
  dy: number,
  o: DragOpts,
): Widget {
  const rawX = wd.x + dx;
  const rawY = wd.y + dy;

  /* Left edge against the guides, and the right edge against them (converted back to a left
   * position by subtracting the width). Take whichever moved the widget less. */
  const snappedLeftX = snap(rawX, o.grid, o.guidesX);
  const snappedRightX = snap(rawX + wd.w, o.grid, o.guidesX) - wd.w;
  const x = Math.abs(snappedLeftX - rawX) <= Math.abs(snappedRightX - rawX)
    ? snappedLeftX
    : snappedRightX;

  const snappedTopY = snap(rawY, o.grid, o.guidesY);
  const snappedBottomY = snap(rawY + wd.h, o.grid, o.guidesY) - wd.h;
  const y = Math.abs(snappedTopY - rawY) <= Math.abs(snappedBottomY - rawY)
    ? snappedTopY
    : snappedBottomY;

  const r = clampToPanel({ x, y, w: wd.w, h: wd.h });
  return { ...wd, ...r };
}

/**
 * The alignment guides for a drag: the panel's edges plus every other widget's edges.
 *
 * `excludeId` is the widget being dragged — including its own edges would make it snap to
 * where it already is and fight every pointer move.
 */
export function guidesFor(
  widgets: Widget[],
  excludeId: string,
): { x: number[]; y: number[] } {
  const x = [0, PANEL_W];
  const y = [0, PANEL_H];
  for (const wd of widgets) {
    if (wd.id === excludeId) continue;
    x.push(wd.x, wd.x + wd.w);
    y.push(wd.y, wd.y + wd.h);
  }
  return { x, y };
}

export function clampToPanel(r: Rect): Rect {
  let { x, y, w, h } = r;
  if (w > PANEL_W) w = PANEL_W;
  if (h > PANEL_H) h = PANEL_H;
  /* FLOORED TO WHOLE PIXELS, for the same reason snap() rounds: a fractional y makes the renderer
   * discard the box entirely, so a resize that produced one looked like it had deleted the value.
   * This is the last stop for geometry on its way into the model, so flooring here means every
   * stored box is integral whatever path produced it. */
  w = Math.max(1, Math.floor(w));
  h = Math.max(1, Math.floor(h));
  x = Math.floor(Math.min(Math.max(0, x), PANEL_W - w));
  y = Math.floor(Math.min(Math.max(0, y), PANEL_H - h));
  return { x, y, w, h };
}

/**
 * The index of the rule under a point, or -1. Nearest wins, so two rules a few pixels apart are
 * both reachable.
 *
 * Rules are tested BEFORE widgets by the caller: a rule may legitimately sit in a gap but never
 * over a box, and giving the line priority makes it unambiguous when the band it is grabbed in
 * grazes a box edge. Only the horizontal span `[inset, PANEL_W - inset]` counts, because that is
 * where the rule is actually drawn — a pointer out beyond an end cap must not grab it.
 */
export function ruleHit(rules: Rule[], x: number, y: number): number {
  let best = -1;
  let bestD = RULE_GRAB;
  for (let i = 0; i < rules.length; i++) {
    const r = rules[i]!;
    if (x < r.inset || x > PANEL_W - r.inset) continue;
    const d = Math.abs(y - r.y);
    if (d <= bestD) { bestD = d; best = i; }
  }
  return best;
}

/**
 * Move a rule's y by a pointer delta, snapped to the grid and clamped inside the panel.
 *
 * VERTICAL ONLY: a rule spans the panel width by construction, so a horizontal drag has nowhere
 * to go. Clamped one pixel short of the bottom edge so the line — and its thickness below y —
 * always has at least one row of glass to land on.
 */
export function applyRuleDrag(originY: number, dy: number, grid: number): number {
  const raw = originY + dy;
  const snapped = snap(raw, grid, [0, PANEL_H]);
  return Math.min(PANEL_H - 1, Math.max(0, snapped));
}

/**
 * Snap a value to a guide or the grid, ALWAYS RETURNING AN INTEGER.
 *
 * THE ROUNDING AT THE END IS NOT COSMETIC. The pointer delta this is fed is computed from a
 * fractional display scale (the panel is 920 px wide and the fit scale is rarely 1:1), so `value`
 * arrives fractional — and a guide is only integral if every widget's stored geometry is. Returning
 * a fraction here put a fractional y into the widget, and a fractional y made the renderer drop
 * every pixel of that box (see setPx). The value would vanish from the preview as the user dragged,
 * which is the reported "values disappear depending on where you place them".
 *
 * The geometry MODEL is therefore integral by construction: every path that produces a widget,
 * rule or label position ends here or at clampToPanel, and both now floor/round. That keeps a
 * saved document clean — the device truncates to int anyway, so a fraction in the document was
 * never meaningful, only a source of preview/panel disagreement.
 */
export function snap(value: number, grid: number, guides: number[]): number {
  const tol = Math.max(2, grid / 2);
  let best = value;
  let bestD = tol + 1;
  for (const g of guides) {
    const d = Math.abs(value - g);
    if (d < bestD) { bestD = d; best = g; }
  }
  if (bestD <= tol) return Math.round(best);
  return Math.round(value / grid) * grid;
}

/**
 * The index of the label under a point, or -1.
 *
 * A LABEL IS HIT BY A BOX, NOT BY ITS BASELINE. `measure` returns the text's rendered width and
 * the line height for the label's pixel size, and that box is what the pointer must be inside —
 * testing the single anchor point instead would make a label almost impossible to grab, and the
 * near miss would select the widget behind it. `minW`/`minH` floor the box so a one-character
 * label ("1") still has a usable grab area rather than a sliver a few pixels wide.
 *
 * Tested AFTER rules and widgets by the caller: a label sits in the margin above its reading, but
 * a user may drag one anywhere, and a widget on top of a label should stay reachable.
 */
export function labelHit(
  labels: Label[],
  x: number,
  y: number,
  measure: (text: string, px: number) => { w: number; h: number },
  minW = 16,
  minH = 12,
): number {
  /* Reverse order so the most recently added label wins when two overlap, matching hitTest. */
  for (let i = labels.length - 1; i >= 0; i--) {
    const l = labels[i]!;
    const m = measure(l.text, l.font);
    const w = Math.max(minW, m.w);
    const h = Math.max(minH, m.h);
    if (x >= l.x && x <= l.x + w && y >= l.y && y <= l.y + h) return i;
  }
  return -1;
}

/**
 * Move a label by a pointer delta, snapped to the grid and clamped inside the panel.
 *
 * THE BOX IS WHAT IS CLAMPED, not the anchor: a label dragged to x=915 would otherwise put all
 * but a few pixels of its text off the glass, and the renderer clips text to the panel — so the
 * heading would silently lose characters with no indication why.
 */
export function applyLabelDrag(
  origin: Label,
  dx: number,
  dy: number,
  grid: number,
  measure: (text: string, px: number) => { w: number; h: number },
): Label {
  const m = measure(origin.text, origin.font);
  const rawX = origin.x + dx;
  const rawY = origin.y + dy;
  /* FLOORED, like clampToPanel: a fractional label position would drop the heading from the
   * preview for the same reason a fractional box drops its value. snap() already rounds, but the
   * `PANEL_W - m.w` bound it is clamped against is fractional whenever the measured width is, so
   * the result is floored here as the last step. */
  const x = Math.floor(Math.min(Math.max(0, snap(rawX, grid, [0, PANEL_W])), Math.max(0, PANEL_W - m.w)));
  const y = Math.floor(Math.min(Math.max(0, snap(rawY, grid, [0, PANEL_H])), Math.max(0, PANEL_H - m.h)));
  return { ...origin, x, y };
}
