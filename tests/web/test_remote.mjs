import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const htmlPath = path.resolve(__dirname, '../../components/screen_mirror/remote.html');

// Read the real production remote.html and extract the <script> block
const htmlContent = fs.readFileSync(htmlPath, 'utf8');
const scriptMatch = htmlContent.match(/<script>([\s\S]*?)<\/script>/);
assert.ok(scriptMatch, 'remote.html must contain a <script> block');
const scriptCode = scriptMatch[1];

function createRemoteContext(canvasRect = { left: 0, top: 0, width: 800, height: 480 }, initialW = 800, initialH = 480) {
  let wsSentMessages = [];
  let currentRect = { ...canvasRect };
  let currentTime = 1000;
  let timers = [];
  let timerId = 0;

  const canvasListeners = {};
  const mockCanvas = {
    width: initialW,
    height: initialH,
    style: {},
    addEventListener: (type, fn) => {
      if (!canvasListeners[type]) canvasListeners[type] = [];
      canvasListeners[type].push(fn);
    },
    dispatchEvent: (type, evt = {}) => {
      const list = canvasListeners[type] || [];
      const eventObj = {
        preventDefault: () => {},
        clientX: 0,
        clientY: 0,
        pointerId: 1,
        ...evt,
      };
      for (const fn of list) fn(eventObj);
    },
    setPointerCapture: () => {},
    releasePointerCapture: () => {},
    getBoundingClientRect: () => currentRect,
    getContext: () => ({
      imageSmoothingEnabled: true,
      drawImage: () => {},
      fillRect: () => {},
    }),
  };

  const elements = {
    screenCanvas: mockCanvas,
    container: {
      appendChild: () => {},
      style: {},
      addEventListener: () => {},
      getBoundingClientRect: () => currentRect,
    },
    metricLbl: { textContent: '' },
    lat: { textContent: '' },
    res: { textContent: '' },
    rot: { textContent: '' },
    stat: { textContent: '' },
    mode: { textContent: '' },
    fps: { textContent: '' },
    fpslabel: { textContent: '' },
    screenOverlay: { style: {} },
    banner: { textContent: '' },
  };

  class MockWebSocket {
    static OPEN = 1;
    static CLOSED = 3;
    constructor(url) {
      this.url = url;
      this.readyState = MockWebSocket.OPEN;
      this.binaryType = 'blob';
      this.onopen = null;
      this.onclose = null;
      this.onerror = null;
      this.onmessage = null;
    }
    send(data) {
      wsSentMessages.push(data);
    }
    close() {
      this.readyState = MockWebSocket.CLOSED;
      if (this.onclose) this.onclose();
    }
  }

  const docListeners = {};
  const winListeners = {};
  let lastMockImage = null;

  const sandbox = {
    document: {
      getElementById: (id) => elements[id] || { textContent: '', style: {} },
      addEventListener: (type, fn) => {
        if (!docListeners[type]) docListeners[type] = [];
        docListeners[type].push(fn);
      },
      createElement: () => ({ className: '', style: {}, remove: () => {} }),
      visibilityState: 'visible',
    },
    window: {
      addEventListener: (type, fn) => {
        if (!winListeners[type]) winListeners[type] = [];
        winListeners[type].push(fn);
      },
      location: { protocol: 'http:', host: 'esphome.local' },
      devicePixelRatio: 1,
    },
    location: { protocol: 'http:', host: 'esphome.local' },
    performance: {
      now: () => currentTime,
    },
    WebSocket: MockWebSocket,
    Image: class MockImage {
      constructor() {
        lastMockImage = this;
        this.onload = null;
        this.onerror = null;
        this.src = '';
      }
    },
    Blob: class MockBlob {
      constructor(parts, options) {
        this.parts = parts;
        this.options = options;
      }
    },
    DataView,
    ArrayBuffer,
    Uint8Array,
    Math,
    setTimeout: (fn, delay) => {
      const id = ++timerId;
      timers.push({ id, fn, delay });
      return id;
    },
    clearTimeout: (id) => {
      timers = timers.filter((t) => t.id !== id);
    },
    console,
    URL: {
      createObjectURL: () => 'blob:mock',
      revokeObjectURL: () => {},
    },
  };

  vm.createContext(sandbox);
  // Run the untouched production script code, attaching test inspector accessors
  const harnessHook = `
;
globalThis.__getWs = () => ws;
globalThis.__getRotation = () => rotation;
globalThis.__setRotation = (r) => { rotation = r; };
globalThis.__setHw = (w, h) => { hwWidth = w; hwHeight = h; };
globalThis.__getPendingQueue = () => pendingQueue;
globalThis.__requestKeyframeRecovery = () => { if (typeof requestKeyframeRecovery === 'function') requestKeyframeRecovery(); };
`;
  vm.runInContext(scriptCode + harnessHook, sandbox);

  return {
    sandbox,
    mockCanvas,
    elements,
    dispatchDocEvent: (type, evt = {}) => {
      for (const fn of (docListeners[type] || [])) fn(evt);
    },
    dispatchWinEvent: (type, evt = {}) => {
      for (const fn of (winListeners[type] || [])) fn(evt);
    },
    getSentWsMessages: () => wsSentMessages,
    getLastMockImage: () => lastMockImage,
    setCanvasRect: (rect) => { currentRect = { ...rect }; },
    advanceTime: (ms) => { currentTime += ms; },
    fireTimers: () => {
      const current = [...timers];
      timers = [];
      for (const t of current) t.fn();
    },
  };
}

