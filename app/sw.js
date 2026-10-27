// Offline support for the installed monitor. Network first, so a new
// version is picked up immediately; the cache is only a fallback.
const CACHE = "oncosense-monitor-v2";
const FILES = [
  "./", "index.html", "styles.css", "model.json", "icon.svg", "manifest.webmanifest",
  "js/app.js", "js/charts.js", "js/demo-device.js", "js/model.js", "js/recorder.js",
  "js/serial-device.js", "js/sim.js",
  "assets/band-worn-electronics.jpg",
];

self.addEventListener("install", (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener("activate", (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim()),
  );
});

self.addEventListener("fetch", (e) => {
  if (e.request.method !== "GET" || new URL(e.request.url).origin !== location.origin) return;
  e.respondWith(
    fetch(e.request)
      .then((res) => {
        // Never let an error page or partial response replace a good copy.
        if (res.ok && res.type === "basic" && res.status === 200) {
          const copy = res.clone();
          e.waitUntil(caches.open(CACHE).then((c) => c.put(e.request, copy)).catch(() => {}));
        }
        return res;
      })
      .catch(async () => {
        // The app rewrites ?view=..., so match pages without their query string.
        const cached = await caches.match(e.request, { ignoreSearch: e.request.mode === "navigate" });
        return cached ?? (e.request.mode === "navigate" ? caches.match("index.html") : Response.error());
      }),
  );
});
