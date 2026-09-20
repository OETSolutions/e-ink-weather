#!/usr/bin/env node
/**
 * Build the web app for the ESP32 and copy it into the firmware component.
 *
 * WHY A SCRIPT AND NOT JUST `vite build`: three things have to happen that Vite does not do,
 * and each of them fails in a way that is painful to debug on a device.
 *
 *   1. GZIP. The bundle is ~214 KB raw and ~65 KB gzipped. The device has ~460 KB of OTA
 *      headroom shared with the firmware, so shipping the raw files would take a third of it
 *      for no benefit — and every request would send 3x the bytes over wifi. Every text asset
 *      is stored gzipped and marked with Content-Encoding.
 *
 *   2. A MANIFEST. The firmware needs to know each file's content type and whether a gzipped
 *      variant exists. Deriving that at RUN time on the device would mean a filesystem and a
 *      lot of code; generating it here means the device just looks up a table.
 *
 *   3. A SIZE BUDGET, ENFORCED. The app must not silently outgrow the device. The budget is
 *      checked against the GZIPPED total (what actually ships) and FAILS the build, so the
 *      problem appears here rather than as a link error or a half-served UI later.
 *
 * It is safe to re-run: the destination is cleared first, so a deleted asset cannot linger and
 * be served from flash forever.
 *
 * Usage:  node scripts/build-for-device.mjs
 */

import { execFileSync } from 'node:child_process';
import {
  existsSync, mkdirSync, readdirSync, readFileSync, rmSync, statSync, writeFileSync,
} from 'node:fs';
import { gzipSync } from 'node:zlib';
import { dirname, join, posix, relative, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const webapp = resolve(here, '..');
const firmware = resolve(webapp, '..', 'firmware');
const distDir = join(webapp, 'dist-device');
const outDir = join(firmware, 'components', 'webui', 'www');

/**
 * The gzipped budget, in bytes.
 *
 * 160 KiB against ~460 KB of measured headroom. Deliberately not the whole headroom: the
 * firmware grows too, and a budget that consumed everything would mean the NEXT firmware
 * feature broke the web app. Failing here is cheap; discovering it during a flash is not.
 */
const BUDGET_BYTES = 160 * 1024;

const CONTENT_TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json',
  '.png': 'image/png',
  '.svg': 'image/svg+xml',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
};

/** Files worth gzipping. Binary formats are already compressed, so gzipping them only costs
 *  build time and a second copy in flash. */
const COMPRESSIBLE = new Set(['.html', '.js', '.css', '.json', '.svg']);

function extOf(p) {
  const i = p.lastIndexOf('.');
  return i < 0 ? '' : p.slice(i).toLowerCase();
}

function walk(dir) {
  const out = [];
  for (const e of readdirSync(dir, { withFileTypes: true })) {
    const full = join(dir, e.name);
    if (e.isDirectory()) out.push(...walk(full));
    else out.push(full);
  }
  return out;
}

/**
 * The linker symbol base for an embedded file.
 *
 * THIS MUST MATCH EXACTLY WHAT IDF GENERATES, and getting it wrong is a link error that names a
 * symbol nobody wrote. EMBED_FILES takes a FILE NAME and defines `_binary_<name>_start` and
 * `_binary_<name>_end`, where every character that is not alphanumeric becomes an underscore.
 * So the base is the embedded file name sanitised — NOT the URL, and not something with an
 * extra prefix, both of which produce a symbol that does not exist.
 */
function symbolFor(embeddedName) {
  return embeddedName.replace(/[^A-Za-z0-9]/g, '_');
}

