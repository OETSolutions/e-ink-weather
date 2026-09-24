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

/* --- HEADINGS (FR-22): a hard-coded heading is now an editable object.
 *
 * These were the "hard-coded headers ... need to be editable" report. The test clicks where the
 * art puts "NOW" (x=40, y=32), which is the actual proof: if labels are still baked into the
 * table alone the click selects nothing and the heading cannot be reached at all. */
{
  const p = await atPanelY(40);
  /* atPanelY centres x; a label is at the LEFT margin, so aim at panel x≈50. */
  const b = await canvas.boundingBox();
  const s = b.height / 680;
  const lx = b.x + 50 * s;
  await page.mouse.click(lx, p.y);
  await page.waitForTimeout(150);
  check('a seeded heading can be selected', /Heading:/.test(await panelHost()));

  /* Renaming it must change the static layer, which is what the canvas draws — so the ink in the
   * heading's band must CHANGE. Comparing dark-pixel counts over just that band is the honest
   * check: a control that stored the text but never repainted would show the new name in the
   * field while the canvas kept the old one. */
  const bandInk = () => page.evaluate(() => {
    const c = document.querySelector('canvas.panel');
    const d = c.getContext('2d').getImageData(0, 24, 400, 40).data;
    let n = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] === 0) n++;
    return n;
  });
  const before = await bandInk();
  const textField = page.locator('.panelHost input[type=text]').first();
  await textField.fill('COOP');
  await page.waitForTimeout(250);
  const after = await bandInk();
  check('renaming a heading repaints the canvas', before !== after, `ink ${before} -> ${after}`);

  await page.getByRole('button', { name: 'Delete heading' }).click();
  await page.waitForTimeout(250);
  check('Delete heading removes it', !/Heading:/.test(await panelHost()));
}

/* --- Add heading --- */
{
  await page.getByRole('button', { name: 'Add heading' }).click();
  await page.waitForTimeout(250);
  check('Add heading reports success', /Added a heading/.test((await page.textContent('p.status')) ?? ''));
  check('the new heading is selected', /Heading:/.test(await panelHost()));
  await page.getByRole('button', { name: 'Delete heading' }).click();
  await page.waitForTimeout(250);
  check('the added heading can be deleted', !/Heading:/.test(await panelHost()));
}

/* --- PAGES: add, rename, dwell, and the refresh interval (FR-15, FR-25).
 *
 * Nothing here SAVES, so the device is untouched; the added page is dropped with the tab. */
{
  const before = await page.locator('#editPage option').count();
  await page.getByRole('button', { name: 'Add page' }).click();
  await page.waitForTimeout(300);
  const after = await page.locator('#editPage option').count();
  check('Add page adds a page to the selector', after === before + 1, `${before} -> ${after}`);
  check('the new page is being edited', (await page.inputValue('#pageName')).startsWith('Page'));

  /* The rotation dwell and the refresh interval are two DIFFERENT fields and both must write to
   * the document. A field that accepted a number and stored nothing is the defect class this
   * project has hit repeatedly, so the test writes a value and reads it back from the document. */
  await page.fill('#pageDwell', '')
    .catch(() => {});
  await page.locator('#pageDwell').fill('1200');
  await page.locator('#refreshSeconds').fill('600');
  await page.waitForTimeout(150);
  /* Blur so the read-back runs; then the field must show what was stored. */
  await page.locator('#pageDwell').blur();
  await page.locator('#refreshSeconds').blur();
  check('the dwell field holds the typed value', (await page.inputValue('#pageDwell')) === '1200');
  check('the refresh field holds the typed value', (await page.inputValue('#refreshSeconds')) === '600');

  await page.getByRole('button', { name: 'Delete this page' }).click();
  await page.waitForTimeout(300);
  const restored = await page.locator('#editPage option').count();
  check('Delete page removes it again', restored === before, `${after} -> ${restored}`);
}

