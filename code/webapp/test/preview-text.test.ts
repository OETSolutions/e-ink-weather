import { describe, it, expect } from 'vitest';
import { previewText, formatPlaceholder } from '../src/data/format';
import type { AlertRule, Format } from '../src/model/config';

/* The exact string a widget will put on the glass (FR-14, FR-27, NFR-4).
 *
 * WHY THIS FILE EXISTS: previewText() used to be an unexported helper inside main.ts, so nothing
 * could compare the editor's preview against what the firmware actually draws — and it drifted.
 * The app rendered "ADVISORY" (uppercased) while the firmware's alerts_level_name() put
 * "advisory" on the panel. NFR-4 requires the preview to match the panel bit-for-bit, so the
 * strings are pinned here against the C, not against what looks right.
 *
 * The C side (lib/layout/src/value_resolve.c, value_format_widget) does, in this order:
 *   1. if a rule fires  -> snprintf(buf, cap, "%s", alerts_level_name(level))
 *   2. else if no value  -> the widget's own fallback
 * and alerts_level_name() (lib/alerts/src/alerts.c) returns exactly
 * "advisory" | "warning" | "severe" | "none" — lowercase, no prefix or suffix.
 */
describe('preview text (mirrors the firmware, FR-14/NFR-4)', () => {
  const rule = (op: AlertRule['op'], threshold: number, level: AlertRule['level']): AlertRule =>
    ({ op, threshold, level });

  const dyn = (format?: Format, alerts?: AlertRule[]) =>
    ({ role: 'dynamic', format, alerts });

  /* THE REGRESSION THIS FILE WAS WRITTEN FOR. The firmware draws the bare lowercase level name;
   * an uppercased version is a preview that lies about the panel. */
  it('renders the alert level EXACTLY as the firmware does — lowercase, no affixes', () => {
    const w = dyn({ prefix: 'P', suffix: 'S', fallback: '--' }, [rule('gte', 100, 'severe')]);
    expect(previewText(w, 101)).toBe('severe');
    expect(previewText(w, 100)).toBe('severe');
  });

  it('matches alerts_level_name() for every level', () => {
    expect(previewText(dyn(undefined, [rule('gte', 1, 'advisory')]), 1)).toBe('advisory');
    expect(previewText(dyn(undefined, [rule('gte', 1, 'warning')]), 1)).toBe('warning');
    expect(previewText(dyn(undefined, [rule('gte', 1, 'severe')]), 1)).toBe('severe');
  });

  /* A firing alert REPLACES the reading, checked BEFORE the no-reading fallback — the same order
   * as the firmware, where putting the alert check after the fallback branch would make it
   * unreachable. */
  it('shows the alert word rather than the fallback when a rule fires', () => {
    const w = dyn({ fallback: '--' }, [rule('gte', 100, 'severe')]);
    expect(previewText(w, 100)).toBe('severe');
    expect(previewText(w, 99)).toBe('--');
  });

  /* With no alert, the preview is the placeholder the device would draw — including its affixes,
   * which is why this is not just the bare fallback. */
  it('falls back to the formatted placeholder when nothing fires', () => {
    const w = dyn({ prefix: '', suffix: '°F', fallback: '--' });
    expect(previewText(w, 70)).toBe('--°F');
    expect(previewText(w, 70)).toBe(formatPlaceholder(w.format));
  });

  /* NaN is what an unavailable reading produces, and the rules must return 'none' for it. The
   * editor's alert toggle works by passing NaN, so this is the real guard, not a special case. */
  it('treats a NaN probe as no alert and shows the fallback', () => {
    const w = dyn({ fallback: '--' }, [rule('gte', 100, 'severe')]);
    expect(previewText(w, NaN)).toBe('--');
  });

  /* A static widget draws no value, so it contributes no preview text — matching the firmware,
   * which only stamps widgets that resolve to a reading. */
  it('gives a non-dynamic widget no text at all', () => {
    expect(previewText({ role: 'static' }, 100)).toBe('');
    expect(previewText({}, 100)).toBe('');
  });

  /* Multiple rules of different severity: the MOST SEVERE wins, as on the device. */
  it('shows the most severe firing level', () => {
    const w = dyn(undefined, [rule('gte', 90, 'advisory'), rule('gte', 100, 'severe')]);
    expect(previewText(w, 105)).toBe('severe');
    expect(previewText(w, 95)).toBe('advisory');
  });
});
