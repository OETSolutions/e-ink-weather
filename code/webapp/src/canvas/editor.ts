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

import type { Page, Selection, Widget } from '../model/config';
import { RULE_ID } from '../model/config';
import { PANEL_WIDTH, PANEL_HEIGHT } from '../model/canvas-consts';
import {
  applyDrag,
  applyResize,
  applyRuleDrag,
  clampToPanel,
  guidesFor,
  hitTest,
  ruleHit,
  type Zone,
} from './geometry';
import { renderPage, type ValueField } from './render';
import { fontIdFor } from './face';
import { isIconWidget } from './widget-kind';
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
  /** What is selected: a value box, a divider, or nothing. */
  selection?: Selection;
}

export interface EditorHandle {
  /** Repaint from the current state. */
  redraw(): void;
  /** Recompute the canvas size for its container. */
  resize(): void;
  /** Replace the static layer and repaint — the shell calls this after a rule moves, because
   *  the rules are baked into that layer and it would otherwise show the old line. */
  setLayer(layer: Uint8Array): void;
  /** Force a zoom multiple (1 = fit the container). Returns the zoom now in force. */
  setZoom(z: number | 'fit'): number;
  /** The zoom currently in force. */
  zoom(): number;
  destroy(): void;
}

export interface EditorOptions {
  canvasEl: HTMLCanvasElement;
  state: EditorState;
  /**
   * The static layer to compose onto — labels, units and rules.
   *
   * WITHOUT THIS THE PREVIEW IS MISLEADING: it would show bare numbers floating on white, with
   * none of the labels that make a reading legible, so a layout could look fine in the editor
   * and be confusing on the glass. Defaults to blank, which is correct for a page the user has
   * not given any static art yet.
   */
  staticLayer?: Uint8Array;
  /**
   * Rebuild the static layer from the page's CURRENT rules. Called on every move during a rule
   * drag so the line follows the pointer. The shell owns the label table, so only it can build
   * this — the editor has no labels of its own.
   */
  rebuildLayer?: () => Uint8Array;
  /** Called after a drag or resize commits a change. */
  onChange: (page: Page) => void;
  /** Called when the pointer selects a widget or rule (or clears the selection). */
  onSelect: (selection: Selection | undefined) => void;
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
  /** Called on every move DURING a rule drag, with the rule's new y. */
  onRuleUpdate?: (y: number) => void;
}

/* The scale is chosen so the whole panel is visible in the container; the canvas backing
 * store stays at PANEL_WIDTH × PANEL_HEIGHT and CSS scales it. Drawing at panel resolution
 * and letting the browser scale is what keeps the result pixel-crisp — drawing at the
 * on-screen size would blur every glyph edge and hide exactly the defects the preview is
 * for.
 *
 * MEASURED FROM THE CONTENT BOX, not the border box. The wrap carries padding (and may show a
 * scrollbar), so fitting to its BORDER box would size the canvas to the full box and push it past
 * the padded content area — a horizontal scrollbar and a preview that is subtly too wide. clientWidth
 * excludes the scrollbar; subtracting the padding leaves the space the canvas actually has. */
function fitScale(el: HTMLElement): number {
  const cs = getComputedStyle(el);
  const padX = (parseFloat(cs.paddingLeft) || 0) + (parseFloat(cs.paddingRight) || 0);
  const padY = (parseFloat(cs.paddingTop) || 0) + (parseFloat(cs.paddingBottom) || 0);
  const w = el.clientWidth - padX;
  const h = el.clientHeight - padY;
  if (w <= 0 || h <= 0) return 1;
  return Math.min(w / PANEL_WIDTH, h / PANEL_HEIGHT);
}

/** Panel-space coordinates from a pointer event, accounting for the CSS scale. */
function toPanel(canvasEl: HTMLCanvasElement, e: PointerEvent): { x: number; y: number } {
  const r = canvasEl.getBoundingClientRect();
  const sx = PANEL_WIDTH / r.width;
  const sy = PANEL_HEIGHT / r.height;
  return { x: (e.clientX - r.left) * sx, y: (e.clientY - r.top) * sy };
}

