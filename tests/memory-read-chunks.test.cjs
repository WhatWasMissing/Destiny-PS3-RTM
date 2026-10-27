const test = require('node:test');
const assert = require('node:assert/strict');
const { readHexChunks } = require('../source/js/memory-read-chunks.js');

test('readHexChunks splits reads at the configured request size', async () => {
  const requests = [];
  const result = await readHexChunks(0x20000, 0x245, async (address, length) => {
    requests.push({ address, length });
    return 'AB'.repeat(length);
  }, { chunkBytes: 0x100 });

  assert.deepEqual(requests, [
    { address: 0x20000, length: 0x100 },
    { address: 0x20100, length: 0x100 },
    { address: 0x20200, length: 0x45 }
  ]);
  assert.equal(result, 'AB'.repeat(0x245));
});

test('readHexChunks rejects short and malformed responses', async () => {
  await assert.rejects(
    readHexChunks(0x1234, 4, async () => 'AABB', { chunkBytes: 4 }),
    /Short or invalid read at 0x1234/
  );
  await assert.rejects(
    readHexChunks(0x1234, 2, async () => 'GGGG', { chunkBytes: 2 }),
    /Short or invalid read at 0x1234/
  );
});

test('readHexChunks rechecks the session around each request and stops on change', async () => {
  let current = true;
  let checks = 0;
  let requests = 0;
  await assert.rejects(readHexChunks(0x10000, 0x200, async (_address, length) => {
    requests++;
    current = false;
    return '00'.repeat(length);
  }, {
    chunkBytes: 0x100,
    assertCurrent() {
      checks++;
      if (!current) throw new Error('session changed');
    }
  }), /session changed/);
  assert.equal(requests, 1);
  assert.equal(checks, 2);
});
