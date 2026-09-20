/**
 * The canvas editor (FR-20, FR-21): draw the page, drag widgets, resize them from their edges.
 *
 * WHAT THIS DRAWS IS THE REAL 1-BIT OUTPUT. The preview is not a DOM approximation with divs
 * and CSS boxes — it is the framebuffer from renderPage(), the same bits the device will
 * compose, scaled up and shown with `image-rendering: pixelated`. That is the only way the
 * preview can be trusted: an HTML mockup would look right while the actual 1 bpp output had a
 * clipped glyph or a mis-aligned field, and the user would find out from the glass.
 *
 * THE PURE GEOMETRY IS NOT HERE. hitTest/applyDrag/applyResize/clampToPanel/snap live in
 * geometry.ts and are host-tested; this file only converts pointer events into panel
 * coordinates and paints. Keeping the arithmetic out of the DOM is what makes the tricky
 * parts (west-edge coupling, right-edge snapping, clamping after snapping) testable at all.
 */

import type { Page, Widget } from '../model/config';
import { PANEL_WIDTH, PANEL_HEIGHT } from '../model/canvas-consts';
import {
  applyDrag,
  applyResize,
  clampToPanel,
  guidesFor,
  hitTest,
  type Zone,
} from './geometry';
import { renderPage, type ValueField } from './render';
import type { Bitmap } from './bitmap';

/** The grid widgets snap to, in panel pixels. 8 is fine enough to place anything and coarse
 *  enough that a drag feels deliberate rather than twitchy. */
export const SNAP_GRID = 8;
/** Smallest widget the editor will produce. Below this a value box cannot hold a glyph and
 *  the user has made something unusable rather than merely small. */
export const MIN_W = 24;
export const MIN_H = 24;

export interface EditorState {
  page: Page;
  /** What each widget's bound value currently reads, for the live preview. */
  values: Record<string, string>;
  /** The widget whose box should be outlined, if any. */
  selectedId?: string;
}

export interface EditorHandle {
  /** Repaint from the current state. */
  redraw(): void;
  /** Recompute the canvas size for its container. */
  resize(): void;
  destroy(): void;
}

export interface EditorOptions {
  canvasEl: HTMLCanvasElement;
  state: EditorState;
  /** Called after a drag or resize commits a change. */
  onChange: (page: Page) => void;
  /** Called when the pointer selects a widget (or clears the selection on empty space). */
  onSelect: (id: string | undefined) => void;
  /**
   * Called on every move DURING a gesture, with the widget as it now stands.
   *
   * WHY THIS IS SEPARATE FROM onChange: onChange commits a finished gesture (and on the
   * device that means a flash write), while this fires continuously so the shell can show the
   * live x/y/width/height. Without it the numbers only updated when the SELECTION changed —
   * so a drag moved the box on screen while the readout showed its old position, which reads
   * as "the drag did nothing" even when it worked.
   */
  onUpdate?: (w: Widget) => void;
}

/* The scale is chosen so the whole panel is visible in the container; the canvas backing
 * store stays at PANEL_WIDTH × PANEL_HEIGHT and CSS scales it. Drawing at panel resolution
 * and letting the browser scale is what keeps the result pixel-crisp — drawing at the
 * on-screen size would blur every glyph edge and hide exactly the defects the preview is
 * for. */
function fitScale(el: HTMLElement): number {
  const rect = el.getBoundingClientRect();
  if (rect.width <= 0 || rect.height <= 0) return 1;
  return Math.min(rect.width / PANEL_WIDTH, rect.height / PANEL_HEIGHT);
}

/** Panel-space coordinates from a pointer event, accounting for the CSS scale. */
function toPanel(canvasEl: HTMLCanvasElement, e: PointerEvent): { x: number; y: number } {
  const r = canvasEl.getBoundingClientRect();
  const sx = PANEL_WIDTH / r.width;
  const sy = PANEL_HEIGHT / r.height;
  return { x: (e.clientX - r.left) * sx, y: (e.clientY - r.top) * sy };
}

