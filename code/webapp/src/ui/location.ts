/**
 * Location value handling for the map picker and its text fields (FR-24).
 *
 * Kept separate from the DOM so it can be unit-tested — the coordinate text a user types is
 * the input most likely to be wrong, and it is the one place where a mistake silently puts
 * the panel in the wrong city. The map itself is in map-picker.ts.
 */

export type CoordKind = 'lat' | 'lon';

export const LAT_MIN = -90;
export const LAT_MAX = 90;
export const LON_MIN = -180;
export const LON_MAX = 180;

export type ParseResult =
  | { ok: true; value: number }
  | { ok: false; error: string };

/**
 * How many decimal places the text fields show.
 *
 * Six is ~11 cm, far finer than a map pin can be placed, but it is the precision the firmware
 * interpolates into the OWM URL (`%.6f`), so showing fewer would make the field disagree with
 * what the device actually sends. Showing more would imply a precision the pin does not have.
 */
export const COORD_DECIMALS = 6;

/** Format a coordinate for a text field: fixed decimals, never exponent notation. */
export function formatCoord(value: number): string {
  if (!Number.isFinite(value)) return '';
  /* toFixed, not toString: 41.7759 has to read the same in the box every time it is written
   * back, or the field would appear to change on its own as the pin moves. */
  return value.toFixed(COORD_DECIMALS);
}

/**
 * Parse a typed coordinate.
 *
 * Deliberately strict, because the alternative is a plausible wrong answer: an empty string is
 * an error (not 0), and so is anything non-numeric. `Number('')` is 0 and `Number(' ')` is 0,
 * so a naive parse would turn a blank field into the Gulf of Guinea — the same trap the
 * device-side IP parser is guarded against. Trailing text ("41.7N") is rejected rather than
 * partly accepted, since accepting it would mean guessing the user's intent.
 */
export function parseCoord(text: string, kind: CoordKind): ParseResult {
  const raw = text.trim();
  if (raw === '') return { ok: false, error: 'Enter a number' };

  /* A leading '+' is legal in a number but unusual here; Number handles it. Reject anything
   * Number would accept loosely (Infinity, hex like 0x10) by requiring a plain decimal shape. */
  if (!/^[+-]?(\d+(\.\d*)?|\.\d+)([eE][+-]?\d+)?$/.test(raw)) {
    return { ok: false, error: 'Not a number' };
  }

  const value = Number(raw);
  if (!Number.isFinite(value)) return { ok: false, error: 'Not a number' };

  const min = kind === 'lat' ? LAT_MIN : LON_MIN;
  const max = kind === 'lat' ? LAT_MAX : LON_MAX;
  if (value < min || value > max) {
    return {
      ok: false,
      error: kind === 'lat' ? 'Latitude must be -90 to 90' : 'Longitude must be -180 to 180',
    };
  }
  return { ok: true, value };
}

/** Clamp a coordinate into range. Used for a map drag, where the value comes from the map and
 *  must become valid rather than be rejected — the user cannot drag "wrong", only past the
 *  edge. Longitude wraps at the antimeridian instead of clamping, because that is what the
 *  user means when they drag off the right edge of a world map. */
export function clampCoord(value: number, kind: CoordKind): number {
  if (!Number.isFinite(value)) return 0;
  if (kind === 'lat') return Math.min(LAT_MAX, Math.max(LAT_MIN, value));
  if (value > LON_MAX) return value - 360;
  if (value < LON_MIN) return value + 360;
  return value;
}

/** A short human label, e.g. "41.7759° N, 111.8068° W". */
export function coordLabel(value: number, kind: CoordKind): string {
  if (!Number.isFinite(value)) return '—';
  if (kind === 'lat') {
    return `${Math.abs(value).toFixed(4)}° ${value >= 0 ? 'N' : 'S'}`;
  }
  return `${Math.abs(value).toFixed(4)}° ${value >= 0 ? 'E' : 'W'}`;
}

/** True when a pair is a usable position. Rejects 0,0 as "unset", which is what both the
 *  config default and a failed lookup produce — a device with no position reports 0,0, and
 *  opening the map on the Gulf of Guinea would look like a bug rather than an empty field. */
export function hasPosition(lat: number, lon: number): boolean {
  return Number.isFinite(lat) && Number.isFinite(lon) && !(lat === 0 && lon === 0);
}
