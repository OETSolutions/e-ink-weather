/**
 * Which font face a widget renders with.
 *
 * THE CONFIG'S `font.size` IS ADVISORY, AND THIS IS THE ONLY PLACE IT IS INTERPRETED. The panel
 * has exactly TWO rasterised faces — FONT_BODY at 20 px and FONT_VALUE at 64 px (FR-4a: glyphs
 * are rasterised once at the exact pixel size, because a 1 bpp panel cannot anti-alias and a
 * runtime rasteriser would cost RAM and flash the device does not have). So a widget's size
 * cannot be an arbitrary number; it selects between two faces.
 *
 * WHY A SHARED FUNCTION RATHER THAN AN INLINE TEST: the renderer, the editor and the golden
 * fixture all need the same answer, and they had drifted — the editor keyed off `role` while
 * the fixture keyed off `size > 48`, so a widget could preview in one face and be stamped in
 * another. The preview would then be a confident lie about the very thing it exists to show.
 */

import type { Widget } from '../model/config';
import { FONT_BODY, FONT_VALUE } from './atlas-data';

/** The two faces the panel actually has, with the sizes to offer a user. */
export const FACES_AVAILABLE = [
  { id: FONT_BODY, px: 20, label: 'Body (20 px)' },
  { id: FONT_VALUE, px: 64, label: 'Value (64 px)' },
] as const;

/** Size at or above which the big face is used. Halfway between the two available sizes, so a
 *  widget authored at any size in between still lands on the nearer face. */
const VALUE_THRESHOLD_PX = 48;

/** The face id for a widget's declared size. Anything unrecognised gets the SMALLER face: an
 *  oversized reading is what makes a layout look broken (it overflows its box and is clipped),
 *  while an undersized one is merely quiet. */
export function fontIdFor(w: Pick<Widget, 'font'>): number {
  const size = w.font?.size;
  if (typeof size !== 'number' || !Number.isFinite(size)) return FONT_BODY;
  return size >= VALUE_THRESHOLD_PX ? FONT_VALUE : FONT_BODY;
}

/** The size to show for a widget, so the panel's control reflects what will really be used. */
export function sizeFor(w: Pick<Widget, 'font'>): number {
  return fontIdFor(w) === FONT_VALUE ? 64 : 20;
}
