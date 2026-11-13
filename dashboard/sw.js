// Service worker: caches the app shell + CDN libs so the dashboard opens instantly
// and still loads offline. Live data always comes from MQTT (WebSocket), which is never cached.
// Bump VERSION whenever you change index.html so phones pick up the new version.
const VERSION = "shd-v2";
const SHELL = [
  "./",
  "./index.html",
  "./manifest.webmanifest",
  "./icons/icon-192.png",
  "./icons/icon-512.png",
];
const LIBS = [
  "https://unpkg.com/mqtt@5.10.1/dist/mqtt.min.js",
  "https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js",
];

self.addEventListener("install", e => {
  e.waitUntil(caches.open(VERSION).then(async c => {
    await c.addAll(SHELL);
    // CDN libs are best-effort: a CDN hiccup must not stop the app from installing.
    await Promise.all(LIBS.map(u => c.add(u).catch(err => console.warn("SW: not cached", u, err))));
  }).then(() => self.skipWaiting()));
});

self.addEventListener("activate", e => {
  e.waitUntil(
    caches.keys().then(keys => Promise.all(keys.filter(k => k !== VERSION).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", e => {
  const req = e.request;
  if (req.method !== "GET") return;
  // Pages: network first (get the newest dashboard), fall back to cache when offline.
  if (req.mode === "navigate") {
    e.respondWith(
      fetch(req).then(res => { caches.open(VERSION).then(c => c.put("./index.html", res.clone())); return res; })
        .catch(() => caches.match("./index.html"))
    );
    return;
  }
  // Everything else (libs, icons): cache first.
  e.respondWith(caches.match(req).then(hit => hit || fetch(req)));
});
