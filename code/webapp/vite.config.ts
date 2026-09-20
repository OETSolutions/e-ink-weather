import { defineConfig } from 'vite';

/* The device to proxy /api to during `npm run dev`. Override per-machine:
 *   EINK_DEVICE=http://192.168.2.34 npm run dev
 * A LAN address is inherently machine-specific, so hardcoding one would be wrong for everyone
 * else — but the app is served over plain HTTP from the device, so the default must be an
 * http:// LAN address, not localhost. */
const DEVICE = process.env.EINK_DEVICE ?? 'http://192.168.2.34';

export default defineConfig({
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
  build: {
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
  },
});
