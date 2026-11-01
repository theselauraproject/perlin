var CACHE = 'perlin-static-v1';
var NETWORK_ONLY = /assets-manifest\.json$|version\.json$|perlin-config\.json$|__relay|coi-sw\.js$/;

function isolate(response) {
  if (response.status === 0) return response;
  var headers = new Headers(response.headers);
  headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
  headers.set('Cross-Origin-Opener-Policy', 'same-origin');
  headers.set('Cross-Origin-Resource-Policy', 'same-origin');
  return new Response(response.body, { status: response.status, statusText: response.statusText, headers: headers });
}

self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('activate', function (event) {
  event.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k !== CACHE; }).map(function (k) { return caches.delete(k); }));
  }).then(function () { return self.clients.claim(); }));
});
self.addEventListener('fetch', function (event) {
  var request = event.request;
  if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
  var url = new URL(request.url);
  if (url.origin !== self.location.origin) return;
  var cacheable = request.method === 'GET' && !request.headers.has('range') && !NETWORK_ONLY.test(url.pathname);
  if (!cacheable) {
    event.respondWith(fetch(request).then(isolate).catch(function () { return fetch(request); }));
    return;
  }
  event.respondWith(caches.open(CACHE).then(function (cache) {
    var key = url.origin + url.pathname;
    return cache.match(key).then(function (hit) {
      var refresh = fetch(request).then(function (response) {
        if (response.ok && response.status === 200) cache.put(key, response.clone());
        return response;
      });
      if (hit) {
        event.waitUntil(refresh.catch(function () {}));
        return isolate(hit);
      }
      return refresh.then(isolate);
    });
  }).catch(function () { return fetch(request); }));
});
