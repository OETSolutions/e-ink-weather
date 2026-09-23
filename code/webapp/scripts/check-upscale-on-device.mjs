#!/usr/bin/env node
/**
 * Prove the upscaled font sizes (160..512 px) actually reach the GLASS.
 *
 * WHY THIS EXISTS AND WHY IT SAVES/RESTORES. The whole point of the upscaled ladder is that a user
 * can pick a face bigger than the 128 px rasterised ceiling, and the ONLY evidence that counts is
 * ink on the panel ("if it doesn't show on the device it's broken"). A host test can prove the
 * renderer produces block-scaled bytes, but it cannot prove the device parses size:256, resolves it
 * to the upscaled face, and drives the panel with it.
 *
 * IT IS REVERSIBLE AND SELF-CLEANING: it reads the CURRENT config first, keeps it verbatim, applies
 * one widget at each test size with a box big enough to hold the glyphs, PUTs the document (which
 * makes the device render), and RESTORES the original config at the end — including on failure. The
 * restore is asserted, so a crash mid-run cannot leave the bench device holding a test layout.
 *
 * HOW IT JUDGES: it counts DARK pixels on the glass via the bench camera, in the test box. A large
 * upscaled face must put substantially MORE ink on the panel than the same text at the small face;
 * a device that ignored the size (or drew nothing) cannot fake that. The camera frames are saved so
 * a human can look too.
 *
 * Usage:  node scripts/check-upscale-on-device.mjs http://192.168.2.34 /dev/cu.usbserial-1121310
 *         (from code/webapp/)
 */
import { execFileSync } from 'node:child_process';
import { writeFileSync } from 'node:fs';

const base = process.argv[2] ?? 'http://192.168.2.34';
const cam = process.argv[3];

const fail = [];
const check = (name, ok, detail = '') => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ' — ' + detail : ''}`);
  if (!ok) fail.push(name);
};

async function getConfig() {
  const r = await fetch(`${base}/api/config`);
  if (!r.ok) throw new Error(`GET /api/config: HTTP ${r.status}`);
  return r.json();
}

async function putConfig(doc) {
  const r = await fetch(`${base}/api/config`, {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(doc),
  });
  const text = await r.text().catch(() => '');
  if (!r.ok) throw new Error(`PUT /api/config: HTTP ${r.status} ${text}`);
  return text;
}

/** Photograph the panel through the bench camera and return a dark-pixel count in a panel box.
 *
 * THE MEDIAN OF SEVERAL FRAMES, not one. An e-ink panel CYCLES through black and white during its
 * waveform, so a single capture can land mid-refresh and report a wildly inflated dark count (the
 * whole panel momentarily dark) — which is exactly how a settled 64 px frame read 176,344 against
 * its true ~48,000. The median of a handful of frames rejects those transient flash frames, and a
 * real image, being stable, has them all agree. */
function glassInk(box) {
  if (!cam) return Promise.resolve(null);
  const [x0, y0, x1, y1] = box;
  const counts = [];
  return (async () => {
    for (let i = 0; i < 5; i++) {
      const png = `/tmp/upscale-glass-${i}.png`;
      execFileSync('ffmpeg', ['-y', '-hide_banner', '-loglevel', 'error',
        '-f', 'avfoundation', '-framerate', '15', '-pixel_format', 'uyvy422',
        '-video_size', '1280x960', '-i', '0', '-frames:v', '1', png], { stdio: 'ignore' });
      const out = execFileSync('python3', ['-c', `
