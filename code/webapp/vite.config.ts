import { defineConfig } from 'vite';

export default defineConfig({
  // Relative base so the built app works when served from the ESP32 at any path.
  base: './',
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