test('Production remote.html: clientToRaw coordinate math across rotations', () => {
  const { sandbox, mockCanvas, setCanvasRect } = createRemoteContext();
  const clientToRaw = sandbox.clientToRaw;
  assert.equal(typeof clientToRaw, 'function', 'clientToRaw must be defined in production script');

  // Test 1: 0 deg rotation (800x480)
  sandbox.__setRotation(0);
  sandbox.__setHw(800, 480);
  mockCanvas.width = 800;
  mockCanvas.height = 480;
  setCanvasRect({ left: 0, top: 0, width: 800, height: 480 });

  let pt = clientToRaw(0, 0);
  assert.equal(pt.rawX, 0);
  assert.equal(pt.rawY, 0);

  pt = clientToRaw(799, 479);
  assert.equal(pt.rawX, 799);
  assert.equal(pt.rawY, 479);

  // Clamping test
  pt = clientToRaw(-10, 600);
  assert.equal(pt.rawX, 0);
  assert.equal(pt.rawY, 479);

  // Test 2: 90 deg rotation (displayed canvas 480x800, hardware 800x480)
  sandbox.__setRotation(90);
  mockCanvas.width = 480;
  mockCanvas.height = 800;
  setCanvasRect({ left: 0, top: 0, width: 480, height: 800 });

  pt = clientToRaw(0, 0);
  assert.equal(pt.rawX, 799);
  assert.equal(pt.rawY, 0);

  pt = clientToRaw(479, 799);
  assert.equal(pt.rawX, 0);
  assert.equal(pt.rawY, 479);

  // Test 3: 180 deg rotation
  sandbox.__setRotation(180);
  mockCanvas.width = 800;
  mockCanvas.height = 480;
  setCanvasRect({ left: 0, top: 0, width: 800, height: 480 });

  pt = clientToRaw(0, 0);
  assert.equal(pt.rawX, 799);
  assert.equal(pt.rawY, 479);

  pt = clientToRaw(799, 479);
  assert.equal(pt.rawX, 0);
  assert.equal(pt.rawY, 0);

  // Test 4: 270 deg rotation
  sandbox.__setRotation(270);
  mockCanvas.width = 480;
  mockCanvas.height = 800;
  setCanvasRect({ left: 0, top: 0, width: 480, height: 800 });

  pt = clientToRaw(0, 0);
  assert.equal(pt.rawX, 0);
  assert.equal(pt.rawY, 479);

  pt = clientToRaw(479, 799);
  assert.equal(pt.rawX, 799);
  assert.equal(pt.rawY, 0);
});

