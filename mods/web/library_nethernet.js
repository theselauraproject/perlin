mergeInto(LibraryManager.library, {
  perlin_nn_js_begin__proxy: 'sync',
  perlin_nn_js_begin__sig: 'vp',
  perlin_nn_js_begin: function (iceServersPtr) {
    var servers = iceServersPtr ? UTF8ToString(iceServersPtr) : '';
    var previous = globalThis.__perlinNN;
    if (previous) {
      try { previous.pc.close(); } catch (e) {}
    }
    var nn = { state: 1, error: '', offer: '', pc: null, reliable: null, unreliable: null };
    globalThis.__perlinNN = nn;

    function fail(message) {
      if (globalThis.__perlinNN !== nn) return;
      if (!nn.error) nn.error = String(message);
      nn.state = 4;
    }

    function deliver(isReliable, data) {
      if (globalThis.__perlinNN !== nn) return;
      var bytes = new Uint8Array(data);
      if (!bytes.length) return;
      var ptr = Module['_malloc'](bytes.length);
      HEAPU8.set(bytes, ptr);
      Module['_perlin_nn_on_message'](ptr, bytes.length, isReliable ? 1 : 0);
      Module['_free'](ptr);
    }

    try {
      var urls = servers.split(',').map(function (s) { return s.trim(); }).filter(Boolean);
      var pc = new RTCPeerConnection({ iceServers: urls.length ? [{ urls: urls }] : [] });
      nn.pc = pc;
      nn.reliable = pc.createDataChannel('ReliableDataChannel');
      nn.unreliable = pc.createDataChannel('UnreliableDataChannel', { ordered: false, maxRetransmits: 0 });
      nn.reliable.binaryType = 'arraybuffer';
      nn.unreliable.binaryType = 'arraybuffer';
      nn.reliable.onopen = function () {
        if (globalThis.__perlinNN === nn && nn.state !== 4) nn.state = 3;
      };
      nn.reliable.onclose = function () {
        if (globalThis.__perlinNN !== nn) return;
        var wasOpen = nn.state === 3;
        fail('data channel closed');
        if (wasOpen && Module['_perlin_nn_on_closed']) Module['_perlin_nn_on_closed']();
      };
      nn.reliable.onmessage = function (e) { deliver(true, e.data); };
      nn.unreliable.onmessage = function (e) { deliver(false, e.data); };
      pc.onconnectionstatechange = function () {
        if (pc.connectionState === 'failed') {
          var wasOpen = nn.state === 3;
          fail('peer connection failed');
          if (wasOpen && Module['_perlin_nn_on_closed']) Module['_perlin_nn_on_closed']();
        }
      };

      var finished = false;
      function publishOffer() {
        if (finished || globalThis.__perlinNN !== nn) return;
        finished = true;
        var desc = pc.localDescription;
        if (!desc || !desc.sdp) { fail('no local description'); return; }
        nn.offer = desc.sdp;
        if (nn.state === 1) nn.state = 2;
      }
      pc.onicegatheringstatechange = function () {
        if (pc.iceGatheringState === 'complete') publishOffer();
      };
      pc.createOffer().then(function (offer) {
        return pc.setLocalDescription(offer);
      }).then(function () {
        if (pc.iceGatheringState === 'complete') publishOffer();
        else setTimeout(publishOffer, 10000);
      }).catch(function (e) { fail('createOffer: ' + e); });
    } catch (e) {
      fail(e);
    }
  },

  perlin_nn_js_state__proxy: 'sync',
  perlin_nn_js_state__sig: 'i',
  perlin_nn_js_state: function () {
    var nn = globalThis.__perlinNN;
    return nn ? nn.state : 0;
  },

  perlin_nn_js_offer__proxy: 'sync',
  perlin_nn_js_offer__sig: 'ipi',
  perlin_nn_js_offer: function (out, capacity) {
    var nn = globalThis.__perlinNN;
    if (!nn) return 0;
    return stringToUTF8(nn.offer, out, capacity);
  },

  perlin_nn_js_error__proxy: 'sync',
  perlin_nn_js_error__sig: 'ipi',
  perlin_nn_js_error: function (out, capacity) {
    var nn = globalThis.__perlinNN;
    if (!nn) return 0;
    return stringToUTF8(nn.error, out, capacity);
  },

  perlin_nn_js_set_answer__proxy: 'sync',
  perlin_nn_js_set_answer__sig: 'vp',
  perlin_nn_js_set_answer: function (sdpPtr) {
    var nn = globalThis.__perlinNN;
    if (!nn || !nn.pc) return;
    var sdp = UTF8ToString(sdpPtr);
    nn.pc.setRemoteDescription({ type: 'answer', sdp: sdp }).then(function () {
      if (globalThis.__perlinNN === nn && nn.state === 2) nn.state = 5;
    }).catch(function (e) {
      if (globalThis.__perlinNN === nn) { nn.error = 'setRemoteDescription: ' + e; nn.state = 4; }
    });
  },

  perlin_nn_js_send__proxy: 'async',
  perlin_nn_js_send__sig: 'vpii',
  perlin_nn_js_send: function (ptr, size, reliable) {
    var nn = globalThis.__perlinNN;
    var copy = HEAPU8.slice(ptr, ptr + size);
    Module['_free'](ptr);
    if (!nn) return;
    var channel = reliable ? nn.reliable : nn.unreliable;
    if (!channel || channel.readyState !== 'open') return;
    try { channel.send(copy); } catch (e) {}
  },

  perlin_nn_js_close__proxy: 'sync',
  perlin_nn_js_close__sig: 'v',
  perlin_nn_js_close: function () {
    var nn = globalThis.__perlinNN;
    globalThis.__perlinNN = null;
    if (nn && nn.pc) {
      try { nn.pc.close(); } catch (e) {}
    }
  }
});
