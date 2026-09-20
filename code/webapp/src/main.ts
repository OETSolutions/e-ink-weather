/**
 * App shell. For now this mounts the location section (FR-24) — the map picker plus its
 * latitude/longitude fields — against the device's current config, which is the first piece
 * of the config UI the user actually needs, because the device cannot fetch weather until it
 * knows where it is. The canvas editor and property panel are later tasks (18, 19) and mount
 * into this same shell.
 */

import './app.css';
import { createMapPicker, type MapPickerHandle } from './ui/map-picker';
import { hasPosition } from './ui/location';

/** Where the device's API lives. Served from the device itself, so a relative URL is correct
 *  both on the device and when the dev server proxies to it. */
const API = '';

interface ConfigDoc {
  schemaVersion: number;
  location?: { latitude?: number; longitude?: number; zipCode?: string };
  [k: string]: unknown;
}

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

/** Fetch the config from the device. Returns null when the device cannot be reached, which is
 *  a normal state (wrong address, device asleep) and must not throw the page away. */
async function loadConfig(): Promise<ConfigDoc | null> {
  try {
    const r = await fetch(`${API}/api/config`, { cache: 'no-store' });
    if (!r.ok) return null;
    return (await r.json()) as ConfigDoc;
  } catch {
    return null;
  }
}

async function saveConfig(doc: ConfigDoc): Promise<{ ok: true } | { ok: false; error: string }> {
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
    return { ok: true };
  } catch (e) {
    return { ok: false, error: e instanceof Error ? e.message : 'Could not reach the device' };
  }
}

async function mount(root: HTMLElement): Promise<void> {
  const doc = await loadConfig();

  const status = el('p', { className: 'status' });
  const mapEl = el('div', { className: 'map' });
  const mapStatus = el('p', { className: 'map-status' });

  const latInput = el('input', {
    type: 'text',
    inputMode: 'decimal',
    autocomplete: 'off',
    id: 'lat',
    placeholder: 'e.g. 41.7759',
  }) as HTMLInputElement;
  const lonInput = el('input', {
    type: 'text',
    inputMode: 'decimal',
    autocomplete: 'off',
    id: 'lon',
    placeholder: 'e.g. -111.8068',
  }) as HTMLInputElement;

  let current: ConfigDoc = doc ?? { schemaVersion: 1 };

  if (!doc) {
    status.textContent =
      'Could not reach the device. Showing an empty location — you can still drag the pin.';
    status.classList.add('err');
  } else if (hasPosition(doc.location?.latitude ?? 0, doc.location?.longitude ?? 0)) {
    /* The device fills a blank location from its public IP, so these are usually already
     * sensible. Saying so is what stops the user assuming they are exact. */
    status.textContent =
      'Position loaded from the device. If it was not chosen by hand it is an approximate ' +
      'guess from the device’s public IP — drag the pin to correct it.';
  } else {
    status.textContent = 'No position set yet. Drag the pin, or type the coordinates.';
  }

  let picker: MapPickerHandle;

  const saveBtn = el('button', { type: 'button' }, 'Save to device') as HTMLButtonElement;
  saveBtn.addEventListener('click', async () => {
    const p = picker.getPosition();
    current = {
      ...current,
      location: { ...(current.location ?? {}), latitude: p.lat, longitude: p.lon },
    };
    saveBtn.disabled = true;
    const r = await saveConfig(current);
    saveBtn.disabled = false;
    if (r.ok) {
      status.textContent = 'Saved. The display will refresh with the new location.';
      status.classList.remove('err');
    } else {
      status.textContent = `Could not save: ${r.error}`;
      status.classList.add('err');
    }
  });

  const zipInput = el('input', {
    type: 'text',
    id: 'zip',
    placeholder: 'e.g. 84341',
    autocomplete: 'off',
  }) as HTMLInputElement;
  zipInput.value = current.location?.zipCode ?? '';
  zipInput.addEventListener('input', () => {
    current = {
      ...current,
      location: { ...(current.location ?? {}), zipCode: zipInput.value },
    };
  });

  root.append(
    el('h1', {}, 'Display location'),
    el(
      'p',
      { className: 'sub' },
      'Used to fetch the weather. Drag the pin for a precise position, or type the coordinates.',
    ),
    status,
    el(
      'div',
      { className: 'fields' },
      el('div', {}, el('label', { htmlFor: 'lat' }, 'Latitude'), latInput),
      el('div', {}, el('label', { htmlFor: 'lon' }, 'Longitude'), lonInput),
    ),
    mapEl,
    mapStatus,
    el(
      'div',
      { className: 'zip' },
      el('label', { htmlFor: 'zip' }, 'Zip code (general area, optional)'),
      zipInput,
    ),
    saveBtn,
  );

  picker = createMapPicker({
    mapEl,
    latInput,
    lonInput,
    statusEl: mapStatus,
    initial: {
      lat: doc?.location?.latitude ?? 0,
      lon: doc?.location?.longitude ?? 0,
    },
    onChange: () => {
      /* Nothing to do per-change yet: Save reads the live position. Kept as the hook the
       * property panel and live preview will use in later tasks. */
    },
  });

  /* Leaflet measures its container on creation, and this one was just appended — a zero-size
   * measurement is why an embedded map sometimes renders as a grey strip until the window is
   * resized. */
  picker.invalidate();
  window.addEventListener('resize', () => picker.invalidate());
}

const root = document.getElementById('app');
if (root) void mount(root);
