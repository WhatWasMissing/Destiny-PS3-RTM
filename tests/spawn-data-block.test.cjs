const test = require('node:test');
const assert = require('node:assert/strict');
const spawnData = require('../source/js/spawn-data-block.js');

test('builds the exact Blackrock BRC1 data image and eight zeroed records', () => {
  const bytes = spawnData.buildDataBlock();
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  assert.equal(bytes.byteLength, 0x1cd0);
  assert.equal(view.getUint32(0, false), spawnData.HEADER_MAGIC);
  assert.equal(view.getUint32(4, false), 1);
  assert.equal(view.getUint32(0x10, false), 0x3f000000);
  assert.equal(view.getUint32(0x44, false), 0x0000000c);
  assert.equal(view.getUint32(0x48, false), 0x80800906);
  assert.equal(view.getUint32(0x74, false), 0xffffffff);
  assert.ok(bytes.subarray(0x0c0, 0x150).every(value => value === 0));
  assert.ok(bytes.subarray(0x150).every(value => value === 0));
});

test('record addresses tile the final 0x1b80-byte region', () => {
  const base = 0x01c75000;
  assert.equal(spawnData.recordAddress(base, 0), base + 0x150);
  assert.equal(spawnData.recordAddress(base, 7), base + 0x1960);
  assert.equal(spawnData.recordAddress(base, 7) + spawnData.RECORD_LENGTH, base + 0x1cd0);
  assert.throws(() => spawnData.recordAddress(base, 8), /record index/);
});
