// Minimal service worker: makes the console installable and works offline,
// but stays out of the way of the device API and OTA endpoints.
//
// Network-first for the app shell so an OTA'd PWA shows up immediately;
// /status and /update are never intercepted (always hit the device live).
const CACHE = 'obd1-console-v1';
const SHELL = [
  '/',
  '/index.html',
  '/css/style.css',
  '/js/app.js',
  '/manifest.json',
  '/icons/dashboard.png',
];

self.addEventListener('install', (e) => {
  self.skipWaiting();
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).catch(() => {}));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k)))
    )
  );
  self.clients.claim();
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (e.request.method !== 'GET' ||
      url.pathname.startsWith('/status') ||
      url.pathname.startsWith('/update')) {
    return;  // let the network handle it, uncached
  }
  e.respondWith(
    fetch(e.request)
      .then((r) => {
        const copy = r.clone();
        caches.open(CACHE).then((c) => c.put(e.request, copy)).catch(() => {});
        return r;
      })
      .catch(() => caches.match(e.request))
  );
});
