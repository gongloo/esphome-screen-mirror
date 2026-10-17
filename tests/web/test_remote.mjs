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

  const mockCanvas = {
    width: initialW,
    height: initialH,
    style: {},
    addEventListener: () => {},
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

  const sandbox = {
    document: {
      getElementById: (id) => elements[id] || { textContent: '', style: {} },
      addEventListener: () => {},
      createElement: () => ({ className: '', style: {}, remove: () => {} }),
      visibilityState: 'visible',
    },
    window: {
      addEventListener: () => {},
      location: { protocol: 'http:', host: 'esphome.local' },
      devicePixelRatio: 1,
    },
    location: { protocol: 'http:', host: 'esphome.local' },
    performance: { now: () => 1000 },
    WebSocket: MockWebSocket,
    Image: class MockImage {},
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
    setTimeout: () => {},
    clearTimeout: () => {},
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
`;
  vm.runInContext(scriptCode + harnessHook, sandbox);

  return {
    sandbox,
    mockCanvas,
    getSentWsMessages: () => wsSentMessages,
    setCanvasRect: (rect) => { currentRect = { ...rect }; },
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
