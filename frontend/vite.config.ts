import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import path from 'path';

// The SPA is served by the FastAPI app at /channel, alongside the existing /operator UI, so
// every asset URL it emits has to carry that prefix. `outDir` writes straight into the API
// package's static tree: that directory is what the wheel ships and what
// docker-compose.playlist.yml bind-mounts read-only into the container, so a local
// `npm run build` is visible to a running appliance without rebuilding the image.
const BASE = '/channel/';

export default defineConfig({
  base: BASE,
  plugins: [react()],
  resolve: {
    alias: {
      '@': path.resolve(__dirname, './src'),
    },
  },
  build: {
    outDir: path.resolve(__dirname, '../api/src/transcoder_api/static/channel'),
    emptyOutDir: true,
  },
  server: {
    port: 3000,
    proxy: {
      // `npm run dev` serves the SPA itself and forwards the API surface it uses to the
      // appliance. Keep this list in step with services/endpoints.ts.
      '/config': { target: 'http://localhost:8080', changeOrigin: true },
      '/inputs': { target: 'http://localhost:8080', changeOrigin: true },
      '/hls': { target: 'http://localhost:8080', changeOrigin: true },
      '/operator': { target: 'http://localhost:8080', changeOrigin: true },
      '/player': { target: 'http://localhost:8080', changeOrigin: true },
    },
  },
  test: {
    globals: true,
    environment: 'jsdom',
    setupFiles: './tests/setup.ts',
  },
});