/** A widget's bounds, for the selection outline. */
function widgetRect(w: Widget): { x: number; y: number; w: number; h: number } {
  return { x: w.x, y: w.y, w: w.w, h: w.h };
}

export function attachEditor(opts: EditorOptions): EditorHandle {
  const { canvasEl, state, onChange, onSelect, onUpdate } = opts;
  const maybeCtx = canvasEl.getContext('2d');
  if (!maybeCtx) throw new Error('canvas 2d context unavailable');
  /* Bound to a const so the closures below see a non-null type: TS does not carry the
   * narrowing from the guard above into a function called later. */
  const ctx: CanvasRenderingContext2D = maybeCtx;

  canvasEl.width = PANEL_WIDTH;
  canvasEl.height = PANEL_HEIGHT;

  /* One Bitmap reused across repaints. renderPage allocates its own, so this holds the last
   * frame only to avoid re-reading it twice in a single paint. */
  let last: Bitmap | null = null;

  let drag: { id: string; zone: Zone; startX: number; startY: number; origin: Widget } | null =
    null;

  function redraw(): void {
    const fields: ValueField[] = state.page.widgets.map((w) => ({
      /* Each widget's value box IS its geometry, so dragging a widget moves where its value
       * will be stamped on the device too — the preview and the layout cannot disagree. */
      x: w.x,
      y: w.y,
      w: w.w,
      h: w.h,
      alignH: w.font?.align === 'center' ? 'C' : w.font?.align === 'right' ? 'R' : 'L',
      alignV: w.font?.valign === 'middle' ? 'M' : w.font?.valign === 'bottom' ? 'B' : 'T',
      fontId: w.role === 'dynamic' ? 1 : 0,
    }));

    const values = state.page.widgets.map((w) => state.values[w.id]);
    const bmp = renderPage(new Uint8Array(PANEL_WIDTH * PANEL_HEIGHT / 8).fill(0xff),
                           fields, values);
    last = bmp;

    /* Paint the framebuffer as an ImageData at PANEL resolution, then let CSS scale it up.
     * 1 = white and 0 = black, so a set bit is a white pixel with full alpha. */
    const img = ctx.createImageData(bmp.width, bmp.height);
    for (let i = 0, p = 0; i < bmp.data.length; i++) {
      const byte = bmp.data[i]!;
      for (let bit = 7; bit >= 0; bit--) {
        const white = (byte >> bit) & 1;
        const v = white ? 255 : 0;
        img.data[p++] = v; /* r */
        img.data[p++] = v; /* g */
        img.data[p++] = v; /* b */
        img.data[p++] = 255;
      }
    }
    ctx.putImageData(img, 0, 0);

    /* Overlay the selection outline and the guides. This is editor chrome and must NOT be
     * part of the framebuffer — drawing it into the ImageData would corrupt the preview and
     * make a selected widget look different from how it will print. */
    const sel = state.page.widgets.find((w) => w.id === state.selectedId);
    if (sel) {
      const r = widgetRect(sel);
      ctx.save();
      ctx.strokeStyle = '#2563eb';
      ctx.lineWidth = 2;
      ctx.setLineDash([]);
      ctx.strokeRect(r.x + 1, r.y + 1, r.w - 2, r.h - 2);
      /* Handle marks at the corners, so the resize affordance is visible. */
      ctx.fillStyle = '#2563eb';
      const H = 8;
      for (const [hx, hy] of [
        [r.x, r.y], [r.x + r.w, r.y], [r.x, r.y + r.h], [r.x + r.w, r.y + r.h],
      ] as [number, number][]) {
        ctx.fillRect(hx - H / 2, hy - H / 2, H, H);
      }
      ctx.restore();
    }
  }

  function onPointerDown(e: PointerEvent): void {
    const p = toPanel(canvasEl, e);
    const hit = hitTest(state.page.widgets, p.x, p.y);
    if (!hit) {
      onSelect(undefined);
      redraw();
      return;
    }
    const origin = state.page.widgets.find((w) => w.id === hit.id);
    if (!origin) return;
    drag = { id: hit.id, zone: hit.zone, startX: p.x, startY: p.y, origin };
    canvasEl.setPointerCapture(e.pointerId);
    onSelect(hit.id);
    redraw();
  }

  function onPointerMove(e: PointerEvent): void {
    const p = toPanel(canvasEl, e);

    /* No drag in progress: only update the cursor so the resize zones are discoverable. */
    if (!drag) {
      const hit = hitTest(state.page.widgets, p.x, p.y);
      canvasEl.style.cursor = !hit
        ? 'default'
        : hit.zone === 'move'
          ? 'move'
          : `${hit.zone}-resize`;
      return;
    }

    const dx = p.x - drag.startX;
    const dy = p.y - drag.startY;
    const idx = state.page.widgets.findIndex((w) => w.id === drag!.id);
    if (idx < 0) return;

    let updated: Widget;
    if (drag.zone === 'move') {
      const g = guidesFor(state.page.widgets, drag.id);
      updated = applyDrag(drag.origin, { x: drag.startX, y: drag.startY }, dx, dy, {
        grid: SNAP_GRID,
        guidesX: g.x,
        guidesY: g.y,
      });
    } else {
      /* CLAMPED AFTER RESIZING, and that is not covered by applyResize.
       *
       * applyResize only enforces the minimum size — dragging a west edge far enough left
       * puts x at a NEGATIVE panel coordinate and leaves the widget hanging off the canvas.
       * Observed for real: a west-edge drag produced x=-32, which the device would then read
       * as a value box partly outside the panel.
       *
       * The clamp can break the "opposite edge stays pinned" property when a drag goes past
       * the edge, and that is the right trade: the widget staying on the panel matters more
       * than an invariant about an off-panel edge. */
      const raw = applyResize(drag.origin, drag.zone, { x: drag.startX, y: drag.startY }, dx, dy, {
        grid: SNAP_GRID,
        minW: MIN_W,
        minH: MIN_H,
      });
      updated = { ...raw, ...clampToPanel(raw) };
    }
    state.page.widgets[idx] = updated;
    redraw();
    /* Live readout during the gesture — see onUpdate's note in EditorOptions. */
    onUpdate?.(updated);
  }

  function onPointerUp(e: PointerEvent): void {
    if (!drag) return;
    drag = null;
    if (canvasEl.hasPointerCapture(e.pointerId)) canvasEl.releasePointerCapture(e.pointerId);
    /* Commit once, at the end of the gesture — not on every move. The device is a flash
     * write and a full refresh; mid-drag commits would be dozens of them. */
    onChange(state.page);
  }

  canvasEl.addEventListener('pointerdown', onPointerDown);
  canvasEl.addEventListener('pointermove', onPointerMove);
  canvasEl.addEventListener('pointerup', onPointerUp);
  canvasEl.addEventListener('pointercancel', onPointerUp);

  function resize(): void {
    const el = canvasEl.parentElement ?? canvasEl;
    const s = fitScale(el);
    canvasEl.style.width = `${Math.round(PANEL_WIDTH * s)}px`;
    canvasEl.style.height = `${Math.round(PANEL_HEIGHT * s)}px`;
    redraw();
  }

  /* CSS scaling must not smooth the pixels, or the preview lies about crispness. */
  canvasEl.style.imageRendering = 'pixelated';
  resize();

  return {
    redraw,
    resize,
    destroy() {
      canvasEl.removeEventListener('pointerdown', onPointerDown);
      canvasEl.removeEventListener('pointermove', onPointerMove);
      canvasEl.removeEventListener('pointerup', onPointerUp);
      canvasEl.removeEventListener('pointercancel', onPointerUp);
      void last;
    },
  };
}
