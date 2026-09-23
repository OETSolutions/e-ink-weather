/**
 * The editor must PREVIEW the upscaled sizes, not just offer them.
 *
 * A size selector that lists 512 px while the canvas still draws the old face is the exact defect
 * class this project keeps hitting — a control that changes a label and not the thing it names. So
 * this drives the real editor: select a widget, change its Size to 512, and require the CANVAS INK
 * to grow without a save. The device-side check (check-upscale-on-device.mjs) proves the panel;
 * this proves the preview the user edits against.
 *
 * Usage: node scripts/check-upscale-preview.mjs http://192.168.2.34
 */
import { chromium } from 'playwright';
const base = process.argv[2] ?? 'http://192.168.2.34';
const browser = await chromium.launch({ executablePath: process.env.PW_CHROME ||
  '/Users/cbrown/Library/Caches/ms-playwright/chromium_headless_shell-1234/chrome-headless-shell-mac-arm64/chrome-headless-shell' });
const page = await browser.newPage({ viewport: { width: 1440, height: 1000 } });
const errors = [];
page.on('console', (m) => { if (m.type() === 'error') errors.push(m.text()); });
page.on('pageerror', (e) => errors.push(String(e)));
const fail = [];
const check = (n, ok, d='') => { console.log(`${ok?'ok  ':'FAIL'} ${n}${d?' — '+d:''}`); if(!ok) fail.push(n); };

await page.goto(`${base}/`, { waitUntil: 'networkidle', timeout: 25000 });
const canvas = await page.waitForSelector('canvas.panel');
await canvas.scrollIntoViewIfNeeded();
const b = await canvas.boundingBox();
const ink = () => page.evaluate(() => {
  const c = document.querySelector('canvas.panel');
  const d = c.getContext('2d').getImageData(0,0,c.width,c.height).data;
  let n=0; for (let i=0;i<d.length;i+=4) if (d[i]<128) n++; return n;
});
// Select the hero temperature box (page 0, box at 40,64).
await page.mouse.click(b.x + (40+180)/920*b.width, b.y + (64+60)/680*b.height);
await page.waitForTimeout(400);

/* GROW THE BOX FIRST. The hero box is 360x120; a 512 px glyph (line height ~620) would be
 * clipped hard by it, so a fixed box makes a bigger face draw LESS ink and the comparison would
 * measure the box, not the font. Setting W/H large removes that confound — the same box for every
 * size, exactly like the device check. */
const setWH = (w, h) => page.evaluate(({ w, h }) => {
  const nums = document.querySelectorAll('fieldset input[type=number]');
  // The geometry fieldset is the FIRST with four number inputs labelled X, Y, W, H.
  const all = Array.from(document.querySelectorAll('input[type=number]'));
  // Find the W and H inputs by their preceding label text.
  let wEl = null, hEl = null;
  for (const inp of all) {
    const lab = inp.previousElementSibling?.textContent?.trim() ?? inp.parentElement?.querySelector('label')?.textContent?.trim();
    if (lab === 'W') wEl = inp;
    if (lab === 'H') hEl = inp;
  }
  if (!wEl || !hEl) return false;
  for (const [el, v] of [[wEl, w], [hEl, h]]) {
    el.value = String(v);
    el.dispatchEvent(new Event('input', { bubbles: true }));
    el.dispatchEvent(new Event('change', { bubbles: true }));
  }
  return true;
}, { w, h });
const grew = await setWH(880, 520);
check('the box can be grown before comparing sizes', grew);
await page.waitForTimeout(500);
const inkBefore = await ink();

// Change Size to 512 via the Text fieldset's select.
const changed = await page.evaluate(() => {
  for (const s of document.querySelectorAll('select')) {
    const has512 = Array.from(s.options).some(o => o.textContent.trim() === '512 px');
    if (has512) { s.value = '512'; s.dispatchEvent(new Event('change', { bubbles: true })); return true; }
  }
  return false;
});
check('the editor offers a 512 px size', changed);
await page.waitForTimeout(800);
const inkAfter = await ink();
check('choosing 512 px changes the preview ink immediately', inkAfter !== inkBefore,
      `${inkBefore} -> ${inkAfter}`);
check('the 512 px preview has MORE ink than before', inkAfter > inkBefore, `${inkBefore} -> ${inkAfter}`);
check('no console errors', errors.length === 0, errors.join('; '));
await browser.close();
console.log(fail.length ? `\nFAILED: ${fail.join(', ')}` : '\nall checks passed');
process.exit(fail.length ? 1 : 0);
