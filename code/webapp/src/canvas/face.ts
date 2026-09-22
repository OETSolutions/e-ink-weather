/**
 * Which font face a widget renders with.
 *
 * THE CONFIG'S `font.size` IS ADVISORY, AND THIS IS THE ONLY PLACE IT IS INTERPRETED. The panel
 * has a fixed LADDER of rasterised faces (FR-4a: glyphs are rasterised once at the exact pixel
 * size, because a 1 bpp panel cannot anti-alias and a runtime rasteriser would cost RAM and
 * flash the device does not have). So a widget's size cannot be an arbitrary number; it selects
 * the NEAREST face on the ladder.
 *
 * WHY A SHARED RULE RATHER THAN AN INLINE TEST: the renderer, the editor and the golden
 * fixture all need the same answer, and they had drifted — the editor keyed off `role` while
 * the fixture keyed off `size > 48`, so a widget could preview in one face and be stamped in
 * another. The preview would then be a confident lie about the very thing it exists to show.
 * THE FIRMWARE HAS THE SAME RULE (font_face_for_px in lib/layout/src/fonts.c); this is the
 * web-app copy, and the golden-image test is what keeps the two honest.
 */

import type { Widget } from '../model/config';
import { FACES, FACE_PX, FONT_20, FONT_64 } from './atlas-data';

/** The faces the panel actually has, with the sizes to offer a user. Built from the generated
 *  ladder so the menu can never list a size the device cannot draw, nor omit one it can. */
export const FACES_AVAILABLE = FACE_PX.map((px, id) => ({
  id,
  px,
  label: `${px} px`,
})) as readonly { id: number; px: number; label: string }[];

/**
 * The face id nearest a declared pixel size, comparing by RATIO.
 *
 * NEAREST-BY-RATIO, not "big is big": the old two-face rule used a threshold at 48 px, so a
 * user who asked for 48 got the 64 px face — 33% larger than requested. The eye reads size
 * multiplicatively, so ratio is the right distance. Ties go to the SMALLER face: an oversized
 * reading overflows its box and is clipped (it looks broken), while an undersized one is merely
 * quiet.
 *
 * Unrecognised input gets the body face — the same conservative default the firmware takes.
 */
export function faceIdForPx(size: number): number {
  if (typeof size !== 'number' || !Number.isFinite(size) || size <= 0) return FONT_20;

  let best = FONT_20;
  let bestErr = Infinity;
  for (const f of FACES_AVAILABLE) {
    const err = f.px > size ? f.px / size : size / f.px;
    /* Strict `<` keeps the FIRST (smaller) face on a tie, since the ladder is ascending. */
    if (err < bestErr) {
      bestErr = err;
      best = f.id;
    }
  }
  return best;
}

/** The face id for a widget's declared size. */
export function fontIdFor(w: Pick<Widget, 'font'>): number {
  return faceIdForPx(w.font?.size as number);
}

/** The size to show for a widget, so the panel's control reflects what will really be used. */
export function sizeFor(w: Pick<Widget, 'font'>): number {
  return FACES[fontIdFor(w)]?.px ?? 20;
}

/** Re-exported so callers can name the two role faces without reaching into atlas-data. */
export { FONT_20, FONT_64 };
