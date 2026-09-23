#!/usr/bin/env node
/**
 * Prove the reported defect "resizing header text boxes above 40 cut off and don't fit text" is
 * fixed on the GLASS.
 *
 * A heading is a LABEL — one line of text baked into the static artwork. It went through the same
 * drawField path as a widget, but with a hardcoded 500x40 clip box. The moment the font ladder grew
 * past the 40 px face (line height 49), a heading at 48 px or larger was cut off at 40 px tall, and
 * a heading wider than 500 px lost its tail. The box is now the rest of the panel.
 *
 * HOW IT JUDGES: it sets a heading to a large size and counts DARK pixels on the glass BELOW the old
 * 40 px cutoff row. Before the fix that band was necessarily empty; after it, a large heading puts
 * ink there. It restores the original config at the end, including on failure.
 *
 * Usage:  node scripts/check-heading-clip.mjs http://192.168.2.34 /dev/cu.usbserial-1121310
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

/** Median dark-pixel count in a panel-space box, over five camera frames (see the note in
 * check-upscale-on-device.mjs on why a single frame cannot be trusted). */
function glassInk(box) {
  if (!cam) return Promise.resolve(null);
  const counts = [];
  return (async () => {
    for (let i = 0; i < 5; i++) {
      const png = `/tmp/heading-glass-${i}.png`;
      execFileSync('ffmpeg', ['-y', '-hide_banner', '-loglevel', 'error',
        '-f', 'avfoundation', '-framerate', '15', '-pixel_format', 'uyvy422',
        '-video_size', '1280x960', '-i', '0', '-frames:v', '1', png], { stdio: 'ignore' });
      const out = execFileSync('python3', ['-c', `
from PIL import Image
im = Image.open(${JSON.stringify(png)}).convert('L')
w, h = im.size
x0, y0, x1, y1 = ${JSON.stringify(box)}
x0 = int(x0 * w / 920); x1 = int(x1 * w / 920)
y0 = int(y0 * h / 680); y1 = int(y1 * h / 680)
px = list(im.crop((max(0,x0), max(0,y0), min(w,x1), min(h,y1))).getdata())
print(sum(1 for p in px if p < 110))
`], { encoding: 'utf8' });
      counts.push(Number(out.trim()));
      await new Promise((r) => setTimeout(r, 400));
    }
    counts.sort((a, b) => a - b);
    return counts[Math.floor(counts.length / 2)];
  })();
}

const original = await getConfig();
const pageIdx = 0;
const page = original.pages[pageIdx];
const labels = page.labels ?? [];
if (!labels.length) {
  console.log('FAIL: page 0 has no headings to test with');
  process.exit(1);
}

/* The heading we grow, and its original verbatim so the restore is exact. */
const idx = 0;
const backup = JSON.parse(JSON.stringify(labels[idx]));
console.log(`heading[0]: "${backup.text}" at ${backup.x},${backup.y} font ${backup.font}`);

/* A W-I-D-E string at a large face: proves BOTH halves of the report at once — that the height is
 * not cut at 40 px and that a heading past the old 500 px width keeps its tail. */
const BIG = 96;
const WIDE_TEXT = 'HALLWAY TEMPERATURE NOW';

try {
  const doc = await getConfig();
  doc.pages[pageIdx].labels[idx] = {
    ...doc.pages[pageIdx].labels[idx],
    text: WIDE_TEXT,
    font: BIG,
    x: 20,
    y: 20,
  };
  await putConfig(doc);
  console.log('pushed the large heading; waiting out the refresh');
  await new Promise((r) => setTimeout(r, 9000));

  /* The old box was 500 wide x 40 tall from the label origin (20,20). So the region BELOW row
   * 20+40=60 and the region PAST x=20+500=520 are exactly the ink the old code would have thrown
   * away. A 96 px face has line height ~118, so it must reach well below row 60. */
  const below = await glassInk([20, 60, 900, 180]);
  const tail = await glassInk([520, 20, 920, 180]);

  writeFileSync('/tmp/heading-clip-result.json',
    JSON.stringify({ BIG, WIDE_TEXT, belowOldCut: below, pastOldRight: tail }, null, 2));

  if (cam) {
    check('a 96 px heading draws ink BELOW the old 40 px cut', below > 200, `ink=${below}`);
    check('a wide heading keeps ink PAST the old 500 px right edge', tail > 200, `ink=${tail}`);
  } else {
    console.log('(no camera given — geometry pushed but not photographed)');
  }
} finally {
  const restore = await getConfig();
  restore.pages[pageIdx].labels[idx] = backup;
  await putConfig(restore);
  const after = await getConfig();
  const same = JSON.stringify(after.pages[pageIdx].labels[idx]) === JSON.stringify(backup);
  check('the original heading is restored', same, JSON.stringify(after.pages[pageIdx].labels[idx]));
}

console.log(fail.length ? `\nFAILED: ${fail.join(', ')}` : '\nall checks passed');
process.exit(fail.length ? 1 : 0);
