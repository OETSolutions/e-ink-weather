import { describe, it, expect } from 'vitest';
import { evaluateAlerts, describeRule, worstLevel } from '../src/alerts/rules';
import type { AlertRule } from '../src/model/config';

/* These cases are written to mirror lib/alerts/src/alerts.c case for case. If one of these
 * fails, check the C before changing the rule here — the device is the one that decides. */
describe('alert evaluation (mirrors the firmware, FR-14)', () => {
  const r = (op: AlertRule['op'], threshold: number, level: AlertRule['level']): AlertRule =>
    ({ op, threshold, level });

  it('is strict for gt and inclusive for gte', () => {
    expect(evaluateAlerts([r('gt', 100, 'severe')], 100.1)).toBe('severe');
    expect(evaluateAlerts([r('gt', 100, 'severe')], 100)).toBe('none');
    expect(evaluateAlerts([r('gte', 100, 'severe')], 100)).toBe('severe');
  });

  it('is strict for lt and inclusive for lte', () => {
    expect(evaluateAlerts([r('lt', 0, 'advisory')], -0.1)).toBe('advisory');
    expect(evaluateAlerts([r('lt', 0, 'advisory')], 0)).toBe('none');
    expect(evaluateAlerts([r('lte', 0, 'advisory')], 0)).toBe('advisory');
  });

  it('compares eq and ne exactly', () => {
    expect(evaluateAlerts([r('eq', 5, 'warning')], 5)).toBe('warning');
    expect(evaluateAlerts([r('eq', 5, 'warning')], 5.0001)).toBe('none');
    expect(evaluateAlerts([r('ne', 5, 'warning')], 6)).toBe('warning');
    expect(evaluateAlerts([r('ne', 5, 'warning')], 5)).toBe('none');
  });

  it('returns the most severe level that fires', () => {
    const rules = [r('gt', 90, 'advisory'), r('gt', 100, 'severe')];
    expect(evaluateAlerts(rules, 105)).toBe('severe');
    expect(evaluateAlerts(rules, 95)).toBe('advisory');
    expect(evaluateAlerts(rules, 50)).toBe('none');
  });

  it('orders severity none < advisory < warning < severe', () => {
    const rules = [r('gt', 0, 'warning'), r('gt', 0, 'severe'), r('gt', 0, 'advisory')];
    expect(evaluateAlerts(rules, 1)).toBe('severe');
    expect(evaluateAlerts([r('gt', 0, 'advisory'), r('gt', 0, 'warning')], 1)).toBe('warning');
  });

  it('never alarms on a non-finite value (an unavailable sensor)', () => {
    expect(evaluateAlerts([r('lt', 0, 'severe')], NaN)).toBe('none');
    expect(evaluateAlerts([r('lt', 0, 'severe')], Infinity)).toBe('none');
    expect(evaluateAlerts([r('lt', 0, 'severe')], -Infinity)).toBe('none');
  });

  /* The subtle one: without the isfinite guard, `ne` on NaN would be TRUE — every comparison
   * with NaN is false except != — so a "not equal to 0" rule would fire on a missing reading. */
  it('does not let a ne rule fire on a missing reading', () => {
    expect(evaluateAlerts([r('ne', 0, 'severe')], NaN)).toBe('none');
  });

  it('ignores a rule with a non-finite threshold', () => {
    expect(evaluateAlerts([r('lt', NaN, 'severe')], 5)).toBe('none');
  });

  it('treats no rules as no alarm', () => {
    expect(evaluateAlerts([], 100)).toBe('none');
    expect(evaluateAlerts(undefined, 100)).toBe('none');
  });
});

describe('rule description', () => {
  it('reads as a threshold sentence', () => {
    expect(describeRule({ op: 'gt', threshold: 100, level: 'severe' })).toBe('> 100 → severe');
    expect(describeRule({ op: 'lte', threshold: 32, level: 'advisory' })).toBe('≤ 32 → advisory');
  });
});

describe('combining levels', () => {
  it('picks the worst', () => {
    expect(worstLevel(['none', 'warning', 'advisory'])).toBe('warning');
    expect(worstLevel(['severe', 'none'])).toBe('severe');
    expect(worstLevel([])).toBe('none');
  });
});
