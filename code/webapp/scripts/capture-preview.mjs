/**
 * Capture the layout editor's panel canvas as a PNG, for documentation.
 *
 * WHY THE CANVAS AND NOT A PHOTOGRAPH: a phone/bench photo of the glass picks up overhead-light
 * glare and the bevel around it, which obscures exactly the thing the picture is meant to show.
 * The editor draws the SAME frame the device renders — same geometry, same fonts, same resolved
 * values from /api/values — into a 920x680 canvas, so a capture of it is the clean, glare-free
 * version of "what is on the glass".
 *
 * The canvas backing store is PANEL_WIDTH x PANEL_HEIGHT (920x680) regardless of the on-screen
 * zoom, so toDataURL() yields the full-resolution frame rather than the CSS-scaled pixels.
 *
 * Usage:  node scripts/capture-preview.mjs http://192.168.2.34 docs/images/device-preview.png
 */
import { chromium } from 'playwright';
import { writeFileSync } from 'node:fs';

const base = process.argv[2] ?? 'http://192.168.2.34';
const out = process.argv[3] ?? 'docs/images/device-preview.png';

const browser = await chromium.launch({
  executablePath: process.env.PW_CHROME ||
    '/Users/cbrown/Library/Caches/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-mac-arm64/chrome-headless-shell',
});
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
await page.goto(`${base}/`, { waitUntil: 'domcontentloaded', timeout: 20000 });
await page.waitForSelector('canvas.panel', { timeout: 10000 });

/* Give the app a moment to fetch /api/values and repaint the preview with real readings — a
 * capture taken before that shows placeholders ("--"), which is not what the glass shows. Poll
 * the canvas's ink count until it stops changing, which means the values have landed. */
let prevInk = -1;
for (let i = 0; i < 40; i++) {
  const ink = await page.evaluate(() => {
    const c = document.querySelector('canvas.panel');
    if (!c) return -1;
    const g = c.getContext('2d');
    const d = g.getImageData(0, 0, c.width, c.height).data;
    let n = 0;
    for (let k = 0; k < d.length; k += 4) if (d[k] < 128) n++;
    return n;
  });
  if (ink > 0 && ink === prevInk) break;
  prevInk = ink;
  await page.waitForTimeout(250);
}

const dataUrl = await page.evaluate(() => {
  const c = document.querySelector('canvas.panel');
  return c.toDataURL('image/png');
});
const b64 = dataUrl.replace(/^data:image\/png;base64,/, '');
writeFileSync(out, Buffer.from(b64, 'base64'));
console.log(`wrote ${out} (${b64.length} b64 chars)`);
await browser.close();
