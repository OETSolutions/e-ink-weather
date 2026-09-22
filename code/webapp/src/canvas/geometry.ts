import type { Rule, Widget } from '../model/config';

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
  return { ...wd, x, y, w, h };
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
  x = Math.min(Math.max(0, x), PANEL_W - w);
  y = Math.min(Math.max(0, y), PANEL_H - h);
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

export function snap(value: number, grid: number, guides: number[]): number {
  const tol = Math.max(2, grid / 2);
  let best = value;
  let bestD = tol + 1;
  for (const g of guides) {
    const d = Math.abs(value - g);
    if (d < bestD) { bestD = d; best = g; }
  }
  if (bestD <= tol) return best;
  return Math.round(value / grid) * grid;
}
