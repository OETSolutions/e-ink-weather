/**
 * App shell.
 *
 * Two sections so far: the location picker (FR-24) and the layout editor (FR-20/21). Both
 * edit one in-memory document and are pushed to the device together by Save — the device
 * stores a single config, so two independent save paths would race each other and the last
 * one to finish would silently discard the other's changes.
 *
 * The canvas editor and the property panel (Task 19) mount into this same shell.
 */

import './app.css';
import { createMapPicker, type MapPickerHandle } from './ui/map-picker';
import { hasPosition } from './ui/location';
import { attachEditor, type EditorHandle, type EditorState } from './canvas/editor';
import { emptyConfig, type Config, type Page, type Widget } from './model/config';

/** Where the device's API lives. Served from the device itself, so a relative URL is correct
 *  both on the device and when the dev server proxies to it. */
const API = '';

function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  props: Partial<HTMLElementTagNameMap[K]> = {},
  ...children: (Node | string)[]
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  Object.assign(node, props);
  for (const c of children) node.append(c);
  return node;
}

/**
 * A widget placed where an existing reading already is, so the device shows something
 * meaningful the moment the web app is wired up (FR-17's stated use case). These are only
 * used when the stored page has NO widgets — a device that already has a layout keeps it.
 */
function starterWidgets(): Widget[] {
  return [
    {
      id: 'outdoor',
      x: 48, y: 76, w: 420, h: 110,
      role: 'dynamic',
      binding: { kind: 'owm-current', owmField: 'temp' },
      format: { decimals: 1, suffix: '°F', fallback: '--' },
      font: { size: 64, align: 'left', valign: 'top' },
    },
    {
      id: 'indoor',
      x: 48, y: 256, w: 420, h: 110,
      role: 'dynamic',
      binding: { kind: 'ha', entityId: 'sensor.upstairs_hallway_temperature' },
      format: { decimals: 1, suffix: '°F', fallback: '--' },
      font: { size: 64, align: 'left', valign: 'top' },
    },
  ];
}

/** Sample values so the editor preview is not blank before data binding exists (Task 19).
 *  Clearly placeholders: the preview must not look like a live reading. */
const PREVIEW_VALUES: Record<string, string> = { outdoor: '68.4', indoor: '41.2' };

/** Fetch the config from the device. Returns null when unreachable, which is a normal state
 *  (wrong address, device asleep) and must not throw the page away. */
async function loadConfig(): Promise<Config | null> {
  try {
    /* BOUNDED, because an unbounded fetch does not fail — it HANGS, and everything after it
     * in mount() then never runs. Observed for real: a device that had dropped off the wifi
     * left the page blank for 30+ s with no error, which reads as "the app is broken" rather
     * than "the device is unreachable". A timeout turns that into the offline path, where the
     * editor still works and Save can be retried. */
    const ac = new AbortController();
    const t = setTimeout(() => ac.abort(), 5000);
    const r = await fetch(`${API}/api/config`, { cache: 'no-store', signal: ac.signal });
    clearTimeout(t);
    if (!r.ok) return null;
    const doc = (await r.json()) as Partial<Config>;
    const base = emptyConfig();
    /* MERGE FIELD BY FIELD, and treat an ABSENT field as absent.
     *
     * A plain `{...base, ...doc}` looks equivalent and is not: the device's stored default
     * document carries no `pages` key at all, so the spread copies an explicit `undefined`
     * over the default and every reader of `doc.pages` then throws. Only keys that are
     * actually present may override the baseline. */
    const merged: Config = { ...base, ...doc } as Config;
    if (!Array.isArray(doc.pages) || doc.pages.length === 0) merged.pages = base.pages;
    if (!doc.location || typeof doc.location.latitude !== 'number') {
      merged.location = { ...base.location, ...(doc.location ?? {}) };
    }
    return merged;
  } catch {
    return null;
  }
}

async function saveConfig(doc: Config): Promise<
  { ok: true; restarting: boolean; warning?: string } | { ok: false; error: string }
> {
  try {
    const r = await fetch(`${API}/api/config`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(doc),
    });
    if (!r.ok) {
      const text = await r.text().catch(() => '');
      return { ok: false, error: text || `HTTP ${r.status}` };
    }
    const body = (await r.json().catch(() => ({}))) as { restarting?: boolean; warning?: string };
    return { ok: true, restarting: !!body.restarting, warning: body.warning };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : 'Could not reach the device' };
  }
}

