import { defineConfig } from 'vite';
import { resolve } from 'path';

export default defineConfig({
  root: './src/renderer',
  base: './',
  publicDir: 'public',
  build: {
    outDir: '../../dist/renderer',
    emptyOutDir: true,
    chunkSizeWarningLimit: 1000,
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'src/renderer/index.html'),
        splash: resolve(__dirname, 'src/renderer/splash.html'),
        xcfgViewer: resolve(__dirname, 'src/renderer/xcfg-viewer/index.html'),
        xcfgCompare: resolve(__dirname, 'src/renderer/xcfg-viewer/compare_xcfg.html'),
        xcfgTouchMatrix: resolve(__dirname, 'src/renderer/xcfg-viewer/kimi_touch_matrix.html'),
      },
      output: {
        manualChunks(id) {
          if (!id.includes('node_modules')) return;
          if (id.includes('/three/')) return 'vendor-three';
          if (id.includes('/chart.js/')) return 'vendor-chart';
          return 'vendor';
        },
      },
    },
  },
  server: {
    port: 5173,
  },
  resolve: {
    alias: {
      '@': resolve(__dirname, 'src/renderer'),
    },
  },
});
