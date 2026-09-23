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
import { searchEntities } from './data/ha';
import { previewTextWithLive } from './data/format';
import { defaultLayout, artworkForPage } from './presets/default-layout';
import { buildStaticLayer } from './canvas/render';
import { findPlacement, fontSizeForBox, freshWidgetId } from './canvas/placement';
import { emptyConfig, MAX_WIDGETS_PER_PAGE, RULE_ID, LABEL_ID, type Config, type Page, type Selection, type Widget } from './model/config';
import { exportConfig, importConfig, configFilename, downloadText } from './transfer/config';
import { uploadArtwork } from './transfer/artwork';
import { getAuth, getValuesInfo, putAuth, getSecrets, putSecrets, requestPage, type AuthState } from './transfer/device';

/** Where the device's API lives. Served from the device itself, so a relative URL is correct
 *  both on the device and when the dev server proxies to it. */
const API = '';

/** How long to wait for the device before running offline. Long enough for a slow wifi
 *  association, short enough that a dead device does not look like a broken app. */
const LOAD_TIMEOUT_MS = 5000;

/**
 * The most pages the display can rotate through, mirrored from the firmware's LAYOUT_MAX_PAGES
 * (lib/layout/include/layout_model.h).
 *
 * A TRUE MIRROR: the device's layout_config_t holds exactly this many page entries and TRUNCATES
 * the rest without a word, so a document with more would preview more pages than the glass ever
 * shows. Enforced while editing so the limit is visible rather than discovered on the panel.
 */
const MAX_PAGES = 8;

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
function starterPage(index: number): Page {
  const d = defaultLayout();
  return d.pages[index] ?? d.pages[0]!;
}

/**
 * The preview text for each widget.
 *
 * Delegates to previewTextWithLive() in data/format.ts so the exact string the panel will show is
 * testable — see that function for why the alert case and the live-value precedence matter
 * (NFR-4: the preview must match the panel bit-for-bit, and it did not). `live` holds the values
 * the DEVICE last resolved, keyed by widget id (FR-27).
 */
