/**
 * The map picker (FR-24): a draggable pin for the precise position, plus latitude/longitude
 * text fields that stay in sync with it in BOTH directions.
 *
 * WHY BOTH INPUTS EXIST AND WHY THEY MUST AGREE: the map is the fast way to get a position,
 * but it is useless when someone already knows the coordinate, wants to paste one from another
 * tool, or has to be exact to more decimal places than a pin can be dragged. So neither is a
 * fallback for the other — either can drive, and the text fields are updated from the map and
 * the pin is moved from the text. The device reads ONE pair of numbers, so the two inputs must
 * never disagree about what that pair is.
 *
 * WHY THE TILES ARE BUNDLED AND NOT FROM A CDN, BUT ARE STILL FETCHED: Leaflet is bundled
 * (see package.json) so the app has no third-party script dependency and works offline-ish,
 * while map TILES necessarily come from the network — a map that cannot load its tiles is
 * still usable here, because the pin, the fields and Save all work without them. Leaflet has a
 * plain-HTTP-safe tile URL and we serve the app over HTTP on the LAN, so tiles load normally.
 *
 * THE POSITION IS ALWAYS THE PIN'S POSITION. Text that does not parse never moves the pin and
 * never reaches the caller — it shows an error and is left alone. That is the difference
 * between a typo and a wrong location: a rejected edit is visible, a silently clamped one is
 * not.
 */

import * as L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import { clampCoord, formatCoord, hasPosition, parseCoord } from './location';

export interface Position {
  lat: number;
  lon: number;
}

export interface MapPickerHandle {
  /** Move the pin (and the view) without firing onChange — for an external change, such as the
   *  device reporting its own autofilled location after a load. */
  setPosition(p: Position): void;
  getPosition(): Position;
  /** Leaflet must be told about a container resize; it cannot observe it itself. */
  invalidate(): void;
}

export interface MapPickerOptions {
  /** Where the map is drawn. */
  mapEl: HTMLElement;
  latInput: HTMLInputElement;
  lonInput: HTMLInputElement;
  /** A field for a validation message or a "moved" note. Optional. */
  statusEl?: HTMLElement;
  initial: Position;
  /** Called whenever the position genuinely changes, from either input. */
  onChange: (p: Position) => void;
}

/* A pin drawn as an inline SVG divIcon rather than Leaflet's default marker image. The default
 * icon is referenced through a CSS-relative URL that a bundler rewrites, and a missing icon
 * shows as a broken-image box ON the map — the one place the user is looking. An inline icon
 * cannot go missing, needs no asset, and is easy to make legible over both light and dark
 * tiles. */
function pinIcon(): L.DivIcon {
  const svg =
    '<svg xmlns="http://www.w3.org/2000/svg" width="30" height="42" viewBox="0 0 30 42">' +
    '<path d="M15 0C6.7 0 0 6.7 0 15c0 10.5 13 25.6 13.6 26.3a1.9 1.9 0 0 0 2.8 0' +
    'C17 40.6 30 25.5 30 15 30 6.7 23.3 0 15 0z" fill="#dc2626" stroke="#fff" stroke-width="2"/>' +
    '<circle cx="15" cy="15" r="5.5" fill="#fff"/></svg>';
  return L.divIcon({
    html: svg,
    className: 'map-pin',
    iconSize: [30, 42],
    iconAnchor: [15, 42],
  });
}

/** A sane zoom for "somewhere on Earth" when the device has no position yet: the whole world,
 *  so the user can see where they are being asked to point without being dropped into an
 *  arbitrary ocean at street level. */
const DEFAULT_ZOOM = 2;
const PIN_ZOOM = 13;

