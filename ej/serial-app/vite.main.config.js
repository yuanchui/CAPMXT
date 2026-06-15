import { defineConfig } from 'vite'
import { resolve } from 'path'

export default defineConfig({
  build: {
    outDir: 'dist/main',
    lib: {
      entry: resolve(__dirname, 'src/main/index.js'), // 或 index.ts
      formats: ['cjs'],
      fileName: () => 'index.js'
    },
    rollupOptions: {
      external: ['electron', 'serialport', '@serialport/bindings-cpp']
    },
    emptyOutDir: true
  }
})
