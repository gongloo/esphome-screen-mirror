import test from 'node:test';
import assert from 'node:assert/strict';

// Isolated implementation of clientToRaw directly from remote.html
function clientToRaw(clientX, clientY, rect, canvasWidth, canvasHeight, rotation, hwWidth, hwHeight) {
  const clickX = clientX - rect.left;
  const clickY = clientY - rect.top;

  const dispX = Math.round(clickX * (canvasWidth / rect.width));
  const dispY = Math.round(clickY * (canvasHeight / rect.height));

  let rawX, rawY;
  if (rotation === 90) {
    rawX = hwWidth - dispY - 1;
    rawY = dispX;
  } else if (rotation === 180) {
    rawX = hwWidth - dispX - 1;
    rawY = hwHeight - dispY - 1;
  } else if (rotation === 270) {
    rawX = dispY;
    rawY = hwHeight - dispX - 1;
  } else {
    rawX = dispX;
    rawY = dispY;
  }

  rawX = Math.max(0, Math.min(hwWidth - 1, Math.round(rawX)));
  rawY = Math.max(0, Math.min(hwHeight - 1, Math.round(rawY)));
  return { clickX, clickY, rawX, rawY };
}

test('Coordinate Transformation - 0 deg rotation', () => {
  const rect = { left: 0, top: 0, width: 800, height: 480 };
  const hwW = 800, hwH = 480;

  // Origin (top-left)
  const tl = clientToRaw(0, 0, rect, 800, 480, 0, hwW, hwH);
  assert.equal(tl.rawX, 0);
  assert.equal(tl.rawY, 0);

  // Bottom-right
  const br = clientToRaw(799, 479, rect, 800, 480, 0, hwW, hwH);
  assert.equal(br.rawX, 799);
  assert.equal(br.rawY, 479);

  // Clamping check
  const oob = clientToRaw(-50, 600, rect, 800, 480, 0, hwW, hwH);
  assert.equal(oob.rawX, 0);
  assert.equal(oob.rawY, 479);
});

test('Coordinate Transformation - 90 deg rotation', () => {
  // Canvas is 480x800, Hardware is 800x480
  const rect = { left: 0, top: 0, width: 480, height: 800 };
  const hwW = 800, hwH = 480;

  // Top-left of displayed canvas -> top-right of hardware framebuffer
  const tl = clientToRaw(0, 0, rect, 480, 800, 90, hwW, hwH);
  assert.equal(tl.rawX, 799);
  assert.equal(tl.rawY, 0);

  // Bottom-right of displayed canvas -> bottom-left of hardware framebuffer
  const br = clientToRaw(479, 799, rect, 480, 800, 90, hwW, hwH);
  assert.equal(br.rawX, 0);
  assert.equal(br.rawY, 479);
});

test('Coordinate Transformation - 180 deg rotation', () => {
  const rect = { left: 0, top: 0, width: 800, height: 480 };
  const hwW = 800, hwH = 480;

  const tl = clientToRaw(0, 0, rect, 800, 480, 180, hwW, hwH);
  assert.equal(tl.rawX, 799);
  assert.equal(tl.rawY, 479);

  const br = clientToRaw(799, 479, rect, 800, 480, 180, hwW, hwH);
  assert.equal(br.rawX, 0);
  assert.equal(br.rawY, 0);
});

test('Coordinate Transformation - 270 deg rotation', () => {
  const rect = { left: 0, top: 0, width: 480, height: 800 };
  const hwW = 800, hwH = 480;

  const tl = clientToRaw(0, 0, rect, 480, 800, 270, hwW, hwH);
  assert.equal(tl.rawX, 0);
  assert.equal(tl.rawY, 479);

  const br = clientToRaw(479, 799, rect, 480, 800, 270, hwW, hwH);
  assert.equal(br.rawX, 799);
  assert.equal(br.rawY, 0);
});

test('Queue Backpressure and Keyframe Flush', () => {
  const MAX_PENDING = 2;
  const pendingQueue = [];

  function enqueue(magic, id) {
    if (magic === 0x46) { // Keyframe
      pendingQueue.length = 0;
    } else if (pendingQueue.length >= MAX_PENDING) {
      pendingQueue.shift();
    }
    pendingQueue.push({ magic, id });
  }

  // Push 2 delta frames
  enqueue(0x44, 'd1');
  enqueue(0x44, 'd2');
  assert.equal(pendingQueue.length, 2);
  assert.equal(pendingQueue[0].id, 'd1');

  // Push 3rd delta frame -> should drop oldest ('d1')
  enqueue(0x44, 'd3');
  assert.equal(pendingQueue.length, 2);
  assert.equal(pendingQueue[0].id, 'd2');
  assert.equal(pendingQueue[1].id, 'd3');

  // Keyframe arrives -> drops all delta frames and keeps only keyframe
  enqueue(0x46, 'kf1');
  assert.equal(pendingQueue.length, 1);
  assert.equal(pendingQueue[0].id, 'kf1');
});

