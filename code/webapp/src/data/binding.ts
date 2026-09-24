/**
 * Human labels for data bindings (FR-23).
 *
 * The property panel shows what a widget is bound to. A bare kind like `'owm-daily'` tells the
 * user nothing, and forgetting the day index is an easy mistake that silently shows today's
 * value — so the label always states the full binding, including what is MISSING.
 */

import type { DataBinding } from '../model/config';

const OWM_FIELD_NAMES: Record<string, string> = {
  temp: 'temperature',
  min: 'low',
  max: 'high',
  wind: 'wind',
  humidity: 'humidity',
  condition: 'conditions',
  icon: 'icon',
  city: 'location',
  time: 'last updated',
};

/** Ordinal-ish day label, 1-based for humans: dayIndex 0 reads as "day 1". */
function dayLabel(i: number | undefined): string {
  if (i === undefined || !Number.isFinite(i)) return 'day 1';
  return `day ${Math.trunc(i) + 1}`;
}

export function describeBinding(b: DataBinding | undefined): string {
  if (!b) return 'Not bound';
  switch (b.kind) {
    case 'owm-current':
      return `Current weather: ${OWM_FIELD_NAMES[b.owmField ?? 'temp'] ?? b.owmField ?? 'temperature'}`;
    case 'owm-daily':
      return `Forecast ${dayLabel(b.dayIndex)}: ${OWM_FIELD_NAMES[b.owmField ?? 'max'] ?? b.owmField ?? 'high'}`;
    case 'owm-alert':
      return 'OpenWeatherMap severe-weather alerts';
    case 'image':
      /* Says WHAT the box is, because its picture is not in the document — a layout opened without
       * the picture still has the box, and "Picture" alone would read as if one were loaded. */
      return 'A picture you upload — drawn into the page’s background';
    case 'ha':
      /* Says "(no entity chosen)" rather than just "Home Assistant:" — an unset entity is a
       * configuration mistake the user needs to see, not a label to hide. */
      return `Home Assistant: ${b.entityId && b.entityId.length > 0 ? b.entityId : '(no entity chosen)'}`;
    default:
      return `unknown binding: ${String(b.kind)}`;
  }
}
