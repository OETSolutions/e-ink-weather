/**
 * App shell.
 *
 * Sections: the location picker (FR-24), the layout editor (FR-20/21), and the property
 * inspector (FR-22). All three edit ONE in-memory document that Save pushes to the device —
 * the device stores a single config, so independent save paths would race and the last one to
 * finish would silently discard the others' changes.
 *
 * ORDERING MATTERS IN THIS FILE: everything is built, then wired, then appended. Appending
 * early and wiring later looks harmless and is not — a listener closing over a variable
 * declared below it throws at runtime, and the page renders blank with no visible reason.
 */

import './app.css';
import { createMapPicker, type MapPickerHandle } from './ui/map-picker';
import { hasPosition } from './ui/location';
import { attachEditor, type EditorHandle, type EditorState } from './canvas/editor';
import { createPropertyPanel, type PropertyPanelHandle } from './ui/property-panel';
import { listEntities } from './data/ha';
import { formatPlaceholder } from './data/format';
import { evaluateAlerts } from './alerts/rules';
import { emptyConfig, type Config, type Page, type Widget } from './model/config';

/** Where the device's API lives. Served from the device itself, so a relative URL is correct
 *  both on the device and when the dev server proxies to it. */
const API = '';

/** How long to wait for the device before running offline. Long enough for a slow wifi
 *  association, short enough that a dead device does not look like a broken app. */
const LOAD_TIMEOUT_MS = 5000;

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

/** A button with a real listener. `onClick` in a props object does NOT work: Object.assign
 *  sets an `onClick` property, which is not the DOM's `onclick`, so the handler is dropped and
 *  the button silently does nothing. */
function button(label: string, onClick: () => void): HTMLButtonElement {
  const b = document.createElement('button');
  b.type = 'button';
  b.textContent = label;
  b.addEventListener('click', onClick);
  return b;
}

/**
 * Starter widgets, placed where readings already are, used ONLY when the stored page has none.
 *
 * A device that already has a layout keeps it — overwriting a user's arrangement because they
 * opened the config app would be destructive. These exist so a fresh device shows something
 * meaningful the moment it is set up (FR-17's stated use case).
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
      alerts: [{ op: 'gt', threshold: 100, level: 'severe' }],
    },
    {
      id: 'indoor',
      x: 48, y: 256, w: 420, h: 110,
      role: 'dynamic',
      binding: { kind: 'ha', entityId: 'sensor.upstairs_hallway_temperature' },
      format: { decimals: 1, suffix: '°F', fallback: '--' },
      font: { size: 64, align: 'left', valign: 'top' },
      alerts: [{ op: 'gt', threshold: 100, level: 'severe' }],
    },
  ];
}

/**
 * The preview text for each widget.
 *
 * `probe` is the value the alert rules are evaluated against. NaN means "preview no alert",
 * which is how the toggle turns off — and NaN rather than a low number on purpose: NaN is
 * exactly what an unavailable reading produces, and the rules must return 'none' for it. So
 * the toggle's "off" state exercises the real guard rather than a special case.
 */
function previewValues(page: Page, probe: number): Record<string, string> {
  const out: Record<string, string> = {};
  for (const w of page.widgets) {
    if (w.role !== 'dynamic') continue;
    const level = evaluateAlerts(w.alerts, probe);
    /* A firing alert changes what the user needs to see, so the preview shows the alert word —
     * the same thing the firmware draws into the alert bar. */
    out[w.id] = level !== 'none' ? level.toUpperCase() : formatPlaceholder(w.format);
  }
  return out;
}

/**
 * Fetch the config from the device. Returns null when unreachable — a normal state (wrong
 * address, device asleep), never a reason to throw the page away.
 */
async function loadConfig(): Promise<Config | null> {
  const ac = new AbortController();
  const timer = setTimeout(() => ac.abort(), LOAD_TIMEOUT_MS);
  try {
    const r = await fetch(`${API}/api/config`, { cache: 'no-store', signal: ac.signal });
    clearTimeout(timer);
    if (!r.ok) return null;
    const doc = (await r.json()) as Partial<Config>;
    const base = emptyConfig();

    /* MERGE FIELD BY FIELD, treating an ABSENT field as absent.
     *
     * `{...base, ...doc}` looks equivalent and is not: the device's stored default document
     * carries no `pages` key, so the spread copies an explicit `undefined` over the default and
     * every reader of doc.pages then throws. Only keys actually present may override. */
    const merged: Config = { ...base, ...doc } as Config;
    if (!Array.isArray(doc.pages) || doc.pages.length === 0) merged.pages = base.pages;
    merged.location = { ...base.location, ...(doc.location ?? {}) };
    merged.ha = { ...base.ha, ...(doc.ha ?? {}) };
    return merged;
  } catch {
    clearTimeout(timer);
    return null;
  }
}

type SaveResult =
  | { ok: true; restarting: boolean; warning?: string }
  | { ok: false; error: string };

