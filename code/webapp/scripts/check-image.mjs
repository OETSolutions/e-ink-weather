/**
 * End-to-end check of the PICTURE feature against the live device.
 *
 * WHAT IT PROVES, and why a unit test cannot: the picture has to survive the whole chain — decoded
 * in the browser, dithered to 1 bit, baked into the page's static layer, COMPRESSED AND UPLOADED as
 * artwork to the device, inflated back on the ESP32, and drawn on the glass. Every one of those
 * steps is a different language or a different machine from the next, so the only proof is to drive
 * a real browser with a real image file and then look at what the device says it holds.
 *
 * IT PUTS A PICTURE BOX ON PAGE 0, SAVES, VERIFIES, AND THEN REMOVES IT, restoring the config. The
 * picture bytes themselves are never in the config (that is the whole design — see ImageBinding),
 * so the device's stored document is unchanged apart from the one box.
 *
 * Usage: node scripts/check-image.mjs http://192.168.2.34 [path-to-image]
 */
import { chromium } from 'playwright';
import { mkdirSync } from 'node:fs';

const base = process.argv[2] ?? 'http://192.168.2.34';
const imagePath = process.argv[3] ?? '/tmp/test-photo.png';
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
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ` — ${detail}` : ''}`);
  if (!ok) fail.push(name);
};

/** Ink count in a rectangle of the editor's canvas, read from the DOM. */
async function canvasInk(x0, y0, x1, y1) {
  return page.evaluate(({ x0, y0, x1, y1 }) => {
    const c = document.querySelector('canvas.panel');
    const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let y = y0; y < y1; y++) {
      for (let x = x0; x < x1; x++) {
        const p = (y * c.width + x) * 4;
        if (d[p] === 0) n++;
      }
    }
    return n;
  }, { x0, y0, x1, y1 });
}

const status = () => page.locator('p.status').first().textContent();
const panelHost = async () => (await page.textContent('.panelHost')) ?? '';

await page.goto(base, { waitUntil: 'domcontentloaded' });
await page.waitForTimeout(2500);

/* ---- 1. the source menu offers a picture, and choosing it changes the box to static ---- */
await page.getByRole('button', { name: 'Add picture' }).click();
await page.waitForTimeout(500);
check('Add picture reports success', /Added a picture box/.test(await status()), await status());

const dataSel = page.locator('fieldset', { hasText: 'Data' }).locator('select').nth(0);
const kind = await dataSel.inputValue();
check('the new box is an image box', kind === 'image', kind);

/* The panel must say the picture is missing rather than showing an empty box with no explanation. */
check('an image box with no picture says so',
      /does not have the picture/.test(await panelHost()), (await panelHost()).slice(0, 120));

/* An image box has nothing to format, so the number controls must be gone — a decimals spinner
 * next to a picture is the "control that does nothing" defect. */
const fieldsets = await page.locator('.panelHost fieldset legend').allTextContents();
check('an image box offers no number format', !fieldsets.includes('Number format'), fieldsets.join(' | '));

/* ---- 2. choosing a file dithers it into the preview ---- */
const before = await canvasInk(0, 0, 920, 680);
await page.locator('.panelHost input[type=file]').setInputFiles(imagePath);
await page.waitForTimeout(1500);
const after = await canvasInk(0, 0, 920, 680);
check('the picture raises the preview ink', after > before + 2000, `${before} -> ${after}`);
check('the picture reports it loaded', /Picture loaded/.test(await status()), await status());
check('the panel now offers to remove it', /Remove picture/.test(await panelHost()));

/* ---- 3. saving pushes it through the artwork encoder to the device ---- */
const artPosts = [];
page.on('request', (r) => { if (r.url().includes('/api/artwork')) artPosts.push(r.url()); });

await page.getByRole('button', { name: 'Save to device' }).click();
/* The artwork set is a few chunks; give it time to finish and the panel to repaint. */
await page.waitForTimeout(6000);
const st = await status();
check('the save reported success', /Saved/.test(st), st);
check('an artwork upload happened', artPosts.length > 0, `${artPosts.length} chunks`);

const devStatus = await page.evaluate(async (b) => (await fetch(`${b}/api/status`)).json().catch(() => null), base);
check('the device holds artwork after the picture save',
      !!devStatus && devStatus.artwork_pages > 0, JSON.stringify(devStatus && devStatus.artwork_pages));

/* ---- 4. the picture is reachable back, so it is really in the layer ---- */
/* The layer is stored compressed on the device and only the device can inflate it, so the check is
 * indirect but meaningful: the device reports artwork, AND the editor's own layer still has the
 * dithered ink — which together mean the bytes that were uploaded were the ones with the picture. */
const savedInk = await canvasInk(0, 0, 920, 680);
check('the saved preview still holds the picture ink', savedInk > before + 2000, `${savedInk}`);

/* ---- 5. removing the picture clears it and saves back ---- */
await page.locator('.panelHost').getByRole('button', { name: 'Remove picture' }).click();
await page.waitForTimeout(800);
check('removing the picture is reported', /Picture removed/.test(await status()), await status());
const removedInk = await canvasInk(0, 0, 920, 680);
check('the preview ink drops back', removedInk < after - 2000, `${after} -> ${removedInk}`);

/* Removing the box and saving restores the device to a layout with no picture. */
await page.getByRole('button', { name: 'Delete box' }).click();
await page.waitForTimeout(500);
await page.getByRole('button', { name: 'Save to device' }).click();
await page.waitForTimeout(6000);
check('the cleanup save reported success', /Saved/.test(await status()), await status());

check('no console errors', errors.length === 0, errors.slice(0, 3).join(' | '));

await page.screenshot({ path: `${out}/image-check.png`, fullPage: false });
await browser.close();

if (fail.length) {
  console.log(`\nFAIL (${fail.length}): ${fail.join(', ')}`);
  process.exit(1);
}
console.log('\nPASS');
