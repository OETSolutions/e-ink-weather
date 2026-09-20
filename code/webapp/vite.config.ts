import { defineConfig } from 'vite';

/* The device to proxy /api to during `npm run dev`. Override per-machine:
 *   EINK_DEVICE=http://192.168.2.34 npm run dev
 * A LAN address is inherently machine-specific, so hardcoding one would be wrong for everyone
 * else — but the app is served over plain HTTP from the device, so the default must be an
 * http:// LAN address, not localhost. */
const DEVICE = process.env.EINK_DEVICE ?? 'http://192.168.2.34';

/**
 * The device build: the SAME app, built to be embedded in the ESP32's flash (FR-18).
 *
 * Differences from the browser build, and why each matters:
 *   - a SEPARATE output directory, so a device build never overwrites the one being served by
 *     `vite preview`;
 *   - no sourcemaps — they would double the embedded size and there is no debugger on the
 *     device to read them;
 *   - assets are NOT inlined. Inlining would be one fewer request, but the size then hides
 *     inside app.js where the manifest cannot gzip or cache it separately, and a CSS change
 *     would invalidate the whole bundle.
 *
 * The size is not trusted to this config: scripts/build-for-device.mjs asserts the gzipped
 * total against a budget, because a bundle that outgrows the OTA slot fails at flash time with
 * a far less obvious error.
 */
const deviceBuild = {
  outDir: 'dist-device',
  target: 'es2020',
  minify: 'esbuild' as const,
  sourcemap: false,
  cssMinify: true,
  assetsInlineLimit: 0,
  rollupOptions: {
    output: {
      entryFileNames: 'assets/app.js',
      assetFileNames: 'assets/[name][extname]',
    },
  },
};

const browserBuild = {
  outDir: 'dist',
  // Keep the bundle small: it has to fit in the device's flash (FR-18).
  target: 'es2020',
  assetsInlineLimit: 8192,
  rollupOptions: {
    output: {
      // One JS file and one CSS file keeps the request count low on-device.
      entryFileNames: 'assets/app.js',
      assetFileNames: 'assets/[name][extname]',
    },
  },
};

export default defineConfig(({ mode }) => ({
  // Relative base so the built app works when served from the ESP32 at any path.
  base: './',
  server: {
    /* Without this, `fetch('/api/config')` in dev hits the Vite server and 404s, so the map
     * would always show an empty location — the app would look broken for a reason that has
     * nothing to do with the app. */
    proxy: {
      '/api': { target: DEVICE, changeOrigin: true },
    },
  },
  /* ONE KEY, CHOSEN — not a spread that a later `build` key would overwrite. Spreading a
   * conditional `build` above the unconditional one looks right and does nothing: the later
   * key wins, and the device build silently writes to dist/ instead of dist-device/. */
  build: mode === 'device' ? deviceBuild : browserBuild,
}));
