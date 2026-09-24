import { describe, it, expect } from 'vitest';
import { formatValue, formatPlaceholder } from '../src/data/format';
import { describeBinding } from '../src/data/binding';
import type { DataBinding } from '../src/model/config';

describe('value formatting', () => {
  it('applies decimals, prefix and suffix', () => {
    expect(formatValue({ value: 68.44, status: 'ok' }, { decimals: 1, suffix: '°F' })).toBe('68.4°F');
    expect(formatValue({ value: 68, status: 'ok' }, { decimals: 0, suffix: '°F' })).toBe('68°F');
    expect(formatValue({ value: 12.3, status: 'ok' }, { decimals: 1, prefix: '$' })).toBe('$12.3');
  });

  it('shows the fallback for an unavailable value, never a zero or NaN', () => {
    expect(formatValue({ value: 0, status: 'unavailable' }, { fallback: '--' })).toBe('--');
    expect(formatValue({ value: NaN, status: 'ok' }, { fallback: '--' })).toBe('--');
  });

  /* A FAILED FETCH OFTEN PARSES TO 0, and 0°F is entirely plausible weather — so a zero with
   * a non-ok status must NOT render as a reading. */
  it('does not render a failed fetch as 0', () => {
    expect(formatValue({ value: 0, status: 'error' }, { decimals: 1, suffix: '°F' })).toBe('--');
  });

  it('defaults to one decimal and the -- fallback', () => {
    expect(formatValue({ value: 68, status: 'ok' })).toBe('68.0');
    expect(formatValue({ value: 68, status: 'bad' })).toBe('--');
  });

  it('falls back rather than emitting a silly precision', () => {
    expect(formatValue({ value: 1, status: 'ok' }, { decimals: 99 })).toBe('--');
    expect(formatValue({ value: 1, status: 'ok' }, { decimals: -1 })).toBe('--');
  });

  it('builds a placeholder that matches what the device would draw', () => {
    expect(formatPlaceholder({ decimals: 1, suffix: '°F' })).toBe('--°F');
  });
});

describe('binding descriptions', () => {
  const b = (over: Partial<DataBinding>): DataBinding => ({ kind: 'owm-current', ...over });

  it('names the current-weather field', () => {
    expect(describeBinding(b({ owmField: 'temp' }))).toBe('Current weather: temperature');
  });

  it('names a forecast day', () => {
    expect(describeBinding(b({ kind: 'owm-daily', dayIndex: 2, owmField: 'max' })))
      .toBe('Forecast day 3: high');
  });

  /* The "last updated" stamp is a current-conditions field, and the label has to read as a TIME
   * rather than as one more reading — a box whose panel says "Current weather: last updated"
   * would suggest this codebase knows what time it is, which it does not (no RTC, no SNTP). */
  it('names the last-updated stamp', () => {
    expect(describeBinding(b({ owmField: 'time' }))).toBe('Current weather: last updated');
  });

  it('names an HA entity, and says so when it is missing', () => {
    expect(describeBinding(b({ kind: 'ha', entityId: 'sensor.x' }))).toBe('Home Assistant: sensor.x');
    expect(describeBinding(b({ kind: 'ha' }))).toBe('Home Assistant: (no entity chosen)');
  });

  it('describes the OWM alert bar', () => {
    expect(describeBinding(b({ kind: 'owm-alert' }))).toBe('OpenWeatherMap severe-weather alerts');
  });

  it('never returns an empty label for an unknown kind', () => {
    expect(describeBinding({ kind: 'nonsense' as DataBinding['kind'] })).toContain('unknown');
  });
});