/** An all-white static layer, used when the caller has not supplied one. */
function blankLayer(): Uint8Array {
  return new Uint8Array((PANEL_WIDTH * PANEL_HEIGHT) / 8).fill(0xff);
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
  /* The static art, held here so a rule drag can redraw the line live. */
  let layer: Uint8Array = opts.staticLayer ?? blankLayer();

  let drag:
    | { kind: 'widget'; id: string; zone: Zone; startX: number; startY: number; origin: Widget }
    | { kind: 'rule'; index: number; startY: number; originY: number }
    | null = null;

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
      /* ONE shared rule for face selection — see canvas/face.ts for why role is not
       * the right key and why a free-form size cannot work. */
      fontId: fontIdFor(w),
      /* An icon widget draws a picture from its value (the OWM code) instead of text. */
      kind: isIconWidget(w) ? 'i' as const : 't' as const,
    }));

    const values = state.page.widgets.map((w) => state.values[w.id]);
    const bmp = renderPage(layer, fields, values);
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
    const sel = state.selection;
    if (sel?.kind === 'widget') {
      const sw = state.page.widgets.find((w) => w.id === sel.id);
      if (sw) {
        const r = widgetRect(sw);
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
    } else if (sel?.kind === 'rule') {
      /* A selected rule is drawn as a bright band over its own pixels plus end caps, so it is
       * obvious WHICH line is picked even when two sit close together. */
      const r = state.page.rules?.[sel.index];
      if (r) {
        ctx.save();
        ctx.fillStyle = 'rgba(37, 99, 235, 0.35)';
        ctx.fillRect(r.inset, r.y - 3, PANEL_WIDTH - 2 * r.inset, Math.max(1, r.thickness) + 6);
        ctx.fillStyle = '#2563eb';
        const H = 10;
        ctx.fillRect(r.inset - H / 2, r.y + r.thickness / 2 - H / 2, H, H);
        ctx.fillRect(PANEL_WIDTH - r.inset - H / 2, r.y + r.thickness / 2 - H / 2, H, H);
        ctx.restore();
      }
    }
  }

  function onPointerDown(e: PointerEvent): void {
    const p = toPanel(canvasEl, e);
    /* RULES FIRST — see ruleHit(). */
    const ri = ruleHit(state.page.rules ?? [], p.x, p.y);
    if (ri >= 0) {
      const r = state.page.rules![ri]!;
      drag = { kind: 'rule', index: ri, startY: p.y, originY: r.y };
      canvasEl.setPointerCapture(e.pointerId);
      onSelect({ kind: 'rule', id: RULE_ID, index: ri });
      redraw();
      return;
    }
    const hit = hitTest(state.page.widgets, p.x, p.y);
    if (!hit) {
      onSelect(undefined);
      redraw();
      return;
    }
    const origin = state.page.widgets.find((w) => w.id === hit.id);
    if (!origin) return;
    drag = { kind: 'widget', id: hit.id, zone: hit.zone, startX: p.x, startY: p.y, origin };
    canvasEl.setPointerCapture(e.pointerId);
    onSelect({ kind: 'widget', id: hit.id });
    redraw();
  }

  function onPointerMove(e: PointerEvent): void {
    const p = toPanel(canvasEl, e);

    /* No drag in progress: only update the cursor so the resize zones are discoverable. */
    if (!drag) {
      const overRule = ruleHit(state.page.rules ?? [], p.x, p.y) >= 0;
      const hit = hitTest(state.page.widgets, p.x, p.y);
      canvasEl.style.cursor = overRule
        ? 'ns-resize'
        : !hit
          ? 'default'
          : hit.zone === 'move'
            ? 'move'
            : `${hit.zone}-resize`;
      return;
    }

    if (drag.kind === 'rule') {
      const rules = state.page.rules;
      if (!rules) return;
      /* Snapping, clamping and the vertical-only rule all live in geometry.ts and are
       * host-tested — this only applies the result. */
      const y = applyRuleDrag(drag.originY, p.y - drag.startY, SNAP_GRID);
      rules[drag.index] = { ...rules[drag.index]!, y };
      /* The layer is what actually DRAWS the line, so it must be rebuilt for the move to be
       * visible — otherwise the drag would only register on release. */
      layer = opts.rebuildLayer ? opts.rebuildLayer() : layer;
      redraw();
      opts.onRuleUpdate?.(y);
      return;
    }

    /* Past the rule branch, this is a widget drag. Captured in a local so TypeScript narrows the
     * union: `drag` is a mutable closure variable, so the discriminant is not carried past the
     * return above. */
    const wd = drag;
    if (wd.kind !== 'widget') return;

    const dx = p.x - wd.startX;
    const dy = p.y - wd.startY;
    const idx = state.page.widgets.findIndex((w) => w.id === wd.id);
    if (idx < 0) return;

    let updated: Widget;
    if (wd.zone === 'move') {
      const g = guidesFor(state.page.widgets, wd.id);
      updated = applyDrag(wd.origin, { x: wd.startX, y: wd.startY }, dx, dy, {
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
      const raw = applyResize(wd.origin, wd.zone, { x: wd.startX, y: wd.startY }, dx, dy, {
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

  /* ZOOM. 'fit' scales the whole panel into the container; a number is an explicit multiple
   * (1 = 1:1 device pixels). Being able to go to 1:1 matters: at fit scale on a laptop the
   * panel is roughly 0.7x, and a one-pixel glyph defect is invisible — which is the whole
   * reason to look at the preview rather than trust the numbers. */
  let zoomMode: number | 'fit' = 'fit';

  function currentScale(): number {
    if (zoomMode === 'fit') return fitScale(canvasEl.parentElement ?? canvasEl);
    return zoomMode;
  }

  /* CSS scaling MUST NOT SMOOTH the pixels when the canvas is UP-scaled, or the preview lies about
   * crispness — a blurred preview would hide exactly the single-pixel defects the preview exists to
   * reveal.
   *
   * BUT IT MUST SMOOTH WHEN THE CANVAS IS DOWN-scaled, which is the common case (a fit on a laptop
   * is ~0.7x). `pixelated` is nearest-neighbour, so at 0.7x it simply DROPS two of every three
   * pixels: a 1-2 px rule loses most of its ink and reads as a broken line, and thin glyph strokes
   * break up — reported as "the text looks terrible, some aliasing problem". Smoothing (the browser
   * default) averages instead, so a downscaled rule stays a continuous grey line and the glyphs stay
   * legible. The output is still 1-bit; this only decides how the browser resamples it on the way to
   * the screen. So the choice is made by DIRECTION: pixelated at 1:1 and above, smooth below. */
  function applyRendering(s: number): void {
    canvasEl.style.imageRendering = s >= 1 ? 'pixelated' : 'auto';
  }

  /* FIT MUST BE MEASURED AFTER LAYOUT, AND RE-MEASURED WHEN THE CONTAINER CHANGES.
   *
   * fitScale() reads the PARENT's content box, and attachEditor() runs BEFORE the canvas is
   * appended to the document (the caller builds the DOM, then appends). Detached, there is no
   * parent, so the fit could not be computed and the canvas was pinned at PANEL_WIDTH x
   * PANEL_HEIGHT CSS pixels — 920px wide in a ~690px column, overflowing and clipped at a negative
   * x, while the label still said "Zoom fit". So the caller must call resize() once after appending
   * (main.ts does), and this observer keeps the fit correct as the column reflows.
   *
   * The observer is attached LAZILY on the first resize that has a parent, because at attach time
   * there is no parent to observe. It watches the PARENT: fitScale() is defined in terms of it, and
   * the canvas's own box is the OUTPUT of this calculation, so observing the canvas would be
   * circular (and would loop). */
  const ro = new ResizeObserver(() => {
    if (zoomMode === 'fit') resize();
  });
  let observing = false;

  function resize(): void {
    if (!observing && canvasEl.parentElement) {
      ro.observe(canvasEl.parentElement);
      observing = true;
    }
    const s = currentScale();
    canvasEl.style.width = `${Math.round(PANEL_WIDTH * s)}px`;
    canvasEl.style.height = `${Math.round(PANEL_HEIGHT * s)}px`;
    applyRendering(s);
    redraw();
  }

  return {
    redraw,
    resize,
    setLayer(next) {
      layer = next;
      redraw();
    },
    setZoom(z) {
      zoomMode = z;
      resize();
      return currentScale();
    },
    zoom() {
      return currentScale();
    },
    destroy() {
      ro.disconnect();
      canvasEl.removeEventListener('pointerdown', onPointerDown);
      canvasEl.removeEventListener('pointermove', onPointerMove);
      canvasEl.removeEventListener('pointerup', onPointerUp);
      canvasEl.removeEventListener('pointercancel', onPointerUp);
      void last;
    },
  };
}
