import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

// Set DEVICE_HOST to a live FermentDial's IP to proxy /api, /settings, /wifi
// etc. during `npm run dev`, e.g. `DEVICE_HOST=192.168.1.50 npm run dev`.
const deviceHost = process.env.DEVICE_HOST;

export default defineConfig({
  base: './',
  plugins: [svelte()],
  server: deviceHost
    ? {
        proxy: {
          '/api': `http://${deviceHost}`,
          '/settings': `http://${deviceHost}`,
          '/wifi': `http://${deviceHost}`,
          '/login': `http://${deviceHost}`,
          '/logout': `http://${deviceHost}`,
          '/metrics': `http://${deviceHost}`,
        },
      }
    : undefined,
  build: {
    outDir: 'dist',
    assetsDir: '',
    cssCodeSplit: false,
    rollupOptions: {
      output: {
        entryFileNames: 'app.js',
        assetFileNames: 'app[extname]',
      },
    },
  },
});
