#!/usr/bin/env node
/**
 * Force a specific page to be the one the device rotates to, for bench verification.
 *
 * WHY: layout_page_at() picks the page from UPTIME modulo the sum of the pages' refreshSeconds,
 * so with the shipped 900 s intervals a rotation takes 15 minutes to observe. This rewrites the
 * page intervals so the chosen page becomes active within seconds, without touching the artwork
 * or any other part of the layout — which is what makes it a valid test of whether rotation
 * draws the RIGHT page's background.
 *
 * Usage:  node scripts/set-active-page.mjs http://192.168.2.34 1
 *         (from code/webapp/)  — makes page 1 active ~20 s from boot.
 */

import { execFileSync } from 'node:child_process';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const webapp = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const base = process.argv[2];
const want = Number(process.argv[3]);
if (!base || !Number.isInteger(want) || want < 0) {
  console.error('usage: node scripts/set-active-page.mjs http://<ip> <pageIndex>');
  process.exit(2);
}

const program = `
import { defaultLayout } from './src/presets/default-layout.ts';

async function main() {
  const doc = defaultLayout();
  const WANT = ${want};
  /* layout_page_at() accumulates refreshSeconds IN ORDER and returns the first page whose
   * cumulative total passes (uptime % total). To land on page WANT, every page BEFORE it needs a
   * small window so control runs past them immediately, and WANT itself needs a large one so the
   * test has time to observe it.
   *
   * THE "SMALL" WINDOW IS 60 s, NOT 1 s: the device's parser runs every interval through
   * clamp_interval(), which replaces anything below LAYOUT_MIN_INTERVAL_SECONDS (30) with the
   * document's updateSeconds. A 1 s window is therefore silently rewritten to 900 s and the pages
   * before the target swallow the whole test. 60 s is comfortably above the floor and still short
   * enough that the target is reached within a minute of boot. */
  doc.pages.forEach((p, i) => { p.refreshSeconds = (i < WANT) ? 60 : 100000; });

  const r = await fetch(${JSON.stringify(base)} + '/api/config', {
    method: 'PUT', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(doc),
  });
  console.log('config PUT: HTTP ' + r.status + ' ' + (await r.text().catch(() => '')));
  if (!r.ok) process.exit(1);
}
main().catch((e) => { console.error(e); process.exit(1); });
`;

try {
  process.stdout.write(execFileSync('npx', ['tsx', '-e', program], {
    cwd: webapp, encoding: 'utf8', env: { ...process.env, NODE_OPTIONS: '' },
  }));
} catch (e) {
  if (e.stdout) process.stdout.write(e.stdout);
  if (e.stderr) process.stderr.write(e.stderr);
  process.exit(1);
}
