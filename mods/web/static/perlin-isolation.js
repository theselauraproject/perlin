(function () {
  if (window.crossOriginIsolated) return;
  if (!window.isSecureContext || !('serviceWorker' in navigator)) {
    window.__perlinNoIsolation = 'This page needs a secure context (https or localhost) so it can become cross-origin isolated.';
    return;
  }
  var reloaded = false;
  try { reloaded = sessionStorage.getItem('perlin-isolation-reload') === '1'; } catch (e) {}
  navigator.serviceWorker.register('coi-sw.js').then(function (registration) {
    function reloadOnce() {
      if (reloaded) {
        window.__perlinNoIsolation = 'Could not enable cross-origin isolation (service worker did not take control).';
        return;
      }
      try { sessionStorage.setItem('perlin-isolation-reload', '1'); } catch (e) {}
      location.reload();
    }
    if (registration.active && !navigator.serviceWorker.controller) {
      reloadOnce();
      return;
    }
    if (!registration.active) {
      var worker = registration.installing || registration.waiting;
      if (worker) {
        worker.addEventListener('statechange', function () {
          if (worker.state === 'activated') reloadOnce();
        });
      }
    }
  }).catch(function (e) {
    window.__perlinNoIsolation = 'Service worker registration failed: ' + e;
  });
})();