test('Production remote.html: sendTouch WebSocket protocol formatting', () => {
  const { sandbox, getSentWsMessages } = createRemoteContext();
  const sendTouch = sandbox.sendTouch;
  assert.equal(typeof sendTouch, 'function');

  // Trigger WebSocket handshake
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  assert.ok(ws, 'WebSocket instance must be created');
  ws.onopen();

  const sent = getSentWsMessages();
  assert.equal(sent[0], 'K', 'Should request keyframe on open');

  sendTouch(100, 200, 'tap');
  assert.equal(sent[1], 'T 100 200');

  sendTouch(50, 60, 'down');
  assert.equal(sent[2], 'D 50 60');

  sendTouch(55, 65, 'move');
  assert.equal(sent[3], 'M 55 65');

  sendTouch(55, 65, 'up');
  assert.equal(sent[4], 'U 55 65');
});

test('Production remote.html: WebSocket message parsing and bounded queue backpressure', () => {
  const { sandbox } = createRemoteContext();
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  ws.onopen();

  // Helper to create valid 10-byte binary message with dummy payload
  function createPacket(magic, rotCode, isKeyframe, x, y, w, h, payloadSize = 20) {
    const buf = new ArrayBuffer(10 + payloadSize);
    const view = new DataView(buf);
    view.setUint8(0, magic);
    view.setUint8(1, (rotCode << 2) | (isKeyframe ? 1 : 0));
    view.setUint16(2, x);
    view.setUint16(4, y);
    view.setUint16(6, w);
    view.setUint16(8, h);
    return buf;
  }

  // Send 2 Delta packets
  const pkt1 = createPacket(0x44, 0, false, 10, 10, 32, 32);
  const pkt2 = createPacket(0x44, 0, false, 50, 50, 32, 32);
  ws.onmessage({ data: pkt1 });
  ws.onmessage({ data: pkt2 });

  // Send 3rd Delta packet -> Backpressure MAX_PENDING = 2 drops oldest
  const pkt3 = createPacket(0x44, 0, false, 90, 90, 32, 32);
  ws.onmessage({ data: pkt3 });

  // Send Keyframe packet -> Flushes any pending delta tiles
  const kfPkt = createPacket(0x46, 0, true, 0, 0, 800, 480);
  ws.onmessage({ data: kfPkt });

  // Verify rotation packet updates
  const rotPkt = createPacket(0x46, 1, true, 0, 0, 800, 480); // rot_code = 1 -> 90 deg
  ws.onmessage({ data: rotPkt });
  assert.equal(sandbox.__getRotation(), 90, 'Packet rotation must update remote.html rotation');
});

test('Production remote.html: pointer gesture state machine (quick tap vs hold-drag vs move throttling)', () => {
  const { sandbox, mockCanvas, getSentWsMessages, advanceTime, fireTimers } = createRemoteContext();
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  ws.onopen();

  // Clear initial 'K' message
  const sent = getSentWsMessages();
  sent.length = 0;

  // Scenario 1: Quick Tap (< 350ms, no move > 6px) -> sends 'tap' ('T x y')
  mockCanvas.dispatchEvent('pointerdown', { clientX: 100, clientY: 150 });
  advanceTime(50);
  mockCanvas.dispatchEvent('pointerup', { clientX: 100, clientY: 150 });
  assert.equal(sent.length, 1);
  assert.equal(sent[0], 'T 100 150', 'Quick tap should send T x y');
  sent.length = 0;

  // Scenario 2: Touch-and-hold (> 250ms holdTimer fires) -> sends 'down' ('D x y')
  mockCanvas.dispatchEvent('pointerdown', { clientX: 200, clientY: 250 });
  advanceTime(260);
  fireTimers(); // holdTimer fires
  assert.equal(sent.length, 1);
  assert.equal(sent[0], 'D 200 250', 'Hold timer should trigger D x y');

  // Releasing after hold -> sends 'up' ('U x y')
  advanceTime(100);
  mockCanvas.dispatchEvent('pointerup', { clientX: 200, clientY: 250 });
  assert.equal(sent.length, 2);
  assert.equal(sent[1], 'U 200 250', 'Releasing hold should trigger U x y');
  sent.length = 0;

  // Scenario 3: Drag gesture with movement threshold (> 6px) and 20ms throttling
  mockCanvas.dispatchEvent('pointerdown', { clientX: 300, clientY: 300 });

  // Move 3px (below 6px threshold) -> should not trigger down or move
  advanceTime(10);
  mockCanvas.dispatchEvent('pointermove', { clientX: 303, clientY: 300 });
  assert.equal(sent.length, 0);

  // Move 15px (crosses 6px threshold) -> triggers down and throttled move
  advanceTime(25);
  mockCanvas.dispatchEvent('pointermove', { clientX: 315, clientY: 300 });
  assert.equal(sent[0], 'D 300 300', 'Crossing move threshold should send D at start coord');
  assert.equal(sent[1], 'M 315 300', 'First move event sent');

  // Rapid move within 10ms (< 20ms throttling) -> throttled
  advanceTime(10);
  mockCanvas.dispatchEvent('pointermove', { clientX: 320, clientY: 300 });
  assert.equal(sent.length, 2, 'Rapid move within 10ms must be throttled');

  // Move after 25ms (>= 20ms throttling) -> sends move
  advanceTime(25);
  mockCanvas.dispatchEvent('pointermove', { clientX: 325, clientY: 300 });
  assert.equal(sent.length, 3);
  assert.equal(sent[2], 'M 325 300');

  // Release drag -> sends up
  advanceTime(10);
  mockCanvas.dispatchEvent('pointerup', { clientX: 325, clientY: 300 });
  assert.equal(sent.length, 4);
  assert.equal(sent[3], 'U 325 300', 'Finishing drag sends U x y');
});

