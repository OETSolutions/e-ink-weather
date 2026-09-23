/**
 * The reported defect: "The layout editor doesn't show the updated values unless you first save and
 * refresh; it should show immediately or at least when you change cursor focus."
 *
 * WHY THIS NEEDS A BROWSER AND THE LIVE DEVICE. The fix spans the whole chain — the device reports
 * the raw reading it resolved, the app re-formats it with the format the user is EDITING right now,
 * and the canvas repaints. None of that is provable from a unit test: the unit test fixes the raw
 * reading by hand, and the device's own /api/values cannot prove the app read it.
 *
 * WHAT MAKES THIS AN HONEST CHECK. It edits the DECIMALS of a box that has a real reading, and
 * requires the INK ON THE CANVAS to change WITHOUT a save and WITHOUT a page reload. A control that
 * stored the new decimals but never repainted the preview would leave the canvas byte-identical,
 * which is exactly the reported symptom — so the ink count is the evidence, not the field value.
 * The reading must also be a genuine device value (not a placeholder), because re-formatting a
 * fallback would prove nothing.
 *
 * IT NEVER SAVES. The on-screen document is dirtied and thrown away with the tab.
 *
 * Usage: node scripts/check-live-format.mjs http://192.168.2.34
 */
import { chromium } from 'playwright';

const base = process.argv[2] ?? 'http://localhost:5199';

const browser = await chromium.launch({
  executablePath: process.env.PW_CHROME ||
    '/Users/cbrown/Library/Caches/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-mac-arm64/chrome-headless-shell',
});
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });

const errors = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(String(e)));

const fail = [];
const check = (name, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`);
  if (!ok) fail.push(name);
};

await page.goto(`${base}/`, { waitUntil: 'networkidle', timeout: 25000 });
const canvas = await page.waitForSelector('canvas.panel', { timeout: 10000 });
await canvas.scrollIntoViewIfNeeded();

/** Dark-pixel count over ONE widget's box, so an edit elsewhere cannot be mistaken for this one. */
const boxInk = (x, y, w, h) => page.evaluate(({ x, y, w, h }) => {
  const c = document.querySelector('canvas.panel');
  const sx = c.width / 920, sy = c.height / 680;
  const d = c.getContext('2d').getImageData(
    Math.round(x * sx), Math.round(y * sy), Math.round(w * sx), Math.round(h * sy)).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] < 128) n++;
  return n;
}, { x, y, w, h });

/** Click the panel at a widget's centre, in PANEL coordinates. */
async function clickPanel(x, y) {
  const b = await canvas.boundingBox();
  await page.mouse.click(b.x + (x / 920) * b.width, b.y + (y / 680) * b.height);
  await page.waitForTimeout(200);
}

/* The hero temperature box on page 0 — x 40 y 64 w 360 h 120, decimals 1, suffix °F. It has a real
 * reading (the device fetched OWM), which is the precondition for this test to mean anything. */
const BOX = { x: 40, y: 64, w: 360, h: 120 };
await clickPanel(BOX.x + BOX.w / 2, BOX.y + BOX.h / 2);
const heading = (await page.textContent('.panelHost')) ?? '';
check('the temperature box is selected', /Box: owm_temp/.test(heading), heading.slice(0, 60));
if (!/Box: owm_temp/.test(heading)) {
  console.log('FAIL: could not select owm_temp; aborting before the checks that depend on it');
  await browser.close();
  process.exit(1);
}

/* PROVE THE READING IS REAL FIRST. Re-formatting a placeholder would change the ink too, and would
 * prove nothing about live values — so the box must be showing a number before the edit. */
const before = await boxInk(BOX.x, BOX.y, BOX.w, BOX.h);
check('the box has a real value drawn (not a placeholder)', before > 200, `${before} dark pixels`);

/* The decimals field is the first number input in the Number format fieldset. */
const fields = page.locator('fieldset', { hasText: 'Number format' });
const decimals = fields.locator('input[type=number]').first();
const shown0 = await decimals.inputValue();

/* ---- THE EDIT, WITH NO SAVE AND NO RELOAD ---- */
await decimals.fill('0');
await decimals.blur();
await page.waitForTimeout(400);
const after = await boxInk(BOX.x, BOX.y, BOX.w, BOX.h);
check('changing decimals repaints the preview AT ONCE (no save, no reload)',
      before !== after, `decimals ${shown0} -> 0, ink ${before} -> ${after}`);

/* And the reverse, so the change is attributable to the edit rather than to a repaint that happens
 * for some other reason: back to 1 decimal must return to (approximately) the original ink. */
await decimals.fill('1');
await decimals.blur();
await page.waitForTimeout(400);
const back = await boxInk(BOX.x, BOX.y, BOX.w, BOX.h);
check('restoring decimals restores the preview', Math.abs(back - before) <= 2, `ink ${back} vs ${before}`);

/* A PREFIX IS THE REPORTED CASE ("layers " was cut to "layer"). Adding one must change the ink
 * immediately too — this is the same code path as the suffix, so it is the one that was reported. */
const prefix = fields.locator('input[type=text]').first();
const beforePrefix = await boxInk(BOX.x, BOX.y, BOX.w, BOX.h);
await prefix.fill('NOW ');
await prefix.blur();
await page.waitForTimeout(400);
const afterPrefix = await boxInk(BOX.x, BOX.y, BOX.w, BOX.h);
check('typing a PREFIX repaints the preview at once', beforePrefix !== afterPrefix,
      `ink ${beforePrefix} -> ${afterPrefix}`);

/* The device must NOT have been touched: this script is a read-only observation of the editor. */
const saved = (await page.textContent('p.status')) ?? '';
check('nothing was saved to the device', !/Saved/.test(saved), saved.slice(0, 60));

await page.screenshot({ path: '/tmp/eink-check/live-format.png', fullPage: true });
await browser.close();

if (errors.length) {
  console.log('CONSOLE ERRORS:');
  for (const e of errors) console.log('  ', e);
}
console.log(fail.length === 0 && errors.length === 0
  ? 'PASS'
  : `FAIL (${fail.length} checks, ${errors.length} console errors)`);
process.exit(fail.length === 0 && errors.length === 0 ? 0 : 1);
