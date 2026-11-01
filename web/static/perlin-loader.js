(function () {
  var DB_NAME = 'perlin-web';
  var STATE_STORE = 'state';
  var FILE_STORE = 'files';
  var DATA_DIR = '/perlin';
  var SAMPLES_REPO = DATA_DIR + '/bedrock-samples/repo';
  var MOJANG_MANIFEST = 'https://piston-meta.mojang.com/mc/game/version_manifest_v2.json';
  var SKIP_PERSIST = {
    'bedrock-samples': 1, server_packs: 1, screenshots: 1, shaderpacks: 1, ui_generated: 1,
    ui_defaults: 1, animation_overrides: 1, 'perlin.log': 1, 'blocks.log': 1
  };
  var CONCURRENCY = 64;
  var CDN_BASE = 'https://cdn.jsdelivr.net/gh/';

  var el = function (id) { return document.getElementById(id); };
  var statusEl = el('status');
  var barEl = el('bar');
  var progressEl = el('progress');
  var bootEl = el('boot');
  var runtimeReady = false;
  var starting = false;
  var lastSaved = {};

  function setStatus(text, isError) {
    statusEl.textContent = text;
    statusEl.className = isError ? 'error' : '';
  }

  function setProgress(fraction) {
    progressEl.hidden = fraction < 0;
    if (fraction >= 0) barEl.style.width = Math.max(0, Math.min(1, fraction)) * 100 + '%';
  }

  function openDb() {
    return new Promise(function (resolve, reject) {
      var request = indexedDB.open(DB_NAME, 1);
      request.onupgradeneeded = function () {
        var db = request.result;
        if (!db.objectStoreNames.contains(STATE_STORE)) db.createObjectStore(STATE_STORE);
        if (!db.objectStoreNames.contains(FILE_STORE)) db.createObjectStore(FILE_STORE);
      };
      request.onsuccess = function () { resolve(request.result); };
      request.onerror = function () { reject(request.error); };
    });
  }

  function transactionDone(tx) {
    return new Promise(function (resolve, reject) {
      tx.oncomplete = function () { resolve(); };
      tx.onerror = function () { reject(tx.error); };
      tx.onabort = function () { reject(tx.error); };
    });
  }

  function cursorAll(db, storeName, visit) {
    return new Promise(function (resolve, reject) {
      var request = db.transaction(storeName, 'readonly').objectStore(storeName).openCursor();
      request.onsuccess = function () {
        var cursor = request.result;
        if (!cursor) { resolve(); return; }
        visit(cursor.key, cursor.value);
        cursor.continue();
      };
      request.onerror = function () { reject(request.error); };
    });
  }

  function isDirectory(mode) { return (mode & 61440) === 16384; }

  var knownDirs = {};
  function ensureDir(path) {
    if (knownDirs[path]) return;
    try { Module.FS.mkdirTree(path); } catch (e) {}
    knownDirs[path] = 1;
  }

  function writeFile(path, bytes) {
    var slash = path.lastIndexOf('/');
    if (slash > 0) ensureDir(path.substring(0, slash));
    Module.FS.writeFile(path, bytes);
  }

  async function restoreState(db) {
    await cursorAll(db, STATE_STORE, function (key, value) {
      try {
        writeFile(DATA_DIR + '/' + key, new Uint8Array(value.bytes));
        lastSaved[key] = value.stamp;
      } catch (e) {}
    });
  }

  function walk(dir, relative, out) {
    var names;
    try { names = Module.FS.readdir(dir); } catch (e) { return; }
    names.forEach(function (name) {
      if (name === '.' || name === '..') return;
      if (!relative && SKIP_PERSIST[name]) return;
      if (name === 'client.jar' || name === 'client.jar.part') return;
      var full = dir + '/' + name;
      var rel = relative ? relative + '/' + name : name;
      var stat;
      try { stat = Module.FS.stat(full); } catch (e) { return; }
      if (isDirectory(stat.mode)) walk(full, rel, out);
      else out.push({ rel: rel, full: full, stamp: stat.size + ':' + (+stat.mtime) });
    });
  }

  var saving = false;
  async function saveState() {
    if (!runtimeReady || saving) return;
    saving = true;
    try {
      var files = [];
      walk(DATA_DIR, '', files);
      var changed = files.filter(function (f) { return lastSaved[f.rel] !== f.stamp; });
      if (!changed.length) return;
      var db = await openDb();
      var tx = db.transaction(STATE_STORE, 'readwrite');
      var store = tx.objectStore(STATE_STORE);
      changed.forEach(function (f) {
        try {
          var bytes = Module.FS.readFile(f.full);
          store.put({ bytes: bytes.slice().buffer, stamp: f.stamp }, f.rel);
          lastSaved[f.rel] = f.stamp;
        } catch (e) {}
      });
      await transactionDone(tx);
      db.close();
    } catch (e) {
    } finally {
      saving = false;
    }
  }

  function rawUrl(manifest, path) {
    if (manifest.baseUrl) return manifest.baseUrl + path.split('/').map(encodeURIComponent).join('/');
    return 'https://raw.githubusercontent.com/' + manifest.repository + '/' + manifest.ref + '/' +
      path.split('/').map(encodeURIComponent).join('/');
  }

  function cdnUrl(manifest, path) {
    return CDN_BASE + manifest.repository + '@' + manifest.ref + '/' + path.split('/').map(encodeURIComponent).join('/');
  }

  async function fetchWithRetry(url, fallbackUrl) {
    var lastError;
    for (var attempt = 0; attempt < 4; attempt++) {
      try {
        var response = await fetch(attempt % 2 === 0 || !fallbackUrl ? url : fallbackUrl, { mode: 'cors', credentials: 'omit' });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        return new Uint8Array(await response.arrayBuffer());
      } catch (e) {
        lastError = e;
        await new Promise(function (r) { setTimeout(r, 400 * (attempt + 1)); });
      }
    }
    throw lastError;
  }

  var persistence = Promise.resolve();
  var firstFrame = null;

  async function ensureSamples(db, includeSounds) {
    setStatus('Reading asset list...');
    var manifestResponse = await fetch('assets-manifest.json', { cache: 'no-cache' });
    if (!manifestResponse.ok) throw new Error('assets-manifest.json is missing (HTTP ' + manifestResponse.status + ')');
    var manifest = await manifestResponse.json();
    var wanted = manifest.files.core.slice();
    if (includeSounds) wanted = wanted.concat(manifest.files.sounds);
    var wantedSet = {};
    wanted.forEach(function (p) { wantedSet[p] = 1; });

    var cached = {};
    var cachedCount = 0;
    setStatus('Loading cached assets...');
    await cursorAll(db, FILE_STORE, function (key, value) {
      if (!wantedSet[key] || value.ref !== manifest.ref) return;
      writeFile(SAMPLES_REPO + '/' + key, new Uint8Array(value.bytes));
      cached[key] = 1;
      cachedCount++;
    });

    var missing = wanted.filter(function (p) { return !cached[p]; });
    if (missing.length) {
      var total = missing.length;
      var done = 0;
      var failed = [];
      var pending = [];
      var flushing = Promise.resolve();
      function flush(batch) {
        flushing = flushing.then(async function () {
          var tx = db.transaction(FILE_STORE, 'readwrite');
          var store = tx.objectStore(FILE_STORE);
          batch.forEach(function (item) { store.put({ bytes: item.bytes.slice().buffer, ref: manifest.ref }, item.path); });
          await transactionDone(tx);
        }).catch(function () {});
      }
      var next = 0;
      async function worker() {
        while (next < missing.length) {
          var path = missing[next++];
          try {
            var bytes = await fetchWithRetry(cdnUrl(manifest, path), rawUrl(manifest, path));
            writeFile(SAMPLES_REPO + '/' + path, bytes);
            pending.push({ path: path, bytes: bytes });
            if (pending.length >= 200) { flush(pending); pending = []; }
          } catch (e) {
            failed.push(path);
          }
          done++;
          if (done % 8 === 0 || done === total) {
            setProgress(done / total);
            setStatus('Downloading assets from GitHub: ' + done + ' / ' + total);
          }
        }
      }
      var workers = [];
      for (var i = 0; i < CONCURRENCY; i++) workers.push(worker());
      await Promise.all(workers);
      if (pending.length) flush(pending);
      persistence = flushing;
      if (failed.length) {
        var coreFailed = failed.filter(function (p) { return manifest.files.core.indexOf(p) >= 0; });
        if (coreFailed.length) throw new Error(coreFailed.length + ' required files failed to download (first: ' + coreFailed[0] + '). Reload the page to retry.');
      }
    }
    writeFile(DATA_DIR + '/bedrock-samples/.ready3', new Uint8Array(0));
    setProgress(-1);
  }

  function hasFile(path) {
    try { Module.FS.stat(path); return true; } catch (e) { return false; }
  }

  async function fetchJson(url) {
    var response = await fetch(url, { mode: 'cors', credentials: 'omit' });
    if (!response.ok) throw new Error('HTTP ' + response.status);
    return response.json();
  }

  async function ensureClientAssets() {
    if (hasFile(DATA_DIR + '/assets/.ready2') && hasFile(DATA_DIR + '/fonts/unifont.hex')) return;
    setStatus('Looking up the current Minecraft version...');
    var manifest = await fetchJson(MOJANG_MANIFEST);
    var entry = manifest.versions.filter(function (v) { return v.id === manifest.latest.release; })[0];
    if (!entry) throw new Error('Mojang version manifest has no release entry');
    var version = await fetchJson(entry.url);
    var jarUrl = version.downloads && version.downloads.client && version.downloads.client.url;
    if (!jarUrl) throw new Error('Mojang version metadata has no client download');
    var response = await fetch(jarUrl, { mode: 'cors', credentials: 'omit' });
    if (!response.ok) throw new Error('client download failed (HTTP ' + response.status + ')');
    var total = +response.headers.get('content-length') || 0;
    var chunks = [];
    var received = 0;
    var reader = response.body.getReader();
    for (;;) {
      var part = await reader.read();
      if (part.done) break;
      chunks.push(part.value);
      received += part.value.length;
      setStatus('Downloading game data from Mojang: ' + Math.round(received / 1048576) + ' MB' + (total ? ' / ' + Math.round(total / 1048576) + ' MB' : ''));
    }
    var bytes = new Uint8Array(received);
    var at = 0;
    chunks.forEach(function (c) { bytes.set(c, at); at += c.length; });
    writeFile(DATA_DIR + '/fonts/client.jar', bytes);
  }

  async function fetchSoundsInBackground(db) {
    try {
      var manifest = await (await fetch('assets-manifest.json', { cache: 'no-cache' })).json();
      var queue = manifest.files.sounds.slice();
      var stored = {};
      await cursorAll(db, FILE_STORE, function (key, value) { if (value.ref === manifest.ref) stored[key] = 1; });
      queue = queue.filter(function (p) { return !stored[p]; });
      var next = 0;
      var failed = 0;
      async function worker() {
        while (next < queue.length) {
          var path = queue[next++];
          try {
            var bytes = await fetchWithRetry(cdnUrl(manifest, path), rawUrl(manifest, path));
            var tx = db.transaction(FILE_STORE, 'readwrite');
            tx.objectStore(FILE_STORE).put({ bytes: bytes.slice().buffer, ref: manifest.ref }, path);
            await transactionDone(tx);
          } catch (e) { failed++; }
        }
      }
      var workers = [];
      for (var i = 0; i < 8; i++) workers.push(worker());
      await Promise.all(workers);
      if (!failed) {
        var tx2 = db.transaction(STATE_STORE, 'readwrite');
        tx2.objectStore(STATE_STORE).put({ bytes: new Uint8Array([1]).buffer, stamp: 'sounds' }, '.sounds-cached');
        await transactionDone(tx2);
      }
    } catch (e) {}
  }

  async function soundsCached(db) {
    return new Promise(function (resolve) {
      var request = db.transaction(STATE_STORE, 'readonly').objectStore(STATE_STORE).get('.sounds-cached');
      request.onsuccess = function () { resolve(!!request.result); };
      request.onerror = function () { resolve(false); };
    });
  }

  // the engine cannot wait on a promise from inside main(), so the device is created here and handed over
  async function acquireWebGpu() {
    if (!navigator.gpu) throw new Error('This browser has no WebGPU. Use a current Chrome, Edge or Safari.');
    var adapter = await navigator.gpu.requestAdapter({ powerPreference: 'high-performance' });
    if (!adapter) throw new Error('No WebGPU adapter is available on this device.');
    var wanted = ['depth-clip-control', 'depth32float-stencil8', 'texture-compression-bc', 'indirect-first-instance',
                  'rg11b10ufloat-renderable', 'float32-filterable', 'float32-blendable'];
    var features = wanted.filter(function (f) { return adapter.features.has(f); });
    var limits = {};
    for (var key in adapter.limits) limits[key] = adapter.limits[key];
    var device = await adapter.requestDevice({ requiredFeatures: features, requiredLimits: limits });
    device.lost.then(function (info) { console.warn('WebGPU device lost: ' + info.message); });
    Module.preinitializedWebGPUDevice = device;
  }

  // Microsoft, Xbox and Minecraft services send no CORS headers, so the page reaches them through a relay
  // (tools/web/auth-relay): ?relay=<url>, else perlin-config.json's "authRelay", else serve.py's own /__relay-https
  async function authRelay() {
    var fromUrl = new URLSearchParams(location.search).get('relay');
    if (fromUrl) return fromUrl;
    try {
      var r = await fetch('perlin-config.json', { cache: 'no-store' });
      if (r.ok) {
        var cfg = await r.json();
        if (cfg && cfg.authRelay) return String(cfg.authRelay);
      }
    } catch (e) {}
    try {
      var probe = await fetch('__relay-https/ping', { cache: 'no-store' });
      if (probe.ok && (await probe.text()) === 'perlin-relay') return new URL('__relay-https', location.href).href;
    } catch (e) {}
    return '';
  }

  async function start() {
    if (starting || !runtimeReady) return;
    starting = true;
    try {
      var db = await openDb();
      setStatus('Restoring saved data...');
      await restoreState(db);
      var withSounds = await soundsCached(db);
      await Promise.all([
        ensureSamples(db, withSounds),
        ensureClientAssets().catch(function (e) {
          console.warn('client assets unavailable: ' + e);
        })
      ]);
      persistence.then(function () {});
      setStatus('Starting...');
      setProgress(-1);
      await acquireWebGpu();
      await new Promise(function (resolve) { setTimeout(resolve, 30); });
      firstFrame = new Promise(function (resolve) { Module.onFirstFrame = resolve; });
      firstFrame.then(function () {
        bootEl.hidden = true;
        if (!withSounds) fetchSoundsInBackground(db);
      });
      setInterval(saveState, 5000);
      window.addEventListener('pagehide', saveState);
      document.addEventListener('visibilitychange', function () { if (document.hidden) saveState(); });
      document.getElementById('canvas').focus();
      Module.perlinAuthRelay = await authRelay();
      Module.callMain(['--nethernet']);
    } catch (e) {
      setProgress(-1);
      setStatus(String(e && e.message ? e.message : e) + '\nReload the page to retry.', true);
      bootEl.hidden = false;
      starting = false;
    }
  }

  window.Module = {
    canvas: el('canvas'),
    noInitialRun: true,
    print: function (text) { console.log(text); },
    printErr: function (text) { console.warn(text); },
    onAbort: function (what) {
      bootEl.hidden = false;
      setStatus('The engine crashed: ' + what + '\nReload the page to restart.', true);
    },
    onRuntimeInitialized: function () {
      runtimeReady = true;
      begin();
    }
  };

  function begin() {
    if (window.__perlinNoIsolation) {
      setStatus(window.__perlinNoIsolation, true);
      return;
    }
    if (!window.crossOriginIsolated) {
      setStatus('Enabling cross-origin isolation...');
      return;
    }
    start();
  }

  if (window.__perlinNoIsolation) setStatus(window.__perlinNoIsolation, true);
  else setStatus('Loading engine...');
})();