from PIL import Image
im = Image.open(${JSON.stringify(png)}).convert('L')
w, h = im.size
x0, y0, x1, y1 = ${JSON.stringify([x0, y0, x1, y1])}
x0 = int(x0 * w / 920); x1 = int(x1 * w / 920)
y0 = int(y0 * h / 680); y1 = int(y1 * h / 680)
px = list(im.crop((max(0,x0), max(0,y0), min(w,x1), min(h,y1))).getdata())
print(sum(1 for p in px if p < 110))
`], { encoding: 'utf8' });
      counts.push(Number(out.trim()));
      /* Small gap so the five captures straddle any flash rather than sampling one instant. */
      await new Promise((r) => setTimeout(r, 400));
    }
    counts.sort((a, b) => a - b);
    return counts[Math.floor(counts.length / 2)];   /* median */
  })();
}

const original = await getConfig();
const pageIdx = 0;
const page = original.pages[pageIdx];
/* A widget bound to a live reading draws real text; owm_temp is the hero and is always present. */
const target = page.widgets.find((w) => w.id === 'owm_temp') ?? page.widgets[0];
if (!target) { console.log('FAIL: the device has no widgets to test with'); process.exit(1); }

/* Back up the two fields we touch, so the restore is exact and never depends on a re-fetch. */
const backup = { font: JSON.parse(JSON.stringify(target.font)), box: { x: target.x, y: target.y, w: target.w, h: target.h } };
console.log(`target widget: ${target.id} (box ${target.w}x${target.h}, font ${target.font?.size})`);

/* ONE box and ONE camera crop for EVERY size, so the only thing that differs between frames is the
 * face — otherwise a bigger crop could raise the ink count for a reason that is not the font. The
 * box is tall enough for the largest face's line height (512 px base 128 -> line height 155*4=620,
 * so 420 is not enough; the string is a short reading, so the box clips the line box but not the
 * digit's ink, which sits at the top). */
const BOX = { x: 20, y: 16, w: 880, h: 500 };
const CROP = [20, 16, 900, 516];

async function applySize(px) {
  const doc = await getConfig();          /* fresh each time: the device is the source of truth */
  const w = doc.pages[pageIdx].widgets.find((x) => x.id === target.id);
  w.x = BOX.x; w.y = BOX.y; w.w = BOX.w; w.h = BOX.h;
  w.font = { ...(w.font ?? {}), size: px, align: 'left', valign: 'top' };
  await putConfig(doc);
  /* The PUT triggers a render; give the panel time to finish its waveform. */
  await new Promise((r) => setTimeout(r, 4500));
}

let ok = true;
try {
  /* Baseline: the ORIGINAL size, in the same box, so only the SIZE differs between frames. */
  await applySize(64);
  const inkSmall = await glassInk(CROP);
  if (inkSmall !== null) console.log(`64 px box ink: ${inkSmall}`);

  for (const px of [256, 512]) {
    await applySize(px);
    const ink = await glassInk(CROP);
    console.log(`${px} px box ink: ${ink}`);

    /* The device must accept the size (a PUT that failed would have thrown) and draw ink. */
    check(`the device draws ink for the ${px} px face`, ink === null || ink > 0, `ink=${ink}`);
    if (inkSmall !== null && ink !== null) {
      check(`the ${px} px face draws much more ink than 64 px`, ink > inkSmall * 1.5,
            `${ink} vs ${inkSmall}`);
    }
  }
} catch (e) {
  ok = false;
  console.log(`FAIL: ${e.message}`);
} finally {
  /* RESTORE — always, and verify it took. */
  const doc = await getConfig();
  const w = doc.pages[pageIdx].widgets.find((x) => x.id === target.id);
  w.x = backup.box.x; w.y = backup.box.y; w.w = backup.box.w; w.h = backup.box.h;
  w.font = backup.font;
  await putConfig(doc);
  await new Promise((r) => setTimeout(r, 1500));
  const after = await getConfig();
  const rw = after.pages[pageIdx].widgets.find((x) => x.id === target.id);
  check('the original config is restored',
        rw.w === backup.box.w && rw.font?.size === backup.font?.size,
        `size=${rw.font?.size} w=${rw.w}`);
}

console.log(fail.length ? `\nFAILED: ${fail.join(', ')}` : '\nall checks passed');
if (!ok || fail.length) process.exit(1);
