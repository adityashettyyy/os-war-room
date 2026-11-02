import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// `npm run dev` proxies /api to the C++ server (default port 8080).
export default defineConfig({
  plugins: [react()],
  base: './',
  server: { port: 5173, proxy: { '/api': 'http://127.0.0.1:8080' } },
  build: { outDir: 'dist', sourcemap: false },
});
