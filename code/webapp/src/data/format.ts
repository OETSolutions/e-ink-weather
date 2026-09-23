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

/**
 * A reading the DEVICE resolved, as the editor needs it (see previewTextWithLive).
 *
 * `text` is exactly what the panel draws. `value` is present ONLY when the device reported that
 * `text` is that number's plain rendering, in which case the app may re-format it locally so a
 * format edit shows up without a save and a repaint.
 */
export interface LiveValue {
  text: string;
  value?: number;
}

/** The default shown when there is no reading, matching the firmware's "--". */
export const DEFAULT_FALLBACK = '--';

/**
 * Format a number to `decimals` places EXACTLY as the firmware's `snprintf("%.*f")` does.
 *
 * WHY NOT `toFixed`. The device draws this string through C's printf, and the two languages round
 * a HALF DIFFERENTLY: printf rounds half-to-EVEN (the FPU's default mode), while toFixed rounds
 * half-away-from-zero. They disagree on every exact tie — and a tie is not exotic here, it is the
 * common case for a weather reading: 72.5 °F at zero decimals is exactly a tie, printf draws "72"
 * and toFixed gives "73". The preview would then contradict the glass for a whole class of ordinary
 * readings, which is the preview-is-a-confident-lie failure NFR-4 exists to prevent.
 *
 * WHY NOT SCALE BY 10^d AND ROUND. That is the obvious implementation and it is wrong: `0.05 * 10`
 * is exactly 0.5 in binary floating point, so a value that is ACTUALLY a hair above 0.05 loses the
 * distinction and rounds DOWN where printf rounds up. Measured against printf over 161
 * (value, decimals) pairs, the scaling version mismatched 9 — every one a value sitting just off a
 * boundary, which is precisely where a weather reading tends to land.
 *
 * SO THE ARITHMETIC IS EXACT. A double is a mantissa and a power of two, so its value times 10^d is
 * an exact RATIONAL, and rounding that to an integer with BigInt reproduces the FPU's result
 * without any intermediate rounding of its own. The comparison test against the C reference (161
 * pairs, including the tie and near-tie cases) is what keeps the two implementations honest.
 */
export function fixedToEven(value: number, decimals: number): string {
  const d = Math.max(0, Math.min(6, Math.trunc(decimals)));
  if (!Number.isFinite(value)) return '';

  /* NEGATIVE ZERO IS STILL NEGATIVE TO PRINTF. `-0.0 < 0` is FALSE, so a sign test alone missed
   * it and `printf("%.1f", -0.0)` writes "-0.0" while this wrote "0.0". Object.is is the only way
   * to see a -0 in JavaScript, and it is needed here rather than in a nice-to-have: the value
   * itself is what the device stores, and the two renderings must be identical strings. */
  const neg = value < 0 || Object.is(value, -0);
  const v = Math.abs(value);
  if (v === 0) {
    const z = d === 0 ? '0' : `0.${'0'.repeat(d)}`;
    return neg ? `-${z}` : z;
  }

  /* Decompose the double: value = mantissa * 2^exponent, exactly. */
  const buf = new DataView(new ArrayBuffer(8));
  buf.setFloat64(0, v);
  const hi = buf.getUint32(0);
  const lo = buf.getUint32(4);
  const expBits = (hi >>> 20) & 0x7ff;
  const frac = (BigInt(hi & 0xfffff) << 32n) | BigInt(lo);
  let mant: bigint;
  let exp: number;
  if (expBits === 0) {
    /* Subnormal: no implicit leading 1. */
    mant = frac;
    exp = -1074;
  } else {
    mant = frac | (1n << 52n);
    exp = expBits - 1075;
  }

  /* Round v * 10^d to an integer, half-to-even — the same decision printf's FPU makes. */
  const pow10 = 10n ** BigInt(d);
  let q: bigint;
  if (exp >= 0) {
    q = mant * (1n << BigInt(exp)) * pow10;    /* already an integer: no rounding at all */
  } else {
    const num = mant * pow10;
    const den = 1n << BigInt(-exp);
    q = num / den;
    const rem = num % den;
    const twice = rem * 2n;
    if (twice > den) q += 1n;
    else if (twice === den && q % 2n !== 0n) q += 1n;
  }

  const digits = q.toString();
  let body: string;
  if (d === 0) {
    body = digits;
  } else {
    const padded = digits.padStart(d + 1, '0');
    body = `${padded.slice(0, -d)}.${padded.slice(-d)}`;
  }
  /* PRINTF KEEPS THE SIGN ON A NEGATIVE VALUE THAT ROUNDS TO ZERO — it writes "-0" and "-0.0",
   * verified on hardware-adjacent ground truth: `printf("%.1f", -0.05)` is "-0.1" and
   * `printf("%.0f", -0.5)` is "-0". A -0.4 °F reading is real (it is below freezing, not calm), so
   * the sign carries information and the preview must not drop it. The ONLY case with no sign is a
   * true +0.0, which `neg` already excludes. */
  return neg ? `-${body}` : body;
}

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

  const body = fixedToEven(v.value, decimals);
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