test('Auto-Recovery Keyframe Debounce', () => {
  let recoveryCount = 0;
  let lastKeyframeRecovery = -1000;

  function triggerError(now) {
    if (now - lastKeyframeRecovery > 1000) {
      lastKeyframeRecovery = now;
      recoveryCount++;
      return true;
    }
    return false;
  }

  // First error at t = 100ms -> sent
  assert.equal(triggerError(100), true);
  assert.equal(recoveryCount, 1);

  // Rapid consecutive errors (< 1000ms later) -> throttled
  assert.equal(triggerError(200), false);
  assert.equal(triggerError(500), false);
  assert.equal(triggerError(1099), false);
  assert.equal(recoveryCount, 1);

  // Error after 1000ms cooldown (t = 1101ms) -> sent
  assert.equal(triggerError(1101), true);
  assert.equal(recoveryCount, 2);
});

test('Binary Packet Header Parser', () => {
  function parseHeader(buf) {
    if (!(buf instanceof ArrayBuffer) || buf.byteLength < 10) return null;
    const view = new DataView(buf);
    const magic = view.getUint8(0);
    const flags = view.getUint8(1);
    const x = (view.getUint8(2) << 8) | view.getUint8(3);
    const y = (view.getUint8(4) << 8) | view.getUint8(5);
    const w = (view.getUint8(6) << 8) | view.getUint8(7);
    const h = (view.getUint8(8) << 8) | view.getUint8(9);
    const rotation = ((flags >> 2) & 0x03) * 90;
    const isKeyframe = (flags & 0x01) === 1;
    return { magic, flags, x, y, w, h, rotation, isKeyframe };
  }

  // 1. Valid Keyframe packet at 90 deg rotation, full screen 800x480
  const kfBuf = new ArrayBuffer(10);
  const kfView = new DataView(kfBuf);
  kfView.setUint8(0, 0x46); // 'F'
  kfView.setUint8(1, (1 << 2) | 1); // rot_code = 1 (90 deg), isKeyframe = 1
  kfView.setUint16(2, 0); // x = 0
  kfView.setUint16(4, 0); // y = 0
  kfView.setUint16(6, 800); // w = 800
  kfView.setUint16(8, 480); // h = 480

  const parsedKf = parseHeader(kfBuf);
  assert.notEqual(parsedKf, null);
  assert.equal(parsedKf.magic, 0x46);
  assert.equal(parsedKf.isKeyframe, true);
  assert.equal(parsedKf.rotation, 90);
  assert.equal(parsedKf.x, 0);
  assert.equal(parsedKf.y, 0);
  assert.equal(parsedKf.w, 800);
  assert.equal(parsedKf.h, 480);

  // 2. Valid Delta tile packet at 270 deg rotation (rot_code = 3)
  const dtBuf = new ArrayBuffer(10);
  const dtView = new DataView(dtBuf);
  dtView.setUint8(0, 0x44); // 'D'
  dtView.setUint8(1, (3 << 2) | 0); // rot_code = 3 (270 deg), isKeyframe = 0
  dtView.setUint16(2, 64);  // x = 64
  dtView.setUint16(4, 128); // y = 128
  dtView.setUint16(6, 32);  // w = 32
  dtView.setUint16(8, 48);  // h = 48

  const parsedDt = parseHeader(dtBuf);
  assert.notEqual(parsedDt, null);
  assert.equal(parsedDt.magic, 0x44);
  assert.equal(parsedDt.isKeyframe, false);
  assert.equal(parsedDt.rotation, 270);
  assert.equal(parsedDt.x, 64);
  assert.equal(parsedDt.y, 128);
  assert.equal(parsedDt.w, 32);
  assert.equal(parsedDt.h, 48);

  // 3. Truncated packet (< 10 bytes)
  const shortBuf = new ArrayBuffer(8);
  assert.equal(parseHeader(shortBuf), null);
});

test('Touch Command Protocol Formatting', () => {
  function formatTouchCmd(rawX, rawY, action) {
    if (action === 'tap') return `T ${rawX} ${rawY}`;
    if (action === 'down') return `D ${rawX} ${rawY}`;
    if (action === 'move') return `M ${rawX} ${rawY}`;
    if (action === 'up') return `U ${rawX} ${rawY}`;
    return null;
  }

  assert.equal(formatTouchCmd(150, 250, 'tap'), 'T 150 250');
  assert.equal(formatTouchCmd(10, 20, 'down'), 'D 10 20');
  assert.equal(formatTouchCmd(12, 22, 'move'), 'M 12 22');
  assert.equal(formatTouchCmd(14, 24, 'up'), 'U 14 24');
  assert.equal(formatTouchCmd(0, 0, 'invalid'), null);
});