async function mount(root: HTMLElement): Promise<void> {
  const loaded = await loadConfig();
  let doc: Config = loaded ?? emptyConfig();
  let online = loaded !== null;

  const status = el('p', { className: 'status' });

  /* ---- location (FR-24) ---- */
  const mapEl = el('div', { className: 'map' });
  const mapStatus = el('p', { className: 'map-status' });
  const latInput = el('input', {
    type: 'text', inputMode: 'decimal', autocomplete: 'off', id: 'lat', placeholder: 'e.g. 41.7759',
  }) as HTMLInputElement;
  const lonInput = el('input', {
    type: 'text', inputMode: 'decimal', autocomplete: 'off', id: 'lon', placeholder: 'e.g. -111.8068',
  }) as HTMLInputElement;
  const zipInput = el('input', {
    type: 'text', id: 'zip', placeholder: 'e.g. 84341', autocomplete: 'off',
  }) as HTMLInputElement;
  zipInput.value = doc.location?.zipCode ?? '';

  if (!online) {
    status.textContent =
      'Could not reach the device. You can still lay out the page; Save will retry.';
    status.classList.add('err');
  } else if (hasPosition(doc.location?.latitude ?? 0, doc.location?.longitude ?? 0)) {
    status.textContent =
      'Position loaded from the device. If it was not chosen by hand it is an approximate ' +
      'guess from the device’s public IP — drag the pin to correct it.';
  } else {
    status.textContent = 'No position set yet. Drag the pin, or type the coordinates.';
  }

  let picker: MapPickerHandle;

  /* ---- layout editor (FR-20/21) ---- */
  const page: Page = doc.pages[0] ?? { id: 'main', name: 'Main', refreshSeconds: 900, weight: 1, widgets: [] };
  /* The device's OWN page objects carry no `widgets` key — it stores what it schedules, not
   * what it draws, because drawing is the web app's job. So this is not a corrupt document,
   * it is the normal shape, and an absent array must become an empty one rather than being
   * read as a length. */
  if (!Array.isArray(page.widgets)) page.widgets = [];
  if (page.widgets.length === 0) page.widgets = starterWidgets();

  const editorState: EditorState = { page, values: PREVIEW_VALUES };
  const canvasEl = el('canvas', { className: 'panel' }) as HTMLCanvasElement;
  const sel = el('p', { className: 'hint' });
  let editor: EditorHandle;

  function describeSelection(id: string | undefined): void {
    if (!id) {
      sel.textContent = 'Nothing selected. Click a value box to move or resize it.';
      return;
    }
    const w = page.widgets.find((x) => x.id === id);
    if (!w) {
      sel.textContent = '';
      return;
    }
    /* Showing the rounded numbers is deliberate: these are the exact values stored and sent,
     * so a user comparing the preview against the glass can check them. */
    sel.textContent =
      `${id}: x=${Math.round(w.x)} y=${Math.round(w.y)} w=${Math.round(w.w)} h=${Math.round(w.h)}`;
  }

  /* ---- save ---- */
  const saveBtn = el('button', { type: 'button' }, 'Save to device') as HTMLButtonElement;
  saveBtn.addEventListener('click', async () => {
    const p = picker.getPosition();
    doc = {
      ...doc,
      location: { ...(doc.location ?? {}), latitude: p.lat, longitude: p.lon, zipCode: zipInput.value },
      pages: [page, ...doc.pages.slice(1)],
    };
    saveBtn.disabled = true;
    const r = await saveConfig(doc);
    saveBtn.disabled = false;
    if (r.ok) {
      online = true;
      /* A powerMode change restarts the device, so say what is happening rather than leaving
       * the user watching a dropped connection. */
      status.classList.remove('err');
      if (r.restarting) {
        status.textContent = r.warning
          ? `Saved. ${r.warning}`
          : 'Saved. The display is restarting to apply the power setting.';
        status.classList.add('err');
      } else {
        status.textContent = 'Saved. The display will refresh with the new layout.';
      }
    } else {
      status.textContent = `Could not save: ${r.error}`;
      status.classList.add('err');
    }
  });

  root.append(
    el('h1', {}, 'E-Ink Weather'),
    status,
    el('h2', {}, 'Display location'),
    el('p', { className: 'sub' },
       'Used to fetch the weather. Drag the pin for a precise position, or type the coordinates.'),
    el('div', { className: 'fields' },
       el('div', {}, el('label', { htmlFor: 'lat' }, 'Latitude'), latInput),
       el('div', {}, el('label', { htmlFor: 'lon' }, 'Longitude'), lonInput)),
    mapEl,
    mapStatus,
    el('div', { className: 'zip' },
       el('label', { htmlFor: 'zip' }, 'Zip code (general area, optional)'), zipInput),
    el('h2', {}, 'Layout'),
    el('p', { className: 'sub' },
       'Drag a value box to move it, or drag its edge to resize. This is the real 1-bit output ' +
       'the panel will show.'),
    el('div', { className: 'editorWrap' }, canvasEl),
    sel,
    saveBtn,
  );

  picker = createMapPicker({
    mapEl, latInput, lonInput, statusEl: mapStatus,
    initial: { lat: doc.location?.latitude ?? 0, lon: doc.location?.longitude ?? 0 },
    onChange: () => { /* Save reads the live position. */ },
  });

  /* Describe a widget from its LIVE values, so the readout tracks a drag rather than showing
   * whatever it was when the box was first selected. */
  function describeWidget(w: Widget): void {
    sel.textContent =
      `${w.id}: x=${Math.round(w.x)} y=${Math.round(w.y)} w=${Math.round(w.w)} h=${Math.round(w.h)}`;
  }

  editor = attachEditor({
    canvasEl,
    state: editorState,
    onChange: () => { /* Commit happens on Save, not per gesture. */ },
    onSelect: (id) => {
      editorState.selectedId = id;
      describeSelection(id);
      editor.redraw();
    },
    onUpdate: describeWidget,
  });
  describeSelection(undefined);

  picker.invalidate();
  window.addEventListener('resize', () => {
    picker.invalidate();
    editor.resize();
  });
}

const root = document.getElementById('app');
if (root) void mount(root);
