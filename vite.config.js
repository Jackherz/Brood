const { defineConfig } = require('vite');

// Listens on the network so a phone on the same Wi-Fi can open it by IP address. Vite's
// default host check stays on (localhost and IP addresses pass; other names don't), so a
// web page elsewhere can't reach the dev server through DNS rebinding. Add a name to
// BROOD_ALLOWED_HOSTS (comma-separated) if you open it by one, e.g. a .local name.
const extra = (process.env.BROOD_ALLOWED_HOSTS || '').split(',').map((h) => h.trim()).filter(Boolean);

module.exports = defineConfig({
  server: { host: '0.0.0.0', allowedHosts: extra },
  preview: { host: '0.0.0.0', allowedHosts: extra },
});
