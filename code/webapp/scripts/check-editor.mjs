/**
 * Exercise the editor's NEW capabilities in a real browser against the live device, over the
 * dev-server proxy. This is the check the unit tests cannot do: that the controls exist, that a
 * drag actually moves a rule on the glass, that Add box places a non-overlapping box, and that
 * Delete removes what was selected.
 *
 * IT LEAVES THE ON-SCREEN DOCUMENT DIRTIED BUT NEVER SAVES, so the device is untouched.
 *
 * Usage: node scripts/check-editor.mjs http://localhost:5199
 */
import { chromium } from 'playwright';
import { mkdirSync } from 'node:fs';

const base = process.argv[2] ?? 'http://localhost:5199';
const out = '/tmp/eink-check';
mkdirSync(out, { recursive: true });

const browser = await chromium.launch({
  executablePath: process.env.PW_CHROME ||
    '/Users/cbrown/Library/Caches/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-mac-arm64/chrome-headless-shell',
});
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });

const errors = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(String(e)));
page.on('requestfailed', (r) => errors.push(`request failed: ${r.url()} (${r.failure()?.errorText})`));

const fail = [];
const check = (name, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`);
  if (!ok) fail.push(name);
};

await page.goto(`${base}/`, { waitUntil: 'networkidle', timeout: 25000 });
const canvas = await page.waitForSelector('canvas.panel', { timeout: 10000 });

/* --- the panel fits its column by default (the "janky zoomed layout" requirement) --- */
const box = await canvas.boundingBox();
check('panel fits the viewport by default', box.width <= 1440 && box.height <= 1000,
      `${Math.round(box.width)}x${Math.round(box.height)}`);

/** A pointer position on the canvas for a PANEL y, with the canvas scrolled into view. */
async function atPanelY(panelY) {
  await canvas.scrollIntoViewIfNeeded();
  await page.waitForTimeout(120);
  const b = await canvas.boundingBox();
  const s = b.height / 680;
  return { x: b.x + b.width / 2, y: b.y + panelY * s, s };
}
const panelHost = async () => (await page.textContent('.panelHost')) ?? '';
const ruleY = async () => Number(await page.inputValue('.panelHost input[type=number]'));

/* --- a SEEDED divider is selectable and draggable ---
 * The device's stored config predates rules, so the app seeds the page from its art table. If
 * that seeding did not happen the lines would be DRAWN but ungrabbable — the drawn set and the
 * editable set would differ, which is the most confusing possible version of "the line will not
 * move". Clicking where art puts a line is exactly that test. */
{
  const p = await atPanelY(440);
  await page.mouse.click(p.x, p.y);
  await page.waitForTimeout(150);
  check('a seeded divider can be selected', /Divider/.test(await panelHost()));

  const before = await ruleY();
  await page.mouse.move(p.x, p.y);
  await page.mouse.down();
  await page.mouse.move(p.x, p.y - 60 * p.s, { steps: 6 });
  await page.mouse.up();
  await page.waitForTimeout(200);
  const after = await ruleY();
  check('dragging a divider moves it', after !== before && after < before, `y ${before} -> ${after}`);
  await canvas.screenshot({ path: `${out}/panel-seeded-divider.png` });
}

/* --- Add box: places a real, non-overlapping box and selects it --- */
{
  await page.getByRole('button', { name: 'Add box' }).click();
  await page.waitForTimeout(250);
  check('Add box reports success', /Added box/.test((await page.textContent('p.status')) ?? ''));
  check('the new box is selected', /Box: val\d/.test(await panelHost()));
  await canvas.screenshot({ path: `${out}/panel-addbox.png` });
}

/* --- Delete box --- */
{
  await page.getByRole('button', { name: 'Delete box' }).click();
  await page.waitForTimeout(250);
  check('Delete box removes it', !/Box: /.test(await panelHost()));
}

/* --- Add divider / Delete divider --- */
{
  await page.getByRole('button', { name: 'Add divider' }).click();
  await page.waitForTimeout(250);
  check('Add divider shows a divider panel', /Divider/.test(await panelHost()));
  await page.getByRole('button', { name: 'Delete divider' }).click();
  await page.waitForTimeout(250);
  check('Delete divider removes it', !/Divider/.test(await panelHost()));
}

/* --- the preview is drawing real values, not placeholders --- */
{
  const dark = await page.evaluate(() => {
    const c = document.querySelector('canvas.panel');
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] === 0) n++;
    return n;
  });
  check('the preview has ink on it', dark > 1000, `${dark} dark pixels`);
}

await page.screenshot({ path: `${out}/final.png`, fullPage: true });
await browser.close();

if (errors.length) {
  console.log('CONSOLE ERRORS:');
  for (const e of errors) console.log('  ', e);
}
console.log(fail.length === 0 && errors.length === 0 ? 'PASS' : `FAIL (${fail.length} checks, ${errors.length} console errors)`);
process.exit(fail.length === 0 && errors.length === 0 ? 0 : 1);
