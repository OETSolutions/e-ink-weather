/**
 * Emit the golden fixture JSON from the default layout preset.
 *
 * Run through vitest (so it resolves TypeScript and the atlas the same way the app does):
 *     npx vitest run scripts/emit-golden-fixture.test.ts
 * then regenerate the firmware side:
 *     python3 firmware/tools/gen_default_layout_h.py <fixture> firmware/tools/golden/default_layout.h
 *
 * WHY THIS EXISTS: the golden image is a cross-check between two renderers, and it only means
 * anything if both are fed the SAME layout. Emitting it from the preset means a layout edit
 * propagates to the firmware's golden by re-running two commands, with no second hand-edited
 * copy to fall out of step.
 */
import { it } from 'vitest';
import { writeFileSync } from 'node:fs';
import { defaultLayout, DEFAULT_LABELS, DEFAULT_RULES } from '../src/presets/default-layout';
import { fontIdFor } from '../src/canvas/face';

/** The value used for every sample. A number with a tenth, so a decimals/rounding mistake is
 *  visible in the golden rather than rounding away. */
const SAMPLE = '68.4';

/* Named *.test.ts ONLY because that is what vitest globs. It is skipped unless asked
 * for explicitly, so a normal `npm test` cannot rewrite the committed fixture. */
it.skipIf(!process.env.GOLDEN_OUT)('emits the golden fixture for the firmware', () => {
  const page = defaultLayout().pages[0]!;
  const doc = {
    schemaVersion: 1,
    updateSeconds: 900,
    partialRefreshLimit: 5,
    pages: [{ name: 'default', refreshSeconds: 900, weight: 1 }],
    labels: DEFAULT_LABELS,
    rules: DEFAULT_RULES,
    fields: page.widgets.map((w) => ({
      x: w.x, y: w.y, w: w.w, h: w.h,
      alignH: w.font?.align === 'center' ? 'C' : w.font?.align === 'right' ? 'R' : 'L',
      alignV: w.font?.valign === 'middle' ? 'M' : w.font?.valign === 'bottom' ? 'B' : 'T',
      /* The SAME face rule the renderer and the editor use. Keying off size here while the
       * editor keyed off role is how the two drifted before. */
      font: fontIdFor(w),
      sample: SAMPLE,
    })),
  };
  const out = process.env.GOLDEN_OUT ?? 'test/fixtures/golden-default.json';
  writeFileSync(out, JSON.stringify(doc, null, 2) + '\n');
});
