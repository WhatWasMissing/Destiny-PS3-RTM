const test = require('node:test');
const assert = require('node:assert/strict');
const {
  bytesFromHex,
  beU32,
  beFloat32,
  findEmblemMatches,
  findAmmoCandidates,
  findSpecialAmmoCandidates,
  framebufferToRgba
} = require('../source/js/memory-features.js');

test('memory hex parser rejects partial bytes', () => {
  assert.deepEqual([...bytesFromHex('00 aF')], [0, 0xAF]);
  assert.throws(() => bytesFromHex('ABC'), /whole-byte hex/);
});

test('emblem scanner returns aligned 16-bit matches only', () => {
  const memory = new Uint8Array(16);
  memory.set([0x12, 0x34], 2);
  memory.set([0x12, 0x34], 9);
  memory.set([0x12, 0x34], 12);
  assert.deepEqual(findEmblemMatches(memory, 0x600E0000, 0x1234), [0x600E0002, 0x600E000C]);
});

test('ammo scan requires the source values and its 0x80A6 slot marker', () => {
  const memory = new Uint8Array(96);
  const currentMag = 37;
  const currentReserve = 142;
  const candidate = 12;
  memory.set([...beU32(currentReserve), ...beU32(currentMag)], candidate);
  memory.set([0x80, 0xA6], candidate + 28);
  memory.set([...beU32(currentReserve), ...beU32(currentMag)], 48);
  memory.set([0x80, 0xA5], 48 + 28);
  assert.deepEqual(findAmmoCandidates(memory, 0x60300000, currentMag, currentReserve), [0x6030000C]);
});

test('special ammo scan matches the float signature and separate 0x8080 marker', () => {
  const memory = new Uint8Array(128);
  memory.set([...beFloat32(1.5), 0x01, 0x00, 0x00, 0x00], 7);
  memory.set([0x80, 0x80], 7 + 40);
  memory.set([...beFloat32(1.5), 0x01, 0x00, 0x00, 0x00], 64);
  memory.set([0x80, 0x81], 64 + 40);
  assert.deepEqual(findSpecialAmmoCandidates(memory, 0x60300000, 1.5), [0x60300007]);
});

test('framebuffer conversion handles padded rows and A,B,G,R source bytes', () => {
  const memory = new Uint8Array([
    0xFF, 10, 20, 30, 0xFF, 40, 50, 60, 0, 0, 0, 0,
    0xFF, 70, 80, 90, 0xFF, 100, 110, 120, 0, 0, 0, 0
  ]);
  const rgba = framebufferToRgba(memory, 2, 2, 12);
  assert.deepEqual([...rgba], [
    10, 20, 30, 255, 40, 50, 60, 255,
    70, 80, 90, 255, 100, 110, 120, 255
  ]);
  assert.throws(() => framebufferToRgba(memory.subarray(0, 12), 2, 2, 12), /invalid/);
});