/**
 * The text for one widget, preferring the value the DEVICE actually resolved (FR-27).
 *
 * FR-27 asks for a preview "rendered with real fetched data", and the device is the only party
 * that has it — it holds the OWM key and the HA token and runs each widget through the same C
 * formatter that draws the string on the glass. So when the device has reported a value for this
 * widget, that string is used: there is nothing for the app to recompute, and no way for the two to
 * disagree. This is also the only option in the embedded case, where the browser has neither
 * credentials nor, on the setup network, any internet.
 *
 * EXCEPT WHEN THE USER IS EDITING THE FORMAT — see the re-format branch below.
 *
 * The ORDER matches the firmware's value_format_widget(), and the alert probe is checked FIRST
 * because it is the user explicitly asking "what would a firing rule look like": if `live` won
 * over a firing level, ticking the editor's toggle on a device that has data would appear to do
 * nothing. `live[NaN]` cannot occur — the probe is a number, and a firing result is not looked up
 * in `live` at all.
 */
export function previewTextWithLive(
  w: { id?: string; role?: string; format?: Format; alerts?: AlertRule[] },
  probe: number,
  live: Record<string, LiveValue | string> = {},
  evaluate: (rules: AlertRule[] | undefined, value: number) => AlertLevel = evaluateAlerts,
): string {
  if (w.role !== 'dynamic') return '';
  const level = evaluate(w.alerts, probe);
  if (level !== 'none') return level;
  /* A widget with no id cannot be matched to a device value, and an uncached id means the device
   * has not drawn it — both show the placeholder, which is what the panel shows before its first
   * successful fetch, so the two agree either way. */
  const id = w.id;
  /* hasOwnProperty rather than a truthiness test: an EMPTY string from the device is a real value
   * (the alert bar ships with "" so quiet weather leaves it blank), not a missing one, and a `!live[id]`
   * check would replace it with the fallback — a preview that lies about a deliberately blank bar. */
  const entry = id && Object.prototype.hasOwnProperty.call(live, id) ? live[id] : undefined;
  if (entry === undefined) return formatPlaceholder(w.format);
  /* A bare string is a caller that has only the drawn text (older device, or a test): echo it. */
  if (typeof entry === 'string') return entry;

  /* RE-FORMAT LOCALLY WHEN THE DEVICE SAYS `text` IS THIS NUMBER'S PLAIN RENDERING.
   *
   * This is what makes a format edit show up immediately: `text` was formatted with the STORED
   * decimals/prefix/suffix, so without this the box would keep showing the old formatting until the
   * config was saved and the panel had redrawn — reported as "the layout editor doesn't show the
   * updated values unless you first save and refresh". The device reports the raw reading AND flags
   * whether re-format is the right thing to do, because only it knows whether the drawn text is the
   * number (an alert word, a text reading, an icon code and a fallback are all things it draws
   * instead, and re-format in those cases would put a temperature over a word the panel shows).
   *
   * The digits come from fixedToEven(), which reproduces printf's half-to-even rounding — so the
   * locally formatted string is character-for-character what the device would draw. */
  if (entry.value !== undefined) {
    const decimals = w.format?.decimals ?? 1;
    const body = fixedToEven(entry.value, decimals);
    return `${w.format?.prefix ?? ''}${body}${w.format?.suffix ?? ''}`;
  }
  return entry.text;
}