test('Production remote.html: WebSocket disconnect and auto-recovery lifecycle', () => {
  const { sandbox, elements, getSentWsMessages, advanceTime } = createRemoteContext();
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  ws.onopen();

  const sent = getSentWsMessages();
  assert.equal(sent[0], 'K', 'Keyframe requested on connection');

  // Simulate WebSocket close event
  ws.onclose();
  assert.equal(elements.stat.textContent, 'HTTP Fallback', 'Status should update to HTTP Fallback on close');
  assert.equal(elements.mode.textContent, 'Live Mirror', 'Mode should revert to Live Mirror');
  assert.equal(sandbox.__getPendingQueue().length, 0, 'Pending queue must be cleared on disconnect');

  // Re-open and verify keyframe auto-recovery
  sandbox.initWebSocket();
  const newWs = sandbox.__getWs();
  newWs.onopen();
  sent.length = 0;

  // Trigger error recovery
  advanceTime(1500); // Exceed 1000ms debounce
  sandbox.__requestKeyframeRecovery();
  assert.equal(sent[0], 'K', 'Corrupt frame or error should request full keyframe K');
});

test('Production remote.html: mobile touch and viewport optimizations', () => {
  // 1. Verify viewport meta tag prevents accidental zooming and supports edge-to-edge screens
  assert.match(htmlContent, /<meta\s+name=["']viewport["']\s+content=["'][^"']*user-scalable=no[^"']*["']/, 'Viewport meta tag must include user-scalable=no');
  assert.match(htmlContent, /<meta\s+name=["']viewport["']\s+content=["'][^"']*viewport-fit=cover[^"']*["']/, 'Viewport meta tag must include viewport-fit=cover');

  // 2. Verify touch-action: none is applied to screen canvas and container to prevent viewport scrolling
  assert.match(htmlContent, /\.screen-container\s*\{[^}]*touch-action:\s*none;/, '.screen-container must have touch-action: none');
  assert.match(htmlContent, /#screenCanvas\s*\{[^}]*touch-action:\s*none;/, '#screenCanvas must have touch-action: none');

  // 3. Verify overscroll-behavior: none prevents pull-to-refresh and rubber-band viewport movement
  assert.match(htmlContent, /overscroll-behavior:\s*none;/, 'html, body must have overscroll-behavior: none');

  // 4. Verify dynamic viewport units (100dvh) are used for mobile address bar resilience
  assert.match(htmlContent, /100dvh/, 'CSS should use dynamic viewport units (100dvh)');

  // 5. Verify contextmenu on canvas calls preventDefault
  const { mockCanvas } = createRemoteContext();
  let defaultPrevented = false;
  mockCanvas.dispatchEvent('contextmenu', {
    preventDefault: () => { defaultPrevented = true; },
  });
  assert.equal(defaultPrevented, true, 'canvas contextmenu must call preventDefault to suppress context menus during touch hold');
});

test('Production remote.html: tab visibility lifecycle (background disconnect, auto-recovery on visible, pagehide cleanup)', () => {
  const { sandbox, elements, dispatchDocEvent, dispatchWinEvent, getSentWsMessages } = createRemoteContext();

  // 1. Initial state: connected via WebSocket
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  assert.ok(ws, 'WebSocket should be initialized');
  ws.onopen();
  assert.equal(elements.stat.textContent, 'WS Connected');

  // 2. Tab becomes hidden (user switches tab, minimizes Safari, locks screen)
  sandbox.document.visibilityState = 'hidden';
  dispatchDocEvent('visibilitychange');

  // WebSocket must be closed immediately
  assert.equal(ws.readyState, 3 /* CLOSED */, 'WebSocket must be closed when tab is hidden');
  assert.equal(sandbox.__getWs(), null, 'ws reference must be null after hiding tab');
  assert.equal(elements.stat.textContent, 'Paused (Background)', 'Status must show Paused (Background)');

  // While hidden, attempting to initWebSocket must not create a new socket
  sandbox.initWebSocket();
  assert.equal(sandbox.__getWs(), null, 'initWebSocket must be a no-op while tab is hidden');

  // 3. Tab becomes visible again
  const sent = getSentWsMessages();
  sent.length = 0;
  sandbox.document.visibilityState = 'visible';
  dispatchDocEvent('visibilitychange');

  // New WebSocket must be opened
  const resumedWs = sandbox.__getWs();
  assert.ok(resumedWs, 'New WebSocket must be created when tab becomes visible');
  resumedWs.onopen();
  assert.equal(elements.stat.textContent, 'WS Connected');
  assert.equal(sent[0], 'K', 'Resumed connection must immediately request keyframe K');

  // 4. Pagehide (tab closed, window navigated away)
  dispatchWinEvent('pagehide');
  assert.equal(resumedWs.readyState, 3 /* CLOSED */, 'pagehide must close active WebSocket');
  assert.equal(sandbox.__getWs(), null, 'ws reference must be null after pagehide');
});

test('Production remote.html: screen dimming and banner state transitions ("Connecting" vs "Disconnected")', () => {
  // Verify HTML structure for overlay and banner
  assert.match(htmlContent, /class=["'][^"']*screen-overlay[^"']*["']/, 'remote.html must contain screen-overlay element');
  assert.match(htmlContent, /class=["'][^"']*banner[^"']*["']/, 'remote.html must contain banner element');
  assert.match(htmlContent, /\.screen-overlay\s*\{[^}]*position:\s*absolute;/, 'screen-overlay must be positioned absolutely');

  const { sandbox, elements, mockCanvas, dispatchDocEvent, dispatchWinEvent } = createRemoteContext();

  // 1. Initial state: dim screen with "Connecting" banner over it
  assert.equal(elements.banner.textContent, 'Connecting', 'Initial banner text must be "Connecting"');
  assert.equal(elements.screenOverlay.style.display, 'flex', 'Initial overlay must be visible');
  assert.equal(mockCanvas.style.filter, 'brightness(0.6)', 'Initial screen canvas must be dimmed');

  // 2. Connected state: banner hidden and dimming removed
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  assert.ok(ws, 'WebSocket instance must exist');
  ws.onopen();
  assert.equal(elements.screenOverlay.style.display, 'none', 'Banner overlay must be hidden when connected');
  assert.equal(mockCanvas.style.filter, '', 'Screen dimming must be cleared when connected');

  // 3. Intentionally disconnected when backgrounded: dim screen with "Disconnected" banner
  sandbox.document.visibilityState = 'hidden';
  dispatchDocEvent('visibilitychange');
  assert.equal(elements.banner.textContent, 'Disconnected', 'Banner must display "Disconnected" when tab is backgrounded');
  assert.equal(elements.screenOverlay.style.display, 'flex', 'Banner overlay must be visible when intentionally disconnected');
  assert.equal(mockCanvas.style.filter, 'brightness(0.6)', 'Screen must be dimmed when backgrounded');

  // 4. Reconnecting when foregrounded: dim screen with "Connecting" banner
  sandbox.document.visibilityState = 'visible';
  dispatchDocEvent('visibilitychange');
  assert.equal(elements.banner.textContent, 'Connecting', 'Banner must display "Connecting" when reconnecting');
  assert.equal(elements.screenOverlay.style.display, 'flex', 'Banner overlay must be visible when connecting');
  assert.equal(mockCanvas.style.filter, 'brightness(0.6)', 'Screen must be dimmed when connecting');

  // 5. Reconnected: banner hidden and dimming removed
  const resumedWs = sandbox.__getWs();
  assert.ok(resumedWs, 'Resumed WebSocket must exist');
  resumedWs.onopen();
  assert.equal(elements.screenOverlay.style.display, 'none', 'Banner overlay must be hidden after reconnecting');
  assert.equal(mockCanvas.style.filter, '', 'Screen dimming must be cleared after reconnecting');

  // 6. Intentionally disconnected on pagehide / cleanup: dim screen with "Disconnected" banner
  dispatchWinEvent('pagehide');
  assert.equal(elements.banner.textContent, 'Disconnected', 'Banner must display "Disconnected" on pagehide cleanup');
  assert.equal(elements.screenOverlay.style.display, 'flex', 'Banner overlay must be visible on cleanup');
  assert.equal(mockCanvas.style.filter, 'brightness(0.6)', 'Screen must be dimmed on cleanup');
});

test('Production remote.html: visible disconnect & HTTP fallback error handling (Connecting banner during OTA)', () => {
  // Verify increased banner sizing styles
  assert.match(htmlContent, /font-size:\s*20px;/, 'Banner should have increased font-size (20px)');
  assert.match(htmlContent, /padding:\s*16px\s+36px;/, 'Banner should have increased padding (16px 36px)');

  const { sandbox, elements, mockCanvas, getLastMockImage } = createRemoteContext();

  // 1. Connect via WebSocket
  sandbox.initWebSocket();
  const ws = sandbox.__getWs();
  assert.ok(ws);
  ws.onopen();
  assert.equal(elements.screenOverlay.style.display, 'none');

  // 2. Disconnect while visible (e.g. ESP32 enters OTA and drops client sockets)
  ws.onclose();
  assert.equal(elements.banner.textContent, 'Connecting', 'Must show Connecting banner when disconnected while visible');
  assert.equal(elements.screenOverlay.style.display, 'flex', 'Overlay must be visible when disconnected while visible');
  assert.equal(mockCanvas.style.filter, 'brightness(0.6)', 'Canvas must be dimmed when disconnected while visible');
  assert.equal(elements.stat.textContent, 'HTTP Fallback', 'Status shows HTTP Fallback');

  // 3. Trigger HTTP snapshot refresh failure (e.g. HTTP 503 during OTA)
  const refreshPromise = sandbox.refreshScreen(false);
  const img1 = getLastMockImage();
  assert.ok(img1, 'Image instance should be created by refreshScreen');
  img1.onerror(); // Simulate 503 response
  return refreshPromise.then(() => {
    assert.equal(elements.banner.textContent, 'Connecting', 'Must keep Connecting banner on snapshot failure');
    assert.equal(elements.screenOverlay.style.display, 'flex', 'Overlay must remain visible on snapshot failure');
    assert.equal(mockCanvas.style.filter, 'brightness(0.6)', 'Canvas must stay dimmed on snapshot failure');

    // 4. Simulate recovery once OTA finishes (HTTP 200 snapshot loads)
    const recoveryPromise = sandbox.refreshScreen(false);
    const img2 = getLastMockImage();
    assert.ok(img2);
    img2.onload();
    return recoveryPromise.then(() => {
      assert.equal(elements.screenOverlay.style.display, 'none', 'Banner overlay must hide when snapshot succeeds');
      assert.equal(mockCanvas.style.filter, '', 'Canvas dimming must be cleared when snapshot succeeds');
    });
  });
});