async function saveConfig(doc: Config): Promise<SaveResult> {
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

  /* ---- the page being edited ---- */
  const page: Page = doc.pages[0] ??
    { id: 'main', name: 'Main', refreshSeconds: 900, weight: 1, widgets: [] };
  /* The device's OWN page objects carry no `widgets` key — it stores what it schedules, not
   * what it draws. An absent array is therefore the normal shape and must become an empty one
   * rather than being read as a length. */
  if (!Array.isArray(page.widgets)) page.widgets = [];
  if (page.widgets.length === 0) page.widgets = starterWidgets();

  let alertProbe = NaN; /* no alert previewed until the toggle is ticked */
  const editorState: EditorState = { page, values: previewValues(page, alertProbe) };

  /* ---- DOM, built first ---- */
  const status = el('p', { className: 'status' });
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

  if (!loaded) {
    status.textContent = 'Could not reach the device. You can still edit; Save will retry.';
    status.classList.add('err');
  } else if (hasPosition(doc.location.latitude, doc.location.longitude)) {
    status.textContent =
      'Position loaded from the device. If it was not chosen by hand it is an approximate ' +
      'guess from the device’s public IP — drag the pin to correct it.';
  } else {
    status.textContent = 'No position set yet. Drag the pin, or type the coordinates.';
  }

  const canvasEl = el('canvas', { className: 'panel' }) as HTMLCanvasElement;
  const panelHost = el('div', { className: 'panelHost' });
  const sel = el('p', { className: 'hint' });
  const alertToggle = el('label', { className: 'toggle' }) as HTMLLabelElement;
  const alertBox = el('input', { type: 'checkbox' }) as HTMLInputElement;
  alertToggle.append(alertBox, el('span', {}, 'Preview a firing alert'));
  const saveBtn = button('Save to device', () => void doSave());

  function describe(id: string | undefined): void {
    const w = id ? page.widgets.find((x) => x.id === id) : undefined;
    if (!w) {
      sel.textContent = id
        ? ''
        : 'Nothing selected. Click a value box to move or resize it.';
      return;
    }
    sel.textContent =
      `${w.id}: x=${Math.round(w.x)} y=${Math.round(w.y)} w=${Math.round(w.w)} h=${Math.round(w.h)}`;
  }

  /* ---- wiring ---- */
  let editor: EditorHandle;

  const panel: PropertyPanelHandle = createPropertyPanel({
    host: panelHost,
    onChange: () => {
      /* A format or alert change alters the preview text, so refresh it — otherwise the panel
       * would edit a widget while the canvas kept painting the old value. */
      editorState.values = previewValues(page, alertProbe);
      editor.redraw();
    },
  });

  const picker: MapPickerHandle = createMapPicker({
    mapEl, latInput, lonInput, statusEl: mapStatus,
    initial: { lat: doc.location.latitude, lon: doc.location.longitude },
    onChange: () => { /* Save reads the live position. */ },
  });

  editor = attachEditor({
    canvasEl,
    state: editorState,
    onChange: () => { /* Commit happens on Save, not per gesture. */ },
    onSelect: (id) => {
      editorState.selectedId = id;
      describe(id);
      panel.show(id ? page.widgets.find((w) => w.id === id) : undefined);
      editor.redraw();
    },
    onUpdate: (w) => {
      /* Live readout during a gesture. Without it the numbers only refreshed when the
       * SELECTION changed, so a working drag looked like it had done nothing. */
      sel.textContent =
        `${w.id}: x=${Math.round(w.x)} y=${Math.round(w.y)} w=${Math.round(w.w)} h=${Math.round(w.h)}`;
    },
  });

  alertBox.addEventListener('change', () => {
    /* 999 fires a normal-range rule; NaN previews nothing, exercising the real "unavailable
     * never alarms" guard. */
    alertProbe = alertBox.checked ? 999 : NaN;
    editorState.values = previewValues(page, alertProbe);
    panel.show(editorState.selectedId
      ? page.widgets.find((w) => w.id === editorState.selectedId)
      : undefined);
    editor.redraw();
  });

  async function doSave(): Promise<void> {
    const p = picker.getPosition();
    doc = {
      ...doc,
      location: { ...doc.location, latitude: p.lat, longitude: p.lon, zipCode: zipInput.value },
      pages: [page, ...doc.pages.slice(1)],
    };
    saveBtn.disabled = true;
    const r = await saveConfig(doc);
    saveBtn.disabled = false;
    if (r.ok) {
      status.classList.remove('err');
      if (r.restarting) {
        /* A powerMode change restarts the device, so say so rather than leaving the user
         * watching a dropped connection. */
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
  }

  /* ---- append LAST, after every declaration above ---- */
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
    el('div', { className: 'editorLayout' },
       el('div', { className: 'editorWrap' }, canvasEl),
       panelHost),
    sel,
    alertToggle,
    saveBtn,
  );

  describe(undefined);
  panel.show(undefined);
  picker.invalidate();

  window.addEventListener('resize', () => {
    picker.invalidate();
    editor.resize();
  });

  /* The HA entity list is fetched once, asynchronously. A failure is not fatal and is explained
   * in the panel rather than shown as an empty picker — "no entities" would read as "your Home
   * Assistant is empty", which is the worst possible message. */
  void (async () => {
    const base = doc.ha?.baseUrl ?? '';
    if (!base) {
      panel.setEntities([], 'No Home Assistant URL configured, so type the entity id by hand.');
      return;
    }
    const res = await listEntities({ baseUrl: base, token: '' });
    if (res.ok) {
      panel.setEntities(
        res.entities.map((e) => ({ entityId: e.entityId, friendlyName: e.friendlyName })),
      );
    } else {
      panel.setEntities([], `Could not list Home Assistant entities: ${res.error}`);
    }
  })();
}

const root = document.getElementById('app');
if (root) void mount(root);