function main() {
  console.log('building the web app for the device…');
  execFileSync('npx', ['vite', 'build', '--mode', 'device'], {
    cwd: webapp,
    stdio: 'inherit',
  });

  if (!existsSync(distDir)) {
    console.error(`build produced no ${distDir}`);
    process.exit(1);
  }

  /* CLEAR FIRST. A stale file left behind would be embedded and served forever — the app
   * would appear to keep an old asset no matter what was rebuilt. */
  rmSync(outDir, { recursive: true, force: true });
  mkdirSync(outDir, { recursive: true });

  const files = walk(distDir);
  const entries = [];
  let gzippedTotal = 0;

  for (const f of files) {
    const rel = relative(distDir, f).split(/[\\/]/).join(posix.sep);
    const ext = extOf(rel);
    const raw = readFileSync(f);
    const type = CONTENT_TYPES[ext] ?? 'application/octet-stream';

    /* FLATTEN the path for the embedded file name: EMBED_FILES takes a list of files and turns
     * each NAME into a symbol, and a nested path would not survive that. The manifest keeps the
     * URL, which is what matters. */
    /* The embedded file name, and BOTH properties below are load-bearing:
     *
     *   - a PREFIX, because EMBED_FILES turns the name into a C identifier `_binary_<name>_start`
     *     and a name starting with a digit is not one the compiler can name;
     *   - NO DOTS. With a dot in the name (e.g. `..._js.gz`) PlatformIO's library dependency
     *     finder treats the generated embed stub as a source file to build and then looks for it
     *     at a doubled path, failing with "Source .pio/build/.../x.gz.S not found". Sanitising
     *     every character to [A-Za-z0-9_] avoids the whole class. The URL keeps its dots, which
     *     is what the browser needs; this name is only an internal symbol holder. */
    const embeddedName = 'www_' + rel.replace(/[^A-Za-z0-9]/g, '_');

    let stored = raw;
    let gzipped = false;
    if (COMPRESSIBLE.has(ext)) {
      const gz = gzipSync(raw, { level: 9 });
      /* Only keep the gzipped copy when it actually helps. A tiny file can grow under gzip, and
       * storing the larger copy would be a small self-inflicted wound. */
      if (gz.length < raw.length) {
        stored = gz;
        gzipped = true;
      }
    }

    writeFileSync(join(outDir, embeddedName), stored);
    gzippedTotal += stored.length;
    entries.push({ url: '/' + rel, file: embeddedName, symbol: symbolFor(embeddedName), type, gzipped });
    console.log(`  ${rel.padEnd(28)} ${String(raw.length).padStart(7)} raw ` +
                `${String(stored.length).padStart(7)} shipped${gzipped ? ' (gzip)' : ''}  ${type}`);
  }

  /* A GENERATED C SOURCE, not EMBED_FILES.
   *
   * WHY NOT EMBED_FILES: IDF supports it, but PlatformIO's library dependency finder treats the
   * generated `.S` stubs as source files to compile and resolves them at a DOUBLED path
   * ("Source .pio/build/esp32dev/.pio/build/esp32dev/www_x.S not found"). There is no
   * arrangement of EMBED_FILES that survives that here. Emitting the arrays as a plain C file
   * puts the whole mechanism under our control, removes the asm-label indirection, and means a
   * missing asset is a compile error rather than a link error naming a symbol nobody wrote.
   *
   * The arrays are `const`, so they land in flash (rodata). Nothing is copied into RAM. */
  const c = [
    '/* GENERATED by webapp/scripts/build-for-device.mjs — DO NOT EDIT.',
    ' *',
    ' * The built web app, as byte arrays in flash. Regenerate with: npm run build:device',
    ' */',
    '#include "webui_assets.h"',
    '',
  ];

  entries.forEach((e, i) => {
    const bytes = readFileSync(join(outDir, e.file));
    c.push(`/* ${e.url} — ${bytes.length} bytes${e.gzipped ? ', gzip' : ''} */`);
    c.push(`const uint8_t ${e.symbol}[${bytes.length}] = {`);
    for (let o = 0; o < bytes.length; o += 20) {
      c.push('  ' + [...bytes.subarray(o, o + 20)].map((b) => '0x' + b.toString(16).padStart(2, '0')).join(',') + ',');
    }
    c.push('};');
    c.push('');
  });

  c.push('const webui_asset_t WEBUI_ASSETS[WEBUI_ASSET_COUNT] = {');
  entries.forEach((e) => {
    c.push(`    { "${e.url}", ${e.symbol}, sizeof(${e.symbol}), "${e.type}", ${e.gzipped ? 1 : 0} },`);
  });
  c.push('};');
  c.push('');
  writeFileSync(join(outDir, 'webui_assets.c'), c.join('\n'));

  const hdr = [
    '/* GENERATED by webapp/scripts/build-for-device.mjs — DO NOT EDIT. */',
    '#pragma once',
    '#include <stdint.h>',
    '',
    'typedef struct {',
    '    const char *url;',
    '    const uint8_t *data;',
    '    unsigned len;',
    '    const char *content_type;',
    '    int gzipped;',
    '} webui_asset_t;',
    '',
    `#define WEBUI_ASSET_COUNT ${entries.length}`,
    'extern const webui_asset_t WEBUI_ASSETS[WEBUI_ASSET_COUNT];',
    '',
    '/* The app shell, for the site root and the SPA fallback. A path with no extension is a',
    ' * route, not a file, and must serve this. */',
    '#define WEBUI_INDEX_URL "/index.html"',
    '',
  ].join('\n');
  writeFileSync(join(outDir, 'webui_assets.h'), hdr);

  const kib = (gzippedTotal / 1024).toFixed(1);
  const budgetKib = (BUDGET_BYTES / 1024).toFixed(0);
  console.log(`\nembedded total: ${gzippedTotal} bytes (${kib} KiB) — budget ${BUDGET_BYTES} (${budgetKib} KiB)`);

  if (gzippedTotal > BUDGET_BYTES) {
    console.error(
      `\nFAIL: the web app is ${kib} KiB but the budget is ${budgetKib} KiB.\n` +
      'It shares the OTA slot with the firmware, so growing past this would leave no room for\n' +
      'the firmware itself. Trim the bundle, or raise BUDGET_BYTES deliberately and check the\n' +
      'remaining headroom with: pio run -e esp32dev',
    );
    process.exit(1);
  }
  console.log('within budget.');
}

main();