export function createMapPicker(opts: MapPickerOptions): MapPickerHandle {
  const { mapEl, latInput, lonInput, statusEl, onChange } = opts;
  let pos: Position = { ...opts.initial };

  /* A flag rather than comparing values: while the user is typing, the fields are the source
   * of truth and must not be overwritten from the pin they are in the middle of moving. */
  let syncingFromMap = false;

  const map = L.map(mapEl, {
    zoomControl: true,
    attributionControl: true,
    /* No scroll-wheel zoom by default: this map is embedded in a scrolling page, and hijacking
     * the wheel traps the user who is just trying to scroll past it. Zoom is on the buttons and
     * on double-click. */
    scrollWheelZoom: false,
  });

  const usable = hasPosition(pos.lat, pos.lon);
  map.setView([usable ? pos.lat : 20, usable ? pos.lon : 0], usable ? PIN_ZOOM : DEFAULT_ZOOM);

  L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
    maxZoom: 19,
    /* Required by the OSM tile usage policy, and honest about where the map comes from. */
    attribution: '&copy; OpenStreetMap contributors',
  }).addTo(map);

  /* The pin exists even before a position is chosen, at the map centre, so the affordance is
   * visible: the user sees something to drag rather than an empty map and a guess that they
   * are supposed to click. */
  const marker = L.marker([usable ? pos.lat : 20, usable ? pos.lon : 0], {
    draggable: true,
    icon: pinIcon(),
    autoPan: true,
  }).addTo(map);

  function setStatus(text: string, isError: boolean): void {
    if (!statusEl) return;
    statusEl.textContent = text;
    statusEl.className = isError ? 'map-status err' : 'map-status';
  }

  /** Write the current position into the text fields. */
  function writeFields(): void {
    syncingFromMap = true;
    latInput.value = formatCoord(pos.lat);
    lonInput.value = formatCoord(pos.lon);
    syncingFromMap = false;
  }

  /** Adopt a new position from the map, updating the fields and notifying the caller. */
  function adoptFromMap(lat: number, lon: number, announce: boolean): void {
    pos = { lat: clampCoord(lat, 'lat'), lon: clampCoord(lon, 'lon') };
    writeFields();
    clearFieldErrors();
    if (announce) setStatus('Pin moved. Adjust it, or type exact values.', false);
    onChange({ ...pos });
  }

  function clearFieldErrors(): void {
    latInput.setCustomValidity('');
    lonInput.setCustomValidity('');
  }

  function moveMarker(): void {
    marker.setLatLng([pos.lat, pos.lon]);
  }

  marker.on('dragend', () => {
    const ll = marker.getLatLng();
    adoptFromMap(ll.lat, ll.lng, true);
  });

  /* Clicking the map moves the pin there. This is the fastest way to get close, and dragging
   * from an arbitrary spot is fiddly on a phone. */
  map.on('click', (e: L.LeafletMouseEvent) => {
    marker.setLatLng(e.latlng);
    adoptFromMap(e.latlng.lat, e.latlng.lng, true);
  });

  /* ---- Text field → pin ---- */

  function onFieldInput(which: 'lat' | 'lon'): void {
    if (syncingFromMap) return;

    const latText = latInput.value;
    const lonText = lonInput.value;

    /* Validate BOTH fields, because a position is a pair. Editing only the latitude while the
     * longitude still holds an old valid value must not commit a half-changed position; and an
     * invalid value in the field not being edited must still block the update. */
    const la = parseCoord(latText, 'lat');
    const lo = parseCoord(lonText, 'lon');

    if (!la.ok) {
      latInput.setCustomValidity(la.error);
      setStatus(which === 'lat' ? la.error : latInput.validationMessage || la.error, true);
      return;
    }
    if (!lo.ok) {
      lonInput.setCustomValidity(lo.error);
      setStatus(which === 'lon' ? lo.error : lonInput.validationMessage || lo.error, true);
      return;
    }

    clearFieldErrors();
    /* Only rebuild the position once both are valid, so the pin never sits at a coordinate
     * that a field currently contradicts. */
    pos = { lat: la.value, lon: lo.value };
    moveMarker();
    if (usableNow()) map.panTo([pos.lat, pos.lon]);
    setStatus('Position set from the fields.', false);
    onChange({ ...pos });
  }

  function usableNow(): boolean {
    return hasPosition(pos.lat, pos.lon) && map.getZoom() > DEFAULT_ZOOM + 1;
  }

  latInput.addEventListener('input', () => onFieldInput('lat'));
  lonInput.addEventListener('input', () => onFieldInput('lon'));

  writeFields();

  return {
    setPosition(p: Position) {
      /* From outside (a load, or the device's own autofilled location): the pin follows, and
       * the fields are rewritten, but onChange is NOT fired — the caller already made this
       * change and echoing it back would loop. */
      pos = { lat: clampCoord(p.lat, 'lat'), lon: clampCoord(p.lon, 'lon') };
      writeFields();
      moveMarker();
      if (hasPosition(pos.lat, pos.lon)) {
        map.setView([pos.lat, pos.lon], Math.max(map.getZoom(), PIN_ZOOM));
      }
    },
    getPosition() {
      return { ...pos };
    },
    invalidate() {
      map.invalidateSize();
    },
  };
}
