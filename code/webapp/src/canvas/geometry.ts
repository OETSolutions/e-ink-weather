import type { Widget } from '../model/config';

export const PANEL_W = 920;
export const PANEL_H = 680;
const EDGE = 8;

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

export function clampToPanel(r: Rect): Rect {
  let { x, y, w, h } = r;
  if (w > PANEL_W) w = PANEL_W;
  if (h > PANEL_H) h = PANEL_H;
  x = Math.min(Math.max(0, x), PANEL_W - w);
  y = Math.min(Math.max(0, y), PANEL_H - h);
  return { x, y, w, h };
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
