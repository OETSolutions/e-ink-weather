/**
 * Alert-rule evaluation (FR-14), mirroring the firmware's lib/alerts/src/alerts.c EXACTLY.
 *
 * WHY "EXACTLY" IS THE WHOLE POINT: the config app shows whether a rule fires, and the device
 * decides whether to draw the alert bar. If the two disagree about an edge — is `100 > 100`
 * true? does an unavailable sensor alarm? — then the app tells the user one thing and the
 * panel shows another, with no error anywhere. The semantics are therefore copied, not
 * reinvented: the same strictness, the same severity ordering, the same refusal to alarm on a
 * missing reading.
 */

import type { AlertLevel, AlertOp, AlertRule } from '../model/config';

/* Severity ORDER, matching the C enum: none < advisory < warning < severe. The comparison in
 * evaluateAlerts depends on this being the numeric order, so it is stated once, here. */
const SEVERITY: Record<AlertLevel, number> = {
  none: 0,
  advisory: 1,
  warning: 2,
  severe: 3,
};

/** Map a rule's operation to a strict/loose comparison, exactly as cmp_fires() does.
 *
 * `gt` is STRICT and `gte` is INCLUSIVE — the difference that matters most in practice, since
 * a "above 100" rule that fired at exactly 100 would nag every time the reading touched the
 * threshold. */
function fires(op: AlertOp, value: number, threshold: number): boolean {
  switch (op) {
    case 'gt': return value > threshold;
    case 'gte': return value >= threshold;
    case 'lt': return value < threshold;
    case 'lte': return value <= threshold;
    case 'eq': return value === threshold;
    case 'ne': return value !== threshold;
    default: return false; /* an op from a newer schema must not fire rather than guess */
  }
}

/**
 * Evaluate a list of rules and return the MOST SEVERE level that fires.
 *
 * A NON-FINITE VALUE NEVER ALARMS, and neither does a non-finite threshold. This is not
 * defensive padding: an unavailable sensor reports NaN, and without this guard every
 * comparison against NaN is false EXCEPT `ne`, which is true — so a "not equal to 0" rule
 * would fire on an unavailable reading. An alarm that means "the sensor is missing" is
 * exactly the alarm nobody can act on.
 *
 * An empty list is 'none', not an error: a widget with no rules is the normal case.
 */
export function evaluateAlerts(rules: AlertRule[] | undefined, value: number): AlertLevel {
  if (!rules || rules.length === 0) return 'none';
  if (!Number.isFinite(value)) return 'none';

  let worst: AlertLevel = 'none';
  for (const r of rules) {
    if (!r || !Number.isFinite(r.threshold)) continue;
    if (fires(r.op, value, r.threshold)) {
      if (SEVERITY[r.level] > SEVERITY[worst]) worst = r.level;
    }
  }
  return worst;
}

/** A short description of a rule, for the property panel's list. */
export function describeRule(r: AlertRule): string {
  const sym: Record<AlertOp, string> = {
    gt: '>', gte: '≥', lt: '<', lte: '≤', eq: '=', ne: '≠',
  };
  /* The threshold is shown as written rather than reformatted, so a whole-number rule reads
   * as "100" and not "100.0" — the user typed the former and should see it back. */
  return `${sym[r.op]} ${r.threshold} → ${r.level}`;
}

/** The most severe level across several levels, for combining per-widget results. */
export function worstLevel(levels: AlertLevel[]): AlertLevel {
  let worst: AlertLevel = 'none';
  for (const l of levels) {
    if (SEVERITY[l] > SEVERITY[worst]) worst = l;
  }
  return worst;
}
