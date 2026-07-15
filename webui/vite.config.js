import { defineConfig, loadEnv } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';

/** Serve the Svelte app for /settings and /dashboard in dev (device owns those paths in production). */
function fermentdialSpaFallback() {
  return {
    name: 'fermentdial-spa-fallback',
    configureServer(server) {
      server.middlewares.use((req, _res, next) => {
        if (req.method !== 'GET' && req.method !== 'HEAD') return next();
        const path = (req.url || '').split('?')[0];
        if (path === '/settings' || path === '/dashboard') {
          req.url = '/index.html';
        }
        next();
      });
    },
  };
}

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  const deviceHost = env.DEVICE_HOST?.trim();

  const deviceTarget = deviceHost ? `http://${deviceHost}` : undefined;

  const proxyDevice = deviceTarget
    ? { target: deviceTarget, changeOrigin: true }
    : undefined;

  /** Proxy form POSTs only — GET stays on Vite (or 404s harmlessly). */
  const proxyPost = deviceTarget
    ? {
        target: deviceTarget,
        changeOrigin: true,
        bypass(req) {
          if (req.method === 'POST') return undefined;
          return false;
        },
      }
    : undefined;

  const proxy = deviceTarget
    ? {
        '/api': proxyDevice,
        '/settings/device': proxyDevice,
        '/settings/security': proxyDevice,
        '/settings/influx': proxyDevice,
        '/settings/mqtt': proxyDevice,
        '/settings/brewfather': proxyDevice,
        '/login': proxyDevice,
        '/logout': proxyDevice,
        '/metrics': proxyDevice,
        '/wifi': proxyPost,
        '/firmware': proxyPost,
      }
    : undefined;

  if (deviceHost) {
    console.log(`[fermentdial] Proxying API requests to http://${deviceHost}`);
  } else {
    console.log('[fermentdial] No DEVICE_HOST set — API calls stay on localhost (copy .env.example to .env.local)');
  }

  return {
    base: './',
    plugins: [svelte(), fermentdialSpaFallback()],
    server: proxy
      ? {
          host: true,
          proxy,
        }
      : { host: true },
    build: {
      outDir: 'dist',
      assetsDir: '',
      cssCodeSplit: false,
      // Inline the DejaVu font subsets (~20 KB woff2 each) into app.css —
      // the firmware serves fixed asset routes, so no separate font files.
      assetsInlineLimit: 32768,
      rollupOptions: {
        output: {
          entryFileNames: 'app.js',
          assetFileNames: 'app[extname]',
        },
      },
    },
  };
});