function previewValues(page: Page, probe: number, live: Record<string, string> = {}): Record<string, string> {
  const out: Record<string, string> = {};
  for (const w of page.widgets) {
    if (w.role !== 'dynamic') continue;
    out[w.id] = previewTextWithLive(w, probe, live);
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

/** How many times a save re-sends the document when the device reports it is mid-refresh.
 *
 * The device refuses a config PUT with 503 while a refresh has its free DRAM pinned at the render
 * floor, because a document that size cannot be parsed until the heap recovers — and the artwork
 * push immediately before a save REQUESTS that refresh, so the save's own PUT is what lands in the
 * window. The device already waits out most of it; this retry covers the remainder so the user's
 * save still succeeds rather than surfacing a busy message for a self-inflicted, transient state. */
const SAVE_RETRIES = 3;
const SAVE_RETRY_MS = 1200;

async function putConfigOnce(doc: Config): Promise<Response> {
  return fetch(`${API}/api/config`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(doc),
  });
}

async function saveConfig(doc: Config): Promise<SaveResult> {
  try {
    let r = await putConfigOnce(doc);
    for (let i = 0; i < SAVE_RETRIES && r.status === 503; i++) {
      await new Promise((res) => setTimeout(res, SAVE_RETRY_MS));
      r = await putConfigOnce(doc);
    }
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
 * Give a page the dividers its art defaults to, when the document carries none.
 *
 * WHY THIS IS NEEDED AT ALL: a device configured before rules moved into the document has no
 * `rules` key, but buildPageLayer() still DRAWS the art table's lines. Left alone, the user would
 * see dividers on the preview that cannot be grabbed — the drawn set and the editable set would
 * be different, which is the most confusing possible version of "the line will not move". Seeding
 * the page from the same table makes them one set, and the next save persists them so the
 * document genuinely describes the layout.
 */
function ensurePageRules(p: Page, index: number): void {
  if (!Array.isArray(p.rules)) {
    p.rules = starterPage(index).rules?.map((r) => ({ ...r }))
      ?? artworkForPage(index).rules.map((r) => ({ ...r }));
  }
}

/**
 * Seed a page's LABELS from the same starter page its widgets would come from.
 *
 * A page authored before labels moved into the document has no `labels` key, but something still
 * DRAWS its headings — so without this the user would see headings on the preview that cannot be
 * selected, renamed or moved, which is the most confusing possible version of "the heading will
 * not move".
 *
 * THE SAME SOURCE AS THE WIDGETS, deliberately: switchPage seeds an unarranged page's widgets from
 * starterPage(), and a page beyond the shipped presets gets the first page's widgets. Taking the
 * labels from artworkForPage() instead (which is empty past the presets) would give such a page
 * eighteen boxes and no headings — a layout whose readings are unlabelled. `starterPage(0)` and
 * `starterPage(1)` carry exactly the labels `artworkForPage` does, so pages 0 and 1 are unchanged.
 *
 * AN EMPTY ARRAY IS RESPECTED, NOT RE-SEEDED: a page whose headings the user deliberately deleted
 * must stay blank. `undefined` means "never authored", `[]` means "the user chose none", and
 * conflating the two would make a deletion undo itself on the next visit.
 */
function ensurePageLabels(p: Page, index: number): void {
  if (!Array.isArray(p.labels)) {
    p.labels = starterPage(index).labels?.map((l) => ({ ...l }))
      ?? artworkForPage(index).labels.map((l) => ({ ...l }));
  }
}

/**
 * Render ONE page's static layer: its labels and rules, baked to a 1 bpp bitmap.
 *
 * MODULE SCOPE, not inside mount(), because both the editor (which draws the page being edited)
 * and the upload path need it, and the upload runs for every page in the document.
 *
 * BOTH LABELS AND RULES COME FROM THE CONFIG, falling back to the art table only when the page
 * carries none — which is the case for a document authored before each moved into the config.
 * Labels used to be drawn from the art table alone, which is exactly why a hard-coded heading
 * could not be renamed: the picture on the preview was not the document, it was a preset. A page
 * with no art entry and no labels yields a blank layer rather than another page's — a missing
 * picture is honest, the wrong picture is a lie about which page you are looking at.
 */
function buildPageLayer(page: Page, pageIndex: number): Uint8Array {
  const art = artworkForPage(pageIndex);
  const rules = page.rules ?? art.rules;
  const labels = page.labels ?? art.labels;
  return buildStaticLayer(
    labels.map((l) => ({ x: l.x, y: l.y, text: l.text, font: l.font })),
    rules.map((r) => ({ y: r.y, thickness: r.thickness, inset: r.inset })),
  ).data;
}

async function pushArtwork(doc: Config): Promise<SaveResult> {
  /* One layer per page, built from the page's own art. A page with no art table entry gets a
   * blank layer rather than a copy of another page's — a missing picture is honest, the wrong
   * picture is a lie about which page you are looking at. */
  const layers: (Uint8Array | null)[] = doc.pages.map((p, i) => buildPageLayer(p, i));
  const up = await uploadArtwork(layers);
  if (!up.ok) return { ok: false, error: up.error ?? 'The artwork upload failed' };
  return { ok: true, restarting: false };
}

async function mount(root: HTMLElement): Promise<void> {
  const loaded = await loadConfig();
  let doc: Config = loaded ?? emptyConfig();

  /* ---- the page being edited ----
   *
   * WHY THERE IS A PAGE INDEX AND NOT JUST A PAGE REFERENCE: the device rotates its pages on
   * its own schedule, so the page the glass is showing is frequently NOT the page the user means
   * to edit. Editing is therefore addressed by index — the page selector and the value poll both
   * compare THAT index against the device's own page, and the editor repoints when it changes.
   *
   * `page` is derived from this index rather than captured, so every reader (the panel, the layer
   * rebuild, the value poll) sees the page actually being edited. A captured reference is the
   * exact bug that made the preview show another page's layout. */
  let pageIndex = 0;
  function pageAt(i: number): Page {
    return doc.pages[i] ?? doc.pages[0] ??
      { id: 'main', name: 'Main', refreshSeconds: 900, weight: 1, widgets: [] };
  }
  let page: Page = pageAt(pageIndex);
  /* The device's OWN page objects carry no `widgets` key — it stores what it schedules, not
   * what it draws. An absent array is therefore the normal shape and must become an empty one
   * rather than being read as a length. */
  if (!Array.isArray(page.widgets)) page.widgets = [];
  /* A page that has never been arranged gets the shipped starter ONCE, and which pages those are
   * is remembered for the session. WITHOUT THIS, switching to a page the user had deliberately
   * emptied put the starter boxes back on every visit — the edit would appear to undo itself, and
   * Save would then write a layout the user had deleted. */
  const seededPages = new Set<number>();
  if (page.widgets.length === 0) { page.widgets = starterPage(pageIndex).widgets; seededPages.add(pageIndex); }
  ensurePageRules(page, pageIndex);
  ensurePageLabels(page, pageIndex);

  let alertProbe = NaN; /* no alert previewed until the toggle is ticked */
  /* The device's last resolved values, keyed by widget id (FR-27). Empty until asked for, and
   * the preview falls back to placeholders until then — which is what the panel shows before its
   * own first fetch, so the two agree either way. */
  let liveValues: Record<string, string> = {};
  const editorState: EditorState = { page, values: previewValues(page, alertProbe, liveValues) };

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

  /* ---- adding and removing entities and dividers ----
   *
   * ADDING A BOX WAS IMPOSSIBLE BEFORE: the editor could only move boxes that already existed,
   * so the only way to get a new reading on the glass was to hand-edit the config JSON. The
   * device already parses whatever widgets the document contains (lib/layout/src/widgets.c), so
   * the missing half was entirely here.
   *
   * A NEW BOX IS PLACED IN A FREE SPOT, not at 0,0. Dropping every new box in the same corner
   * would stack them exactly on top of each other — invisible, and impossible to tell apart.
   * canvas/placement.ts finds the first clear spot and is host-tested; this only calls it. */

  function addWidget(): void {
    /* THE DEVICE CAPS WIDGETS PER PAGE, so the editor must too. Past the cap the panel would
     * look complete in the preview and the extra boxes would simply not appear on the glass —
     * the device drops them without a word. Refusing here makes the limit visible while
     * editing. */
    if (page.widgets.length >= MAX_WIDGETS_PER_PAGE) {
      status.textContent =
        `This page already holds the maximum of ${MAX_WIDGETS_PER_PAGE} boxes the display can `
        + 'draw. Remove one to add another.';
      status.classList.add('err');
      return;
    }
    const spot = findPlacement(page.widgets, 40, page.rules ?? []);
    if (!spot) {
      status.textContent = 'No free space left for another box. Remove one first.';
      status.classList.add('err');
      return;
    }
    const nw: Widget = {
      id: freshWidgetId(page.widgets),
      x: spot.x, y: spot.y, w: spot.w, h: spot.h,
      role: 'dynamic',
      binding: { kind: 'owm-current', owmField: 'temp' },
      format: { decimals: 1, suffix: '°F', fallback: '--' },
      /* Sized to the box the placement found, not a fixed 64: the step-down can leave a gap
       * shorter than the 64 px face's line height, and the numeral would be clipped. */
      font: { size: fontSizeForBox(spot.h), align: 'left', valign: 'top' },
    };
    page.widgets.push(nw);
    editorState.values = previewValues(page, alertProbe, liveValues);
    editorState.selection = { kind: 'widget', id: nw.id };
    panel.show(editorState.selection, page);
    describe(editorState.selection);
    editor.redraw();
    status.classList.remove('err');
    status.textContent = `Added box “${nw.id}”. Bind it to a reading, then Save to device.`;
  }

  function addRule(): void {
    if (!page.rules) page.rules = [];
    /* THE MIDDLE, NOT THE TOP: a new line at y=0 would sit on the panel's edge under the first
     * label. 340 is the middle of 680, a visible place the user can immediately drag from. */
    page.rules.push({ y: 340, thickness: 2, inset: 40 });
    editor.setLayer(rebuildEditedLayer());
    editorState.selection = { kind: 'rule', id: RULE_ID, index: page.rules.length - 1 };
    panel.show(editorState.selection, page);
    describe(editorState.selection);
    editor.redraw();
    status.classList.remove('err');
    status.textContent = 'Added a divider. Drag it on the panel, or set its Y exactly here.';
  }

  function addLabel(): void {
    if (!page.labels) page.labels = [];
    /* A NEW HEADING LANDS IN THE CLEAR, not at 0,0 — stacked headings would be indistinguishable
     * and one would hide the others. findPlacement() is for widgets (it respects widgets AND
     * rules); a heading is one short line of body text, so a simpler rule is enough: try the left
     * margin down the panel at a few anchor Ys and take the first that does not overlap an
     * existing heading. This is deliberately in main.ts rather than geometry.ts: it is a
     * placement POLICY for a new object, not hit-testing arithmetic the tests exercise.
     *
     * The size is the body size the shipped headings use, so a new one looks like the rest. */
    const FONT = 20;
    const used = page.labels;
    const overlaps = (y: number): boolean =>
      used.some((l) => Math.abs(l.y - y) < FONT);
    let y = 40;
    for (const candidate of [40, 100, 200, 300, 400, 500, 600]) {
      if (!overlaps(candidate)) { y = candidate; break; }
    }
    page.labels.push({ x: 40, y, text: 'HEADING', font: FONT });
    editor.setLayer(rebuildEditedLayer());
    editorState.selection = { kind: 'label', id: LABEL_ID, index: page.labels.length - 1 };
    panel.show(editorState.selection, page);
    describe(editorState.selection);
    editor.redraw();
    status.classList.remove('err');
    status.textContent = 'Added a heading. Type its text here or drag it on the panel.';
  }

  const addBoxBtn = button('Add box', addWidget);
  const addRuleBtn = button('Add divider', addRule);
  const addLabelBtn = button('Add heading', addLabel);

  /* ---- pages: add, delete, rename, and the two intervals (FR-15, FR-25) ----
   *
   * WHY THIS IS HERE: the device and the document always supported several pages, but the UI had
   * no way to create or remove one — the shipped layout's two pages were the only pages a user
   * could ever have, and the rotation interval could only be changed by hand-editing JSON. Two
   * model fields were worse than useless without a control: `updateSeconds` (how often the display
   * wakes and re-fetches — FR-9) and each page's `refreshSeconds` (how long that page stays up —
   * FR-15) were written by the app, stored by the device and IGNORED by the UI, which is exactly
   * the "a setting that only changes a label" defect class this project has hit before.
   *
   * THE LIMIT IS THE DEVICE'S. layout_config_t holds LAYOUT_MAX_PAGES (8) pages and truncates the
   * rest silently, so the button refuses past 8 with an explanation rather than adding a page the
   * display would never rotate to. */

  /** A fresh page for "Add page".
   *
   * WIDGETS, RULES AND LABELS ARE ALL OMITTED, so switchPage() seeds all three from the same
   * starter page. An explicit `widgets: [] / labels: []` is the WRONG shape here: `[]` means "the
   * user chose none" (the rule ensurePageLabels/ensurePageRules respect so a deletion is not
   * undone), so an empty array would seed the boxes while leaving the headings out — a page of
   * unlabelled readings, which is worse than the template it was meant to start from. Omitting them
   * says "never authored", which is exactly what a brand-new page is. */
  function newPage(existing: Page[]): Page {
    const n = existing.length + 1;
    return {
      id: `page${n}-${Date.now().toString(36).slice(-4)}`,
      name: `Page ${n}`,
      refreshSeconds: 900,
      weight: 1,
    } as Page;
  }

  function addPage(): void {
    if (doc.pages.length >= MAX_PAGES) {
      status.textContent =
        `The display rotates at most ${MAX_PAGES} pages. Remove one to add another.`;
      status.classList.add('err');
      return;
    }
    doc.pages.push(newPage(doc.pages));
    seededPages.delete(doc.pages.length - 1);   /* a brand-new page may be seeded if left empty */
    buildPageOptions();
    switchPage(doc.pages.length - 1);
    status.classList.remove('err');
    status.textContent = `Added ${doc.pages[doc.pages.length - 1]!.name}. Save to put it on the display.`;
  }

  function deletePage(): void {
    if (doc.pages.length <= 1) {
      status.textContent = 'The display needs at least one page.';
      status.classList.add('err');
      return;
    }
    const removed = page.name;
    doc.pages.splice(pageIndex, 1);
    /* THE DEVICE'S SCHEDULE IS NOT RENUMBERED — it is the config's page ARRAY that changes, and
     * the device re-derives its rotation from the new array on the next refresh. So the edited
     * index just clamps back into range; a saved document with a shorter page list is what the
     * device needs, and it takes effect when the user saves. */
    pageIndex = Math.min(pageIndex, doc.pages.length - 1);
    page = pageAt(pageIndex);
    if (!Array.isArray(page.widgets)) page.widgets = [];
    ensurePageRules(page, pageIndex);
    ensurePageLabels(page, pageIndex);
    liveValues = {};
    editorState.page = page;
    editorState.selection = undefined;
    editorState.values = previewValues(page, alertProbe, liveValues);
    editor.setLayer(rebuildEditedLayer());
    panel.show(undefined, page);
    editor.redraw();
    describe(undefined);
    buildPageOptions();
    describePage();
    pageSettings.refresh();
    status.classList.remove('err');
    status.textContent = `Removed “${removed}”. Save to update the display.`;
  }

  const addPageBtn = button('Add page', addPage);
  const delPageBtn = button('Delete this page', deletePage);

  /* ---- page settings: name, rotation dwell, and the refresh interval ----
   *
   * The name and the two numbers are edited into the DOCUMENT's own fields, so Save carries them
   * with the rest of the config and the device acts on them — see the block comment above. */
  const pageNameInput = el('input', { type: 'text', id: 'pageName', autocomplete: 'off' }) as HTMLInputElement;
  pageNameInput.addEventListener('input', () => {
    page.name = pageNameInput.value;
    /* Keep the selector's label in step so the page list does not name a page the user just
     * renamed. Only the edited page's option is touched — rebuilding the whole list on each
     * keystroke would reset the select and lose the caret. */
    const opt = pageSel.options[pageIndex];
    if (opt) opt.textContent = `${pageIndex + 1}. ${page.name || '(unnamed)'}`;
  });

  const pageDwellInput = el('input', {
    type: 'number', id: 'pageDwell', min: '30', max: '604800', step: '30',
  }) as HTMLInputElement;
  pageDwellInput.addEventListener('input', () => {
    const n = Number(pageDwellInput.value);
    /* The device clamps to [30, 604800] (LAYOUT_MIN/MAX_INTERVAL_SECONDS) and refuses a document
     * whose sum would overflow. Clamping HERE keeps the number shown equal to the number stored —
     * the same rule every other control follows. */
    if (!Number.isFinite(n)) return;
    page.refreshSeconds = Math.min(604800, Math.max(30, Math.round(n)));
  });
  pageDwellInput.addEventListener('blur', () => {
    pageDwellInput.value = String(page.refreshSeconds);
  });

  const refreshInput = el('input', {
    type: 'number', id: 'refreshSeconds', min: '30', max: '604800', step: '30',
  }) as HTMLInputElement;
  refreshInput.addEventListener('input', () => {
    const n = Number(refreshInput.value);
    if (!Number.isFinite(n)) return;
    doc.updateSeconds = Math.min(604800, Math.max(30, Math.round(n)));
  });
  refreshInput.addEventListener('blur', () => {
    refreshInput.value = String(doc.updateSeconds);
  });

  /** Repoint the settings fields at the page now being edited. Called from switchPage and after a
   *  page is added or removed; without it the fields would keep the previous page's values while
   *  editing the new one — a control showing a number the document does not hold. */
  const pageSettings = {
    refresh(): void {
      pageNameInput.value = page.name ?? '';
      pageDwellInput.value = String(page.refreshSeconds ?? 900);
      refreshInput.value = String(doc.updateSeconds ?? 900);
      delPageBtn.disabled = doc.pages.length <= 1;
      addPageBtn.disabled = doc.pages.length >= MAX_PAGES;
    },
  };

  const rotationHint = el('p', { className: 'hint' },
    'Rotation is per page: the display shows each page for its own dwell time, then moves on. '
    + 'With 2 pages at 900 s each, it turns every 15 minutes.');


  /* ---- which page is being edited ----
   *
   * THE DEVICE ROTATES PAGES ON ITS OWN, so without this the editor was pinned to page 1
   * (index 0) and the reported symptoms followed directly: the preview showed page 0's boxes
   * while the glass showed page 1's ("the layout doesn't match"), the poll compared the device's
   * page against a fixed 0 and never applied, so every reading stayed on its "--" placeholder,
   * and a box added to page 0 waited up to a full rotation (15 min by default) to appear.
   *
   * The selector makes the edited page an explicit choice; the readout beneath it says which page
   * the GLASS is on right now, so the mismatch is visible instead of mystifying. */
  const pageSel = el('select', { id: 'editPage' }) as HTMLSelectElement;
  const pageHint = el('p', { className: 'hint' });
  /* -1 means "the device has not told us yet". Kept separate from 0 because page 0 is a real
   * answer and must not be confused with "unknown". */
  let devicePage = -1;

  function buildPageOptions(): void {
    pageSel.replaceChildren();
    doc.pages.forEach((p, i) => {
      pageSel.append(el('option', { value: String(i) }, `${i + 1}. ${p.name}`));
    });
    pageSel.value = String(pageIndex);
  }

  /** Say which page the display is showing, so the preview and the glass can be reconciled. */
  function describePage(): void {
    if (devicePage < 0) {
      pageHint.classList.remove('warn');
      pageHint.textContent = 'Checking which page the display is showing…';
      return;
    }
    /* The device may report a page index this document no longer has — the user can load a file
     * with fewer pages while the device still holds the old schedule. pageAt() would then fall back
     * to page 0 and the message would name the wrong page, so say what is actually known instead. */
    if (devicePage >= doc.pages.length) {
      pageHint.textContent =
        'The display is on a page this layout no longer has. Save to bring them back in step.';
      pageHint.classList.add('warn');
      return;
    }
    const devName = pageAt(devicePage).name;
    if (devicePage === pageIndex) {
      pageHint.classList.remove('warn');
      pageHint.textContent = `The display is on this page (${devicePage + 1}. ${devName}).`;
      return;
    }
    /* The values here are REAL and current — the device resolves every page, not just the one it
     * is drawing — so this is a note about the glass, not a warning that the preview is stale. It
     * still earns a mention because a change saved now reaches the panel only when that page's
     * rotation slot comes round (or when the user asks for the page explicitly). */
    pageHint.classList.remove('warn');
    pageHint.textContent =
      `You are editing page ${pageIndex + 1}; the display is showing page ${devicePage + 1} `
      + `(${devName}). Readings here are live. To see this page on the glass now, use the button `
      + 'below.';
  }

  /**
   * Repoint the editor at another page.
   *
   * EVERY reader of the edited page is re-seeded here: the editor's state (whose `page` the canvas
   * draws), the static layer (whose labels and rules differ per page), the inspector, and the value
   * poll. Repointing some and not others is how the preview and the document drift apart. */
  function switchPage(i: number): void {
    if (i < 0 || i >= doc.pages.length || i === pageIndex) return;
    pageIndex = i;
    page = pageAt(i);
    if (!Array.isArray(page.widgets)) page.widgets = [];
    /* Seed only a page the user has never arranged, and only once — see seededPages. A page that is
     * empty because the user emptied it must STAY empty. */
    if (page.widgets.length === 0 && !seededPages.has(i)) {
      page.widgets = starterPage(i).widgets;
      seededPages.add(i);
    }
    ensurePageRules(page, i);
    ensurePageLabels(page, i);
    /* Drop the previous page's readings. Ids can recur across pages, and a stale value under a
     * reused id would be another page's number drawn as if it were this page's. */
    liveValues = {};
    editorState.page = page;
    editorState.selection = undefined;
    editorState.values = previewValues(page, alertProbe, liveValues);
    editor.setLayer(rebuildEditedLayer());
    panel.show(undefined, page);
    editor.redraw();
    describe(undefined);
    pageSel.value = String(i);
    pageSettings.refresh();
    describePage();
    /* Ask again: the device may already be on this page, in which case the boxes fill in now; if
     * it is not, the poll's own page guard keeps the placeholders honest. */
    pollValues();
  }
  pageSel.addEventListener('change', () => switchPage(Number(pageSel.value)));

  /**
   * Ask the display to show the page being edited, now.
   *
   * WITHOUT THIS A CHANGE TO A NON-SCHEDULED PAGE WAS INVISIBLE: the device draws whichever page
   * its rotation schedule picks, so a box moved onto a page that is not "next" showed up on the
   * glass only when that page's slot came round — up to a full rotation later, which the user
   * reasonably read as "my change did nothing". The device takes the page on its next tick and
   * returns to its schedule afterwards, so this is a one-shot nudge, not a mode.
   */
  const showPageBtn = button('Show this page on the display', () => {
    void (async () => {
      const r = await requestPage(pageIndex);
      if (!r.ok) {
        status.textContent = `Could not ask the display to change page: ${r.error}`;
        status.classList.add('err');
        return;
      }
      status.classList.remove('err');
      status.textContent =
        `The display is switching to page ${pageIndex + 1} (${page.name}). Its readings fill in here once it has.`;
      /* The poll's page guard would otherwise stop after it saw the old page; restart it so the
       * boxes fill in as soon as the device reports the new one. */
      pollValues();
    })();
  });

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
        pageIndex = 0;
        buildPageOptions();
        liveValues = {};
        page = pageAt(0);
        if (!Array.isArray(page.widgets)) page.widgets = [];
        /* A LOADED FILE IS RESPECTED EXACTLY — no starter seeding. The seeding at mount exists for
         * a device that has no layout yet (FR-17); a file the user chose to load is explicit
         * intent, and putting boxes back into a page it deliberately left empty would be this app
         * overriding the document it just read. seededPages is cleared so the next page switch
         * follows the SAME rule as the file rather than the pre-load session's. */
        seededPages.clear();
        /* A loaded file that predates rules has no `rules` key, and the layer still draws the art
         * table's lines — so an empty array would show dividers that cannot be grabbed, exactly the
         * state ensurePageRules() exists to prevent. */
        ensurePageRules(page, 0);
        ensurePageLabels(page, 0);
        editorState.page = page;
        editorState.selection = undefined;
        editorState.values = previewValues(page, alertProbe, liveValues);
        picker.setPosition({ lat: doc.location.latitude, lon: doc.location.longitude });
        zipInput.value = doc.location.zipCode ?? '';
        editor.setLayer(rebuildEditedLayer());
        panel.show(undefined, page);
        editor.redraw();
        describe(undefined);
        pageSettings.refresh();
        describePage();
        pollValues();
        status.textContent = `Loaded “${f.name}”. Review it, then Save to device.`;
        status.classList.remove('err');
      } catch (e) {
        status.textContent = e instanceof Error ? e.message : 'Could not read that file.';
        status.classList.add('err');
      }
      fileInput.value = '';   /* allow re-picking the same file */
    })();
  });

  /* ---- OpenWeatherMap product (FR-6) ----
   *
   * WHY THIS IS A CONTROL AND NOT A HIDDEN DEFAULT: the two products differ in whether official
   * severe-weather alerts exist at all — One Call 3.0 carries them, the free 2.5 pair does not
   * (FR-7). A user who has the subscription but cannot find the switch would silently get the
   * free tier and an alert bar that says "NO ALERTS (PRODUCT)" forever.
   *
   * 'Auto' is the default and is labelled as a probe, not a fixed choice: it asks One Call 3.0
   * once and falls back on the documented not-subscribed response. Saying so matters, because
   * 'Auto' is what most keys want and the user should not think they have to know their own
   * subscription status to leave it alone. */
  const owmSel = el('select', { id: 'owmProduct' }) as HTMLSelectElement;
  for (const [v, label] of [
    ['auto', 'Automatic (probe One Call 3.0, fall back)'],
    ['onecall3', 'One Call 3.0 (official alerts)'],
    ['legacy', 'Free current + 5-day forecast (no official alerts)'],
  ] as const) {
    owmSel.append(el('option', { value: v }, label));
  }
  owmSel.value = doc.owmProduct;
  owmSel.addEventListener('change', () => {
    doc.owmProduct = owmSel.value as Config['owmProduct'];
    describeProduct();
  });
  const productHint = el('p', { className: 'hint' });
  function describeProduct(): void {
    /* State the alert consequence for the CURRENT selection, because it is the one difference a
     * user will notice on the glass and the one they cannot see until they have already saved. */
    productHint.textContent = owmSel.value === 'legacy'
      ? 'Official severe-weather alerts are not available on the free products, so the alert bar '
        + 'will say so rather than staying blank.'
      : owmSel.value === 'onecall3'
        ? 'Requires the "One Call by Call" subscription. Without it every weather fetch fails.'
        : 'One Call 3.0 is probed once; without the subscription the free products are used and '
          + 'official alerts are reported as unavailable.';
  }
  describeProduct();

  /* ---- credentials (FR-30) ----
   *
   * WHY THIS SECTION EXISTS: the OWM key and the HA URL/token could previously only be set by
   * the captive portal on FIRST BOOT. A device already on the network had no way to be given
   * them, so its Home Assistant boxes stayed "--" with no route to fix that short of a factory
   * reset — which is exactly what the user hit.
   *
   * THE FIELDS START BLANK AND ARE NEVER PRE-FILLED WITH THE STORED VALUE. The device will not
   * send a secret back (see its /api/secrets), and pre-filling would also mean a Save re-sent
   * whatever was in the box. Blank therefore reads as "leave it alone", and the status line
   * beside each field says whether the device already holds one — which is the only thing the
   * user needs from it. */
  const owmKeyInput = el('input', {
    type: 'password', id: 'owmKey', autocomplete: 'off', placeholder: 'leave blank to keep the stored key',
  }) as HTMLInputElement;
  const haUrlInput = el('input', {
    type: 'text', id: 'haUrl', autocomplete: 'off', placeholder: 'http://homeassistant.local:8123',
  }) as HTMLInputElement;
  const haTokenInput = el('input', {
    type: 'password', id: 'haToken', autocomplete: 'off', placeholder: 'leave blank to keep the stored token',
  }) as HTMLInputElement;
  const owmKeyStatus = el('span', { className: 'badge' }, '');
  const haTokenStatus = el('span', { className: 'badge' }, '');
  const credStatus = el('p', { className: 'hint' });
  const secretsBtn = button('Save credentials', () => void doSaveSecrets());

  function describeSecrets(s: { owmKey: boolean; haToken: boolean; haUrl: string } | null): void {
    if (!s) {
      owmKeyStatus.textContent = 'not loaded';
      haTokenStatus.textContent = 'not loaded';
      return;
    }
    owmKeyStatus.textContent = s.owmKey ? 'configured' : 'not set';
    haTokenStatus.textContent = s.haToken ? 'configured' : 'not set';
    /* The HA URL IS returned by the device, so it can be shown — and filling it means the
     * entity picker below has something to query without the user retyping the address. */
    if (s.haUrl) haUrlInput.value = s.haUrl;
  }

  async function doSaveSecrets(): Promise<void> {
    const body: { owmKey?: string; haUrl?: string; haToken?: string } = {};
    if (owmKeyInput.value.trim()) body.owmKey = owmKeyInput.value.trim();
    if (haUrlInput.value.trim()) body.haUrl = haUrlInput.value.trim();
    if (haTokenInput.value.trim()) body.haToken = haTokenInput.value.trim();
    if (Object.keys(body).length === 0) {
      credStatus.textContent = 'Nothing to save — fill at least one field.';
      credStatus.classList.add('err');
      return;
    }
    secretsBtn.disabled = true;
    const r = await putSecrets(body);
    secretsBtn.disabled = false;
    if (!r.ok) {
      credStatus.textContent = `Could not save credentials: ${r.error}`;
      credStatus.classList.add('err');
      return;
    }
    /* Clear the secret fields on success so a later Save cannot re-send them, then re-read the
     * device's state so the badges reflect what is actually stored. */
    owmKeyInput.value = '';
    haTokenInput.value = '';
    credStatus.classList.remove('err');
    credStatus.textContent = 'Saved. The display is refreshing with the new credentials.';
    const after = await getSecrets();
    describeSecrets(after.ok ? after.value : null);
    /* The picker searched through the device, which now holds the token, so clear any
     * "enter a token" note and let the next keystroke search. */
    panel.setEntities([]);
  }

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

  function describe(s: Selection | undefined): void {
    if (s?.kind === 'rule') {
      const r = page.rules?.[s.index];
      sel.textContent = r ? `divider: y=${Math.round(r.y)} thickness=${r.thickness}` : '';
      return;
    }
    if (s?.kind === 'label') {
      const l = page.labels?.[s.index];
      sel.textContent = l ? `heading: “${l.text}” x=${Math.round(l.x)} y=${Math.round(l.y)}` : '';
      return;
    }
    const w = s?.kind === 'widget' ? page.widgets.find((x) => x.id === s.id) : undefined;
    if (!w) {
      sel.textContent = s
        ? ''
        : 'Nothing selected. Click a value box, a divider or a heading to move it.';
      return;
    }
    sel.textContent =
      `${w.id}: x=${Math.round(w.x)} y=${Math.round(w.y)} w=${Math.round(w.w)} h=${Math.round(w.h)}`;
  }

  /* ---- wiring ---- */
  let editor: EditorHandle;
  /* The panel is built before searchEntitiesDebounced() is declared (everything here is built,
   * then wired, then appended), so the panel's search callback reads this slot rather than closing
   * over a function that does not exist yet. */
  let entitySearch: (q: string) => void = () => {};

  /* Rebuild just the page being edited, from its CURRENT rules. Indexed by the page being
   * edited — not a fixed 0, and not `indexOf(page)`, which is -1 for a page synthesized for a
   * device document that carried no such entry. */
  function rebuildEditedLayer(): Uint8Array {
    return buildPageLayer(page, pageIndex);
  }

  const panel: PropertyPanelHandle = createPropertyPanel({
    host: panelHost,
    onEntitySearch: (q) => entitySearch(q),
    onChange: () => {
      /* A format or alert change alters the preview text, so refresh it — otherwise the panel
       * would edit a widget while the canvas kept painting the old value. */
      editorState.values = previewValues(page, alertProbe, liveValues);
      editor.redraw();
    },
    onRuleChange: () => {
      /* The rule is BAKED INTO THE LAYER, so the panel cannot simply change a number: the line
       * on the canvas comes from the layer, and without this the edit would be invisible until a
       * reload. This is the same class of defect as a control that shows a value it never
       * stores. */
      editor.setLayer(rebuildEditedLayer());
    },
    onLabelChange: () => {
      /* Exactly the same reason as onRuleChange: a heading is drawn FROM the layer, so renaming
       * or moving one must rebuild it — otherwise the text field would show the new name while the
       * canvas went on painting the old, which is the "control shows a value it did not store"
       * defect in its most confusing form. */
      editor.setLayer(rebuildEditedLayer());
      describe(editorState.selection);
    },
    onDelete: (s) => {
      if (s.kind === 'rule') {
        page.rules = (page.rules ?? []).filter((_, i) => i !== s.index);
        editor.setLayer(rebuildEditedLayer());
      } else if (s.kind === 'label') {
        page.labels = (page.labels ?? []).filter((_, i) => i !== s.index);
        editor.setLayer(rebuildEditedLayer());
      } else {
        const i = page.widgets.findIndex((w) => w.id === s.id);
        if (i >= 0) page.widgets.splice(i, 1);
        editorState.values = previewValues(page, alertProbe, liveValues);
      }
      editorState.selection = undefined;
      panel.show(undefined, page);
      describe(undefined);
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
   * drawn under page 1's labels — seen on the glass. The layer the EDITOR draws is rebuilt from
   * the edited page whenever a rule moves; `pushArtwork` builds them all again on Save. */
  editor = attachEditor({
    canvasEl,
    state: editorState,
    staticLayer: rebuildEditedLayer(),
    rebuildLayer: rebuildEditedLayer,
    /* Commit happens on Save, not per gesture — but the INSPECTOR must be re-shown. Its geometry
     * fields are built once from the widget and never updated during a drag, so without this a
     * drag moved the box on the panel while the X/Y/W/H numbers beside it kept the values they
     * had before — the same "the drag did nothing" reading that the live readout line exists to
     * prevent, in the one place the user is most likely to look to confirm a move. */
    onChange: () => {
      panel.show(editorState.selection, page);
    },
    onSelect: (s) => {
      editorState.selection = s;
      describe(s);
      panel.show(s, page);
      editor.redraw();
    },
    onUpdate: (w) => {
      /* Live readout during a gesture. Without it the numbers only refreshed when the
       * SELECTION changed, so a working drag looked like it had done nothing. */
      sel.textContent =
        `${w.id}: x=${Math.round(w.x)} y=${Math.round(w.y)} w=${Math.round(w.w)} h=${Math.round(w.h)}`;
    },
    onRuleUpdate: (y) => {
      sel.textContent = `divider: y=${Math.round(y)}`;
    },
    onLabelUpdate: (l) => {
      sel.textContent = `heading: “${l.text}” x=${Math.round(l.x)} y=${Math.round(l.y)}`;
    },
  });

  alertBox.addEventListener('change', () => {
    /* 999 fires a normal-range rule; NaN previews nothing, exercising the real "unavailable
     * never alarms" guard. */
    alertProbe = alertBox.checked ? 999 : NaN;
    editorState.values = previewValues(page, alertProbe, liveValues);
    panel.show(editorState.selection, page);
    editor.redraw();
  });

  /* Poll the device for the values it last resolved (FR-27: the preview must show real fetched
   * data).
   *
   * WHY A POLL AND NOT ONE FETCH: this used to run once, at mount. On a device that had not yet
   * finished its own first refresh the answer was empty, the preview stayed on "--", and it never
   * asked again — so the user saw placeholders for a display that was in fact showing real
   * readings, and concluded the data was broken.
   *
   * IT SETTLES TO A SLOW HEARTBEAT rather than stopping. Stopping left the "the display is on this
   * page" readout asserting a fact it had learned once: the device rotates on its own, so minutes
   * after the last fetch that line would be describing where the panel WAS. The heartbeat keeps it
   * honest at a cost the embedded server can afford (one small GET every 15 s), and drops back to
   * the fast cadence whenever a value is still missing — which is the case right after Save, when
   * a widget the user just added has no resolved value yet.
   *
   * THE PAGE IS RESOLVED ON THE DEVICE, so the editor no longer has to WAIT for the glass to
   * reach the page it is editing: it asks for that page explicitly. What still has to be checked
   * is the DRAWN page, which drives the "the display is showing page N" readout and is the page
   * the device would have answered with before — so a device that reports values for the edited
   * page but is drawing another one is described honestly rather than passed off as live. */
  const editedPageIndex = (): number => pageIndex;
  let valuesTimer = 0;
  let valuesSlow = false;   /* true once every widget has resolved and the heartbeat is in force */
  const FAST_MS = 2000;
  const HEARTBEAT_MS = 15000;
  function pollValues(): void {
    if (valuesTimer) window.clearInterval(valuesTimer);
    let tries = 0;
    valuesSlow = false;
    const tick = (): void => {
      tries++;
      /* The hard stop is a backstop for a device that never answers: without it a tab left open
       * would poll a dead address forever. It is generous because the heartbeat is legitimate. */
      if (tries > 600) { window.clearInterval(valuesTimer); valuesTimer = 0; return; }
      /* ASK FOR THE PAGE BEING EDITED, not just whatever is on the glass. A device too old to
       * understand `?page=` answers with its own page, which the guard below still accepts when it
       * matches — so this degrades to the old behaviour rather than breaking against one. */
      void getValuesInfo({ baseUrl: API }, editedPageIndex()).then((info) => {
        if (!info) return;
        /* Record the DEVICE's page on EVERY answer, independent of the page we asked about, so
         * the selector's readout keeps telling the user where the glass is. */
        if (info.drawnPage !== devicePage) { devicePage = info.drawnPage; describePage(); }
        /* A device that echoed back a page other than the one asked for cannot be trusted to have
         * answered our question, so its values are dropped rather than painted into the wrong
         * page's boxes. On a current device info.page === editedPageIndex() by construction. */
        if (info.page !== editedPageIndex()) return;
        if (Object.keys(info.values).length === 0) return;   /* nothing resolved yet; try again */
        liveValues = info.values;
        editorState.values = previewValues(editorState.page, alertProbe, liveValues);
        editor.redraw();
        /* Every widget resolved: slow down to the heartbeat ONCE. If one is still missing — a
         * widget the user just added, which the device has not seen yet — stay fast. */
        const allPresent = editorState.page.widgets
          .filter((w) => w.role === 'dynamic')
          .every((w) => liveValues[w.id] !== undefined);
        if (allPresent && !valuesSlow) {
          valuesSlow = true;
          window.clearInterval(valuesTimer);
          valuesTimer = window.setInterval(tick, HEARTBEAT_MS);
        }
      });
    };
    tick();
    valuesTimer = window.setInterval(tick, FAST_MS);
  }
  pollValues();
  window.addEventListener('beforeunload', () => window.clearInterval(valuesTimer));

  async function doSave(): Promise<void> {
    const p = picker.getPosition();
    /* WRITE THE EDITED PAGE BACK BY INDEX, not as `[page, ...slice(1)]`. That construction
     * assumed the edited page was always index 0, so with the selector on another page it
     * replaced page 1 with page 2's edits and duplicated the rest — saving a layout nobody had
     * ever seen. Indexing the same array the selector came from cannot drift that way. */
    const pages = doc.pages.slice();
    if (pages[pageIndex]) pages[pageIndex] = page;
    doc = {
      ...doc,
      location: { ...doc.location, latitude: p.lat, longitude: p.lon, zipCode: zipInput.value },
      pages,
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
      /* MAKE THE SAVED PAGE THE ONE ON THE GLASS. The config PUT schedules a refresh, but the
       * device picks the page from its rotation schedule — so saving an edit to a page whose slot
       * is not next drew a DIFFERENT page and the user's change appeared to be lost. Asking for
       * the edited page makes the save visibly take effect. Best-effort: a device that refuses the
       * nudge has still stored the config, so this is not reported as a save failure. */
      void requestPage(pageIndex);
      /* A widget the user just added has no resolved value until the device has seen this
       * config, so restart the poll to fill its box in rather than leaving the placeholder. */
      pollValues();
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
       'Drag a value box to move it, or drag its edge to resize. Drag a divider to move the ' +
       'line, or a heading to move it. This is the real 1-bit output the panel will show.'),
    el('div', { className: 'fields' },
       el('div', {}, el('label', { htmlFor: 'editPage' }, 'Editing page'), pageSel)),
    pageHint,
    el('div', { className: 'actions' }, showPageBtn),
    el('h3', {}, 'Pages'),
    el('p', { className: 'sub' },
       'The display rotates through these pages on its own. Each page has its own boxes, ' +
       'dividers and headings.'),
    el('div', { className: 'pGrid' },
       el('div', {}, el('label', { htmlFor: 'pageName' }, 'Page name'), pageNameInput),
       el('div', {}, el('label', { htmlFor: 'pageDwell' }, 'Show this page for (seconds)'), pageDwellInput)),
    el('div', { className: 'fields' },
       el('div', {}, el('label', { htmlFor: 'refreshSeconds' }, 'Refresh all values every (seconds)'),
          refreshInput)),
    rotationHint,
    el('div', { className: 'actions' }, addPageBtn, delPageBtn),
    el('div', { className: 'toolbar' },
       zoomOut, zoomIn, zoomFit, bitBadge, zoomLabel),
    el('div', { className: 'actions' }, addBoxBtn, addRuleBtn, addLabelBtn),
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
    el('h2', {}, 'Weather product'),
    el('p', { className: 'sub' },
       'Which OpenWeatherMap product to fetch from. This decides whether official ' +
       'severe-weather alerts are available.'),
    el('div', { className: 'fields' },
       el('div', {}, el('label', { htmlFor: 'owmProduct' }, 'Product'), owmSel)),
    productHint,
    el('h2', {}, 'Data source credentials'),
    el('p', { className: 'sub' },
       'Credentials for the data sources. Stored on the display, never in this file. A blank ' +
       'field keeps whatever the display already has.'),
    el('div', { className: 'fields' },
       el('div', {}, el('label', { htmlFor: 'owmKey' }, 'OpenWeatherMap key'), owmKeyInput, owmKeyStatus)),
    el('div', { className: 'fields' },
       el('div', {}, el('label', { htmlFor: 'haUrl' }, 'Home Assistant URL'), haUrlInput),
       el('div', {}, el('label', { htmlFor: 'haToken' }, 'Home Assistant token'),
          haTokenInput, haTokenStatus)),
    el('div', { className: 'actions' }, secretsBtn),
    credStatus,
    el('h2', {}, 'Access'),
    authLabel,
    authRow,
  );

  describe(undefined);
  panel.show(undefined, page);
  buildPageOptions();
  pageSettings.refresh();
  describePage();
  picker.invalidate();

  /* The canvas is in the document NOW, so this is the first moment "fit" can be measured against
   * the real column width. attachEditor() ran before the append above (the DOM is built first), so
   * its own resize saw no parent; without this call the panel would stay at 1:1 and overflow the
   * column — the exact "not fit by default" defect this fixes. The editor's ResizeObserver keeps it
   * correct after this. */
  editor.resize();

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

  /* Read the credential flags AND the stored HA URL. The URL fills the field so a save does not
   * blank it; the entity picker itself searches THROUGH the device (see below). */
  void (async () => {
    const r = await getSecrets();
    describeSecrets(r.ok ? r.value : null);
    if (r.ok && !r.value.haToken) {
      panel.setEntities([], 'Enter the Home Assistant token above (and Save) so the display can '
        + 'search Home Assistant for entities.');
    }
  })();

  /* ---- Home Assistant entity search, through the DISPLAY (FR-23) ----
   *
   * WHY NOT FROM THE BROWSER: Home Assistant sends no CORS headers, so a browser fetch of
   * /api/states is blocked before it is sent — verified on the bench, where a valid token still
   * produced "blocked by CORS policy" and an empty picker, which reads as "your HA has no
   * entities". The display holds the HA URL and token and reaches HA on every refresh, so the
   * search runs there and the app only asks for a term.
   *
   * DEBOUNCED: the search fires per keystroke, and each one is a TLS request from a part with no
   * PSRAM. 250 ms coalesces a burst of typing into one request without feeling laggy. */
  let entitySearchTimer = 0;
  function searchEntitiesDebounced(q: string): void {
    if (entitySearchTimer) window.clearTimeout(entitySearchTimer);
    if (q.trim().length < 2) {
      /* Below two characters the match set is the whole instance; not worth a request. */
      panel.setEntityOptions([]);
      return;
    }
    entitySearchTimer = window.setTimeout(() => {
      void searchEntities(q).then((res) => {
        /* Report a failure IN PLACE, without a re-render (which would drop focus), so a missing
         * or wrong token is explained where the user is typing rather than in an empty dropdown.
         * `total` is passed through so a clipped match list says how many matched. */
        panel.setEntityOptions(
          res.ok ? res.entities : [],
          res.ok ? undefined : res.error,
          res.ok ? res.total : undefined,
        );
      });
    }, 250);
  }
  /* The panel was created earlier, so its search callback reads this slot rather than closing
   * over a function declared below it. */
  entitySearch = searchEntitiesDebounced;
}

const root = document.getElementById('app');
if (root) void mount(root);
