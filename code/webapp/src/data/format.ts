/**
 * Value formatting for the panel (FR-23).
 *
 * The device renders whatever string it is handed, so this is where a number becomes the exact
 * text that appears on the glass. That makes it worth being strict about: a wrong fallback or a
 * stray "NaN" is not a cosmetic bug, it is what the user reads off the wall.
 */

import type { Format } from '../model/config';

export interface Reading {
  value: number;
  /** 'ok' when the value is real; anything else means it could not be read. */
  status: string;
}

/** The default shown when there is no reading, matching the firmware's "--". */
export const DEFAULT_FALLBACK = '--';

/**
 * Format a reading for display.
 *
 * A NON-FINITE OR UNAVAILABLE READING NEVER RENDERS AS A NUMBER. `NaN.toFixed(1)` is the
 * string "NaN" and a failed fetch often parses to 0 — both would be displayed as if they were
 * real readings, and 0°F is entirely plausible weather. The fallback is the only honest thing
 * to show, and it matches what the firmware draws (`fmt_temp` in app_refresh.c).
 */
export function formatValue(v: Reading, f?: Format): string {
  const fallback = f?.fallback ?? DEFAULT_FALLBACK;

  if (!v || v.status !== 'ok') return fallback;
  if (!Number.isFinite(v.value)) return fallback;

  /* decimals defaults to 1: the panel's hero numbers are temperatures, and a whole-degree
   * reading would lose the tenth that makes a display feel live. */
  const decimals = f?.decimals ?? 1;
  if (!Number.isFinite(decimals) || decimals < 0 || decimals > 6) return fallback;

  const body = v.value.toFixed(Math.trunc(decimals));
  return `${f?.prefix ?? ''}${body}${f?.suffix ?? ''}`;
}

/**
 * Format a value that has no reading yet — used for the editor's placeholder, so a preview
 * shows the same dashes the device would rather than a made-up number.
 */
export function formatPlaceholder(f?: Format): string {
  return `${f?.prefix ?? ''}${f?.fallback ?? DEFAULT_FALLBACK}${f?.suffix ?? ''}`;
}
