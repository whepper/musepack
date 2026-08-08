import { defineConfig } from 'vitest/config';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// The MusicPack web client. Served in production by `musicpack-server`
// `--static-dir` (COOP/COEP come from the server). In development Vite
// proxies /api to the running server and emits the same cross-origin
// isolation headers so the SharedArrayBuffer demand reader works.
export default defineConfig({
  root: 'app',
  plugins: [svelte()],
  base: '/',
  resolve: {
    alias: {
      $lib: new URL('./app/src/lib', import.meta.url).pathname,
    },
  },
  server: {
    port: 5173,
    proxy: {
      '/api': {
        target: 'http://127.0.0.1:8080',
        changeOrigin: false,
      },
    },
    headers: {
      'Cross-Origin-Opener-Policy': 'same-origin',
      'Cross-Origin-Embedder-Policy': 'require-corp',
    },
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    target: 'es2022',
    sourcemap: false,
    // Emit worklet/worker entries as real files (same-origin, CSP-clean);
    // never inline as data: URIs.
    assetsInlineLimit: 0,
  },
  test: {
    environment: 'node',
    include: ['../tests/unit/**/*.test.ts'],
  },
});
