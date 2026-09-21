/**
 * Load the DEVICE-SERVED config app in a real browser and verify it works end to end.
 *
 * WHY THIS AND NOT THE UNIT TESTS: the app is served from the device's own flash over plain HTTP
 * on the LAN, from a bundle built by a different command than the unit tests run against. Unit
 * tests passing says the SOURCE is right; only a browser loading http://<device>/ says the thing
 * the user opens actually works — including that the browser bundle has no Node-only imports
 * (which is how the node:zlib bug surfaced: the build failed, but only for the device target).
 *
 * It also exercises CompressionStream, which the artwork encoder now depends on and which no
 * Node unit test can vouch for in the browser.
 *
 * SETUP: playwright is deliberately NOT a package.json dependency — it is ~300 MB of browser and
 * this app ships as a 66 KB gzipped bundle to a 4 MB device. Install it ad hoc and point the
 * script at it:
 *
 *   npm i -g playwright && npx playwright install chromium
 *   PW_CHROME=/path/to/chrome-headless-shell node scripts/browser-check.mjs http://<ip>
 *
 * Without PW_CHROME it falls back to a path under ms-playwright; set the env var if yours differs.
 *
 * Usage:  node scripts/browser-check.mjs http://192.168.2.34
 */

import { chromium } from 'playwright';

const base = process.argv[2] ?? 'http://192.168.2.34';
/* Point at the headless shell that is actually installed. Playwright's own resolution expects
 * the exact revision its version pins, and the npx cache here has a different one — using the
 * binary directly avoids a browser download for a check that only needs Chromium's engine. */
const browser = await chromium.launch({
  executablePath: process.env.PW_CHROME ||
    '/Users/cbrown/Library/Caches/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-mac-arm64/chrome-headless-shell',
});
const page = await browser.newPage();

const errors = [];
const requests = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(String(e)));
page.on('requestfailed', (r) => errors.push(`request failed: ${r.url()} (${r.failure()?.errorText})`));
page.on('response', (r) => requests.push(`${r.status()} ${r.url().replace(base, '')}`));

await page.goto(`${base}/`, { waitUntil: 'networkidle', timeout: 20000 });

/* The editor's own readiness signal: the canvas the layout is drawn on. Its presence means the
 * module graph executed, which is exactly what the node:zlib bug prevented. */
const canvas = await page.waitForSelector('canvas.panel', { timeout: 10000 }).catch(() => null);
const title = await page.title();

/* The page must have fetched the config from the device — a 200 on /api/config is the proof the
 * app is talking to real hardware rather than a bundled fixture. */
const gotConfig = requests.some((r) => r.startsWith('200') && r.includes('/api/config'));

console.log('title:            ', title);
console.log('canvas rendered:  ', canvas ? 'YES' : 'NO');
console.log('fetched /api/config:', gotConfig ? 'YES' : 'NO');

/* ---- the save path, which is what actually puts artwork on the glass ---- */
let saveOk = false;
let artPosts = 0;
if (canvas) {
  page.on('response', (r) => {
    if (r.url().includes('/api/artwork')) artPosts++;
  });
  const saveBtn = page.getByRole('button', { name: /Save to device/i });
  await saveBtn.click();
  /* The button carries its own state; wait for it to leave "Saving…" so the async push has
   * finished before reading status. */
  await page.waitForFunction(
    () => {
      const b = [...document.querySelectorAll('button')].find((x) => /Save to device|Saved|Save failed/.test(x.textContent || ''));
      return b && !/Saving/.test(b.textContent || '');
    },
    { timeout: 30000 },
  ).catch(() => {});
  const label = await saveBtn.textContent();
  saveOk = !/failed/i.test(label ?? '');
  console.log('save button state:', label);
}

/* The device reports how many artwork pages it holds. > 0 is the proof the push reached flash and
 * promoted — the exact thing that silently read 0 before the fix. */
let artworkPages = -1;
try {
  const s = await page.evaluate(async (b) => (await fetch(`${b}/api/status`)).json(), base);
  artworkPages = s.artwork_pages;
} catch { /* reported as -1 below */ }
console.log('artwork /api/artwork posts:', artPosts);
console.log('device artwork_pages:', artworkPages);

/* ---- the preview must show the device's REAL fetched values (FR-27) ----
 *
 * The canvas is a bitmap, so the values are not readable from the DOM. Instead the check asks the
 * device what it resolved and asserts the app actually requested it — a 200 on /api/values is what
 * distinguishes "the preview shows real data" from "the app only ever showed placeholders", which
 * is the state FR-27 was written against and which looks identical on screen. */
const gotValues = requests.some((r) => r.startsWith('200') && r.includes('/api/values'));
let liveSample = '';
try {
  const v = await page.evaluate(async (b) => (await fetch(`${b}/api/values`)).json(), base);
  const real = (v.values ?? []).find((x) => x.has_value);
  liveSample = real ? `${real.id}="${real.text}"` : '';
} catch { /* reported below */ }
console.log('fetched /api/values:', gotValues ? 'YES' : 'NO');
console.log('a real resolved value:', liveSample || '(none reported)');

console.log('console errors:   ', errors.length ? errors : 'none');
console.log('requests:', requests.filter((r) => r.includes('/api/')).join('\n          '));

await browser.close();
/* `gotValues` is required: without it the preview silently degrades to placeholders, which is a
 * functional regression even though every other check still passes. `liveSample` is reported but
 * NOT required — a device that has never had a successful fetch legitimately has no values. */
const ok = canvas && gotConfig && gotValues && saveOk && artworkPages > 0 && errors.length === 0;
console.log(ok ? '\nPASS' : '\nFAIL');
process.exit(ok ? 0 : 1);
