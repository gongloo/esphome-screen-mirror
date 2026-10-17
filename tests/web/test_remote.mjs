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
