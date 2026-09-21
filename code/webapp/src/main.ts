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

import './ui/theme.css';
import './ui/app.css';
import { createMapPicker, type MapPickerHandle } from './ui/map-picker';
import { hasPosition } from './ui/location';
import { attachEditor, type EditorHandle, type EditorState } from './canvas/editor';
import { createPropertyPanel, type PropertyPanelHandle } from './ui/property-panel';
import { listEntities } from './data/ha';
import { formatPlaceholder } from './data/format';
import { evaluateAlerts } from './alerts/rules';
import { defaultLayout, artworkForPage } from './presets/default-layout';
import { buildStaticLayer } from './canvas/render';
import { emptyConfig, type Config, type Page, type Widget } from './model/config';
import { exportConfig, importConfig, configFilename, downloadText } from './transfer/config';
import { uploadArtwork } from './transfer/artwork';
import { getAuth, putAuth, type AuthState } from './transfer/device';

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
 * The starting point for a device with no layout yet (FR-17).
 *
 * Uses the shipped preset rather than an ad-hoc set of boxes, so a fresh device shows the
 * dashboard the design was actually made for. A device that ALREADY has widgets keeps them —
 * overwriting a user's arrangement because they opened the app would be destructive.
 */
function starterPage(): Page {
  return defaultLayout().pages[0]!;
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

/**
 * Push every page's static layer to the device (FR-15, FR-1).
 *
 * WHY THIS IS SEPARATE FROM THE CONFIG PUT: the config says WHERE the value boxes are; this is
 * the picture each page's values are stamped onto. The device only stamps readings into boxes the
 * web app defines, so without this its "static layer" is the factory boot mark and none of the
 * labels exist on the glass. The device ties the two together by page, so a config with 2 pages
 * and artwork for 2 pages renders page 2 on page 2's picture.
 *
 * ONE SET, NOT ONE PER PAGE, and that is the fix for rotation drawing the wrong labels: a layer
 * is uploaded for EVERY page in the document, in page order, so a page added or removed changes
 * what is stored rather than shifting which picture each page gets.
 *
 * ORDER MATTERS: artwork FIRST, then the config. The device renders on a config PUT, so the other
 * order would draw one frame with the new boxes over the old picture, and the user would see a
 * momentarily wrong panel before the artwork landed.
 */
/**
 * Render ONE page's static layer: its labels and rules, baked to a 1 bpp bitmap.
 *
 * MODULE SCOPE, not inside mount(), because both the editor (which draws page 0) and the upload
 * path need it, and the upload runs for every page in the document. A page with no art entry
 * yields a blank layer rather than another page's — see artworkForPage().
 */
function buildPageLayer(pageIndex: number): Uint8Array {
  const art = artworkForPage(pageIndex);
  return buildStaticLayer(
    art.labels.map((l) => ({ x: l.x, y: l.y, text: l.text, font: l.font })),
    art.rules.map((r) => ({ y: r.y, thickness: r.thickness, inset: r.inset })),
  ).data;
}

async function pushArtwork(doc: Config): Promise<SaveResult> {
  /* One layer per page, built from the page's own art. A page with no art table entry gets a
   * blank layer rather than a copy of another page's — a missing picture is honest, the wrong
   * picture is a lie about which page you are looking at. */
  const layers: (Uint8Array | null)[] = doc.pages.map((_, i) => buildPageLayer(i));
  const up = await uploadArtwork(layers);
  if (!up.ok) return { ok: false, error: up.error ?? 'The artwork upload failed' };
  return { ok: true, restarting: false };
}

async function mount(root: HTMLElement): Promise<void> {
  const loaded = await loadConfig();
  let doc: Config = loaded ?? emptyConfig();

  /* ---- the page being edited ---- */
  /* `let`, not `const`: loading a file REPLACES the document, and the editor holds a reference
   * to this page, so it must be repointable. */
  let page: Page = doc.pages[0] ??
    { id: 'main', name: 'Main', refreshSeconds: 900, weight: 1, widgets: [] };
  /* The device's OWN page objects carry no `widgets` key — it stores what it schedules, not
   * what it draws. An absent array is therefore the normal shape and must become an empty one
   * rather than being read as a length. */
  if (!Array.isArray(page.widgets)) page.widgets = [];
  if (page.widgets.length === 0) page.widgets = starterPage().widgets;

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

  /* Zoom controls. 'Fit' is the default because it shows the whole panel; 1:1 is there because
   * a one-pixel defect is invisible at fit scale, and the point of looking at the preview is to
   * see exactly what the glass will show. */
  const zoomOut = button('−', () => void applyZoom('out'));
  const zoomIn = button('+', () => void applyZoom('in'));
  const zoomFit = button('Fit', () => void applyZoom('fit'));
  const zoomLabel = el('span', { className: 'panelReadout' });
  const bitBadge = el('span', { className: 'badge' }, '1-bit, no greys');
  const alertToggle = el('label', { className: 'toggle' }) as HTMLLabelElement;
  const alertBox = el('input', { type: 'checkbox' }) as HTMLInputElement;
  alertToggle.append(alertBox, el('span', {}, 'Preview a firing alert'));
  const saveBtn = button('Save to device', () => void doSave());

  /* ---- file save / load (FR-26) ---- */
  const saveFileBtn = button('Save to file', () => {
    downloadText(exportConfig(doc), configFilename());
    status.textContent = 'Saved a copy of this layout to your downloads.';
    status.classList.remove('err');
  });
  const fileInput = el('input', { type: 'file', accept: 'application/json,.json' }) as HTMLInputElement;
  fileInput.addEventListener('change', () => {
    const f = fileInput.files?.[0];
    if (!f) return;
    void (async () => {
      try {
        const next = importConfig(await f.text());
        /* Adopt the loaded document WHOLESALE and re-seed the page the editor holds. The
         * editor captured `page` at mount, so it must be repointed — otherwise the canvas would
         * keep showing the old layout while the document held the new one, and Save would then
         * write a mixture of the two. */
        doc = next;
        page = doc.pages[0]!;
        if (!Array.isArray(page.widgets)) page.widgets = [];
        editorState.page = page;
        editorState.selectedId = undefined;
        editorState.values = previewValues(page, alertProbe);
        picker.setPosition({ lat: doc.location.latitude, lon: doc.location.longitude });
        zipInput.value = doc.location.zipCode ?? '';
        panel.show(undefined);
        editor.redraw();
        describe(undefined);
        status.textContent = `Loaded “${f.name}”. Review it, then Save to device.`;
        status.classList.remove('err');
      } catch (e) {
        status.textContent = e instanceof Error ? e.message : 'Could not read that file.';
        status.classList.add('err');
      }
      fileInput.value = '';   /* allow re-picking the same file */
    })();
  });

  /* ---- optional API auth (FR-31) ---- */
  const authBox = el('input', { type: 'checkbox' }) as HTMLInputElement;
  const authLabel = el('label', { className: 'toggle' }) as HTMLLabelElement;
  authLabel.append(authBox, el('span', {}, 'Require a token to change settings'));
  const authRow = el('div', { className: 'authRow' });
  let authToken = '';

  function describeAuth(): void {
    authRow.replaceChildren();
    if (!authToken) {
      authRow.append(el('p', { className: 'hint' },
        'Turn this on to get a token. Anyone without it can still change this display.'));
      return;
    }
    const code = el('code', { className: 'token' }, authToken);
    authRow.append(
      el('p', { className: 'hint' }, 'Token — keep it, you will not see it again:'),
      code,
      button('Copy', () => void navigator.clipboard?.writeText(authToken)),
    );
  }

  authBox.addEventListener('change', () => {
    void (async () => {
      /* SEND THE TOKEN WHEN WE HAVE IT, or a device with protection ON would refuse the very
       * request that turns it off — the user would untick the box, get a 401, and watch the box
       * spring back with no explanation. The token was returned when protection was enabled and
       * is held in memory for exactly this. */
      const r = await putAuth({ enabled: authBox.checked }, authToken ? { token: authToken } : {});
      if (!r.ok) {
        /* Revert the box: a checkbox that stays ticked after a failed write would claim the
         * device is protected when it is not. */
        authBox.checked = !authBox.checked;
        status.textContent = `Could not change the auth setting: ${r.error}`;
        status.classList.add('err');
        return;
      }
      applyAuthState(r.value);
      status.textContent = r.value.enabled
        ? 'API token protection is ON. Keep the token safe.'
        : 'API token protection is OFF.';
      status.classList.remove('err');
    })();
  });

  function applyAuthState(a: AuthState): void {
    authBox.checked = a.enabled;
    authToken = a.token;
    describeAuth();
  }

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

  /* The labels and rules are the LAYOUT's, not the device's — the device is layout-independent
   * and only stamps values into boxes, so the static art is the web app's job (FR-1).
   *
   * ONE LAYER PER PAGE, because the device rotates pages on its own and each page's readings are
   * stamped onto that page's own background. A single shared layer meant page 2's numbers were
   * drawn under page 1's labels — seen on the glass. `staticLayer` (page 0) is what the EDITOR
   * draws, since the editor edits one page at a time; `allLayers` is what gets uploaded. */
  let staticLayer = buildPageLayer(0);

  editor = attachEditor({
    canvasEl,
    state: editorState,
    staticLayer,
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
    /* The button carries its own state. A message at the top of the page is easy to miss when
     * the user is looking at the button they just pressed, and "did it save?" is the one
     * question this app must never leave ambiguous. */
    const setBtn = (st: 'saving' | 'saved' | 'failed' | 'idle') => {
      saveBtn.disabled = st === 'saving';
      if (st === 'idle') {
        saveBtn.removeAttribute('data-state');
        saveBtn.textContent = 'Save to device';
      } else {
        saveBtn.dataset.state = st;
        saveBtn.textContent =
          st === 'saving' ? 'Saving…' : st === 'saved' ? 'Saved' : 'Save failed — retry';
      }
    };
    setBtn('saving');
    /* The static layer FIRST, then the config: the device renders on a config PUT, so the other
     * order would draw one frame with the new boxes over the old picture. */
    const up = await pushArtwork(doc);
    const r = up.ok ? await saveConfig(doc) : up;
    if (r.ok) {
      setBtn('saved');
      setTimeout(() => setBtn('idle'), 2500);
      status.classList.remove('err', 'busy');
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
      setBtn('failed');
      setTimeout(() => setBtn('idle'), 4000);
      status.textContent = `Could not save: ${r.error}`;
      status.classList.add('err');
    }
  }

  let zoomValue: number | 'fit' = 'fit';
  function applyZoom(z: 'in' | 'out' | 'fit'): void {
    if (z === 'fit') {
      zoomValue = 'fit';
    } else {
      const base = zoomValue === 'fit' ? editor.zoom() : zoomValue;
      const next = z === 'in' ? base * 1.25 : base / 1.25;
      /* Clamped to something useful: below 0.25 the panel is unreadable, above 4x a single
       * glyph fills the viewport and the layout cannot be judged. */
      zoomValue = Math.min(4, Math.max(0.25, next));
    }
    const s2 = editor.setZoom(zoomValue);
    zoomLabel.textContent = `Zoom ${(s2 * 100).toFixed(0)}%`;
  }
  zoomLabel.textContent = 'Zoom fit';

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
    el('div', { className: 'toolbar' },
       zoomOut, zoomIn, zoomFit, bitBadge, zoomLabel),
    el('div', { className: 'editorLayout' },
       el('div', { className: 'editorWrap' }, canvasEl),
       panelHost),
    sel,
    alertToggle,
    el('div', { className: 'actions' }, saveBtn, saveFileBtn),
    el('h2', {}, 'Layout file'),
    el('p', { className: 'sub' },
       'Save a copy, or load one you saved earlier. Loading does not touch the device until ' +
       'you press Save.'),
    el('div', { className: 'actions' }, fileInput),
    el('h2', {}, 'Access'),
    authLabel,
    authRow,
  );

  describe(undefined);
  panel.show(undefined);
  picker.invalidate();

  window.addEventListener('resize', () => {
    picker.invalidate();
    editor.resize();
  });

  /* Read the auth state so the checkbox reflects the DEVICE, not the page's default. A
   * checkbox that starts unticked on a device that is actually protected would invite the user
   * to "fix" a setting that is already correct. */
  void (async () => {
    const r = await getAuth();
    if (r.ok) applyAuthState(r.value);
    else describeAuth();
  })();

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