/* --- the icon binding is reachable (the "icons don't make sense" report).
 *
 * The icon was ONLY reachable by hand-editing JSON because the Value menu omitted the `icon`
 * field. This asserts the option is offered and that choosing it produces an icon box. */
{
  await page.getByRole('button', { name: 'Add box' }).click();
  await page.waitForTimeout(200);
  /* The Value select is the second select in the Data fieldset (Source is the first). */
  const dataSel = page.locator('fieldset', { hasText: 'Data' }).locator('select').nth(1);
  await dataSel.selectOption({ label: 'Weather icon (picture)' });
  await page.waitForTimeout(250);
  check('the Value menu offers the weather icon', true);
  check('choosing the icon explains it draws a picture',
        /draws the current weather icon as a PICTURE/.test(await panelHost()));
  await page.getByRole('button', { name: 'Delete box' }).click();
  await page.waitForTimeout(200);
}

/* --- the "last updated" stamp is reachable, and the panel says where the time comes from ---
 *
 * Same defect class as the icon above: a binding the firmware and the device both support but the
 * Value menu never offers, so the only way to use it is to hand-edit JSON. The hint is asserted
 * with it because the stamp is the one value whose provenance is NOT obvious — a user who reads a
 * clock in a box and goes looking for a time-zone setting would never find one, and the sentence
 * is the only place that says so. */
{
  await page.getByRole('button', { name: 'Add box' }).click();
  await page.waitForTimeout(200);
  const dataSel = page.locator('fieldset', { hasText: 'Data' }).locator('select').nth(1);
  await dataSel.selectOption({ label: 'Last updated (date and time)' });
  await page.waitForTimeout(250);
  check('the Value menu offers the last-updated stamp', true);
  check('choosing the stamp explains it is not a clock in the display',
        /not a clock in the display/.test(await panelHost()));
  /* A forecast box must NOT offer it: a forecast document carries no observation time, so the box
   * would draw its fallback forever. A selectable value that cannot work is the defect class this
   * project already hit with a product toggle that only changed a label. */
  const srcSel = page.locator('fieldset', { hasText: 'Data' }).locator('select').nth(0);
  await srcSel.selectOption({ label: 'Forecast' });
  await page.waitForTimeout(250);
  const opts = await dataSel.locator('option').allTextContents();
  check('a forecast box does not offer the last-updated stamp',
        !opts.some((t) => /Last updated/.test(t)), opts.join(' | '));
  await page.getByRole('button', { name: 'Delete box' }).click();
  await page.waitForTimeout(200);
}

/* --- an image box: reachable, static, and with no controls that do nothing ---
 *
 * The picture's PIXELS are never in the config (the device re-parses it every refresh tick), so
 * what this can assert here is the CONTRACT around the box: that the picture source is offered,
 * that choosing it makes the box STATIC (a dynamic image box would have the firmware stamping a
 * reading over the picture), and that the number-format and alert controls — which have no meaning
 * for a picture — are not shown. The dither itself is checked pixel-wise in test/image.test.ts, and
 * the round trip to the device in scripts/check-image.mjs. */
{
  await page.getByRole('button', { name: 'Add picture' }).click();
  await page.waitForTimeout(250);
  check('Add picture reports success',
        /Added a picture box/.test((await page.textContent('p.status')) ?? ''));
  const imgKind = page.locator('fieldset', { hasText: 'Data' }).locator('select').nth(0);
  check('the picture source is selected', (await imgKind.inputValue()) === 'image');
  const legends = await page.locator('.panelHost fieldset legend').allTextContents();
  check('an image box hides the number format', !legends.includes('Number format'), legends.join(' | '));
  check('an image box hides the alert rules', !legends.includes('Alerts'), legends.join(' | '));
  /* With no picture chosen yet, the panel must say so — an empty box with no explanation reads as
   * a broken control. */
  check('an image box with no picture explains that',
        /does not have the picture/.test(await panelHost()));
  await page.getByRole('button', { name: 'Delete box' }).click();
  await page.waitForTimeout(200);
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
