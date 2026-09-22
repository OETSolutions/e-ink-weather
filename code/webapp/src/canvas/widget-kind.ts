/**
 * Whether a widget draws a weather icon rather than text.
 *
 * WHY A SHARED FUNCTION: three places must agree — the editor (which builds the preview's
 * fields), the preview-text path (which decides what string to show) and the firmware (which
 * decides whether to blit an icon). If they disagreed, the preview would show the code "04n" as
 * text while the panel drew a picture, or vice versa. One predicate keeps them together, exactly
 * as face.ts does for the font face.
 *
 * Only owm-current carries an icon code; a daily forecast binding has none, so a widget bound to
 * a daily icon field is NOT an icon widget and will render its fallback as text.
 */

import type { Widget } from '../model/config';

export function isIconWidget(w: Pick<Widget, 'binding'>): boolean {
  const b = w.binding;
  return !!b && b.kind === 'owm-current' && b.owmField === 'icon';
}
