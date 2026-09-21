/**
 * Value formatting for the panel (FR-23).
 *
 * The device renders whatever string it is handed, so this is where a number becomes the exact
 * text that appears on the glass. That makes it worth being strict about: a wrong fallback or a
 * stray "NaN" is not a cosmetic bug, it is what the user reads off the wall.
 */

import type { AlertLevel, AlertRule, Format } from '../model/config';
import { evaluateAlerts } from '../alerts/rules';

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

/**
 * The text for one widget, given the value the alert rules are probed with (FR-14, FR-27).
 *
 * LIVES HERE RATHER THAN IN main.ts because it must be TESTABLE: it decides the exact string
 * the panel will show, and it was previously an unexported helper inside the editor, which is
 * how a preview/panel mismatch survived — the app rendered "ADVISORY" while the firmware's
 * alerts_level_name() put "advisory" on the glass. NFR-4 requires the preview to match the panel
 * bit-for-bit, so the two must be comparable in a test, not merely similar by construction.
 *
 * `probe` of NaN means "no alert" (an unavailable reading produces NaN, and the rules return
 * 'none' for it), so the editor's toggle exercises the real guard rather than a special case.
 *
 * The ORDER mirrors the firmware's value_format_widget(): a firing alert REPLACES the reading,
 * checked before the no-reading fallback — and it is the bare level name, with no prefix or
 * suffix, because that is what the firmware draws (`snprintf(buf, cap, "%s", name)`).
 */
export function previewText(
  w: { role?: string; format?: Format; alerts?: AlertRule[] },
  probe: number,
  evaluate: (rules: AlertRule[] | undefined, value: number) => AlertLevel = evaluateAlerts,
): string {
  if (w.role !== 'dynamic') return '';
  const level = evaluate(w.alerts, probe);
  return level !== 'none' ? level : formatPlaceholder(w.format);
}

