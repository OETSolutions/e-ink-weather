#!/usr/bin/env node
/**
 * Push the shipped layout — artwork AND config — to a device on the LAN.
 *
 * WHY THIS EXISTS: verifying "does page 2 show page 2's labels" needs a device that actually has
 * the artwork. The editor does this from a browser, but there is no browser on the bench, and a
 * hand-rolled curl harness would be a SECOND implementation of the encoder — which is exactly the
 * kind of divergence the cross-language tests exist to prevent. So this calls the app's real
 * modules: the same encodeArtwork()/uploadArtwork() the editor uses, and the same config document
 * the editor saves.
 *
 * ORDER MATTERS: artwork first, then the config. The device renders on the config PUT, so the
 * other order would draw a frame with the new value boxes over the old picture.
 *
 * Usage:  node scripts/push-to-device.mjs http://192.168.2.34
 *         (from code/webapp/)
 */

import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webapp = resolve(here, '..');

const base = process.argv[2];
if (!base) {
  console.error('usage: node scripts/push-to-device.mjs http://<device-ip>');
  process.exit(2);
}

/* Run through tsx so the app's TypeScript modules load directly. The program builds each page's
 * static layer with buildStaticLayer + artworkForPage — the same pair main.ts uses — encodes the
 * set, uploads it, then PUTs the config. */
const program = `
import { encodeArtwork, uploadArtwork } from './src/transfer/artwork.ts';
import { artworkForPage, PAGE_ARTWORK, defaultLayout } from './src/presets/default-layout.ts';
import { buildStaticLayer } from './src/canvas/render.ts';

const BASE = ${JSON.stringify(base)};

async function main() {
  const doc = defaultLayout();

  const layers = doc.pages.map((p, i) => {
    const art = artworkForPage(i);
    /* RULES COME FROM THE PAGE, falling back to the art table — the same rule buildPageLayer()
     * in main.ts applies. Using the art table alone would push the ORIGINAL line positions and
     * silently undo a divider the user had moved, which is the divergence this script exists to
     * avoid. */
    const rules = p.rules ?? art.rules;
    return buildStaticLayer(
      art.labels.map((l) => ({ x: l.x, y: l.y, text: l.text, font: l.font })),
      rules.map((r) => ({ y: r.y, thickness: r.thickness, inset: r.inset })),
    ).data;
  });

  const { blob, crc, pageCount } = await encodeArtwork(layers);
  console.log('encoded ' + pageCount + ' page layers, ' + blob.length + ' bytes, crc ' + crc);

  const up = await uploadArtwork(layers, { baseUrl: BASE });
  console.log('artwork upload: ' + JSON.stringify(up));
  if (!up.ok) process.exit(1);

  const r = await fetch(BASE + '/api/config', {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(doc),
  });
  const body = await r.text().catch(() => '');
  console.log('config PUT: HTTP ' + r.status + ' ' + body);
  if (!r.ok) process.exit(1);
}
main().catch((e) => { console.error(e); process.exit(1); });
`;

try {
  const out = execFileSync('npx', ['tsx', '-e', program], {
    cwd: webapp,
    encoding: 'utf8',
    maxBuffer: 64 * 1024 * 1024,
    env: { ...process.env, NODE_OPTIONS: '' },
  });
  process.stdout.write(out);
} catch (e) {
  if (e.stdout) process.stdout.write(e.stdout);
  if (e.stderr) process.stderr.write(e.stderr);
  process.exit(1);
}
