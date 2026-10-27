const test = require('node:test');
const assert = require('node:assert/strict');
const packet = require('../source/js/spawn-packet.js');

const sample = {
  sequence: 7,
  expectedType: -1,
  tag: 0x8087600f,
  placementMode94: 0,
  placementMode98: 1,
  count: 1,
  placementModeA0: 0,
  pose: [5, 1, 1, 0, 0, 0, 0, 0, 0]
};

test('operation-1 native vector matches the source prefix and slot copy', () => {
  const body = packet.buildOperation1(sample);
  const vector = packet.buildNativeOperation1Vector(sample);
  assert.equal(body.byteLength, 0x12c);
  assert.equal(vector.byteLength, 0x130);
  assert.deepEqual([...vector.subarray(0, 4)], [0, 0, 0, 0]);
  assert.deepEqual([...vector.subarray(4)], [...body]);

  const decoded = packet.decodeNativeOperation1Vector(vector);
  assert.equal(decoded.operation, 1);
  assert.equal(decoded.sequence, sample.sequence);
  assert.equal(decoded.tag, sample.tag);
  assert.equal(decoded.scaleIsPositive, true);
  assert.equal(decoded.nativePrefixIsZero, true);
  assert.equal(decoded.reservedBytesAreZero, true);
});

test('operation-1 validates source tags, modes, count, spacing, and positive spawn scale', () => {
  assert.throws(() => packet.buildOperation1({ ...sample, tag: 0 }), /tag/);
  assert.throws(() => packet.buildOperation1({ ...sample, count: 33 }), /count/);
  assert.throws(() => packet.buildOperation1({ ...sample, pose: [5, -1, 1, 0, 0, 0, 0, 0, 0] }), /cannot be negative/);
  assert.throws(() => packet.buildOperation1({ ...sample, pose: [5, 1, 0, 0, 0, 0, 0, 0, 0] }), /must be positive/);
  assert.throws(() => packet.buildOperation1({ ...sample, pose: [5, 1, -1, 0, 0, 0, 0, 0, 0] }), /must be positive/);
  assert.throws(() => packet.buildOperation1({ ...sample, pose: [5, 1, 1e-50, 0, 0, 0, 0, 0, 0] }), /must be positive/);
  assert.doesNotThrow(() => packet.buildOperation1({ ...sample, pose: [5, 0, 1, 0, 0, 0, 0, 0, 0] }));
});

test('operation-1 decoder flags a nonpositive raw scale', () => {
  const body = packet.buildOperation1(sample);
  new DataView(body.buffer).setFloat32(packet.BODY_OFFSETS.pose + 2 * 4, 0, false);
  assert.equal(packet.decodeOperation1(body).scaleIsPositive, false);
});
