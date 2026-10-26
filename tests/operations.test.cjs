const { test } = require('node:test');
const assert = require('node:assert/strict');
const { createRunner } = require('../source/js/operations.js');
test('verified requires matching readback, not only an acknowledged write', async () => {
  const memory = new Map();
  const runner = createRunner({ session: () => 'console1', write: async (a,v) => memory.set(a,v), read: async a => memory.get(a) });
  assert.equal((await runner.apply([['1234','AABB']])).status, 'verified');
  const mismatch = createRunner({ session: () => 'console1', write: async () => {}, read: async () => '0000' });
  await assert.rejects(mismatch.apply([['1234','AABB']]), /Readback mismatch/);
});
test('unreadable response is explicitly unverified', async () => {
  const runner = createRunner({ session: () => 'console1', write: async () => {}, read: async () => { throw new Error('not readable'); } });
  assert.equal((await runner.apply([['1234','AA']])).status, 'unverified');
});
test('session change prevents remaining writes and queued actions', async () => {
  let owner = 'console1', writes = 0;
  const runner = createRunner({ session: () => owner, write: async () => { writes++; owner = 'console2'; }, read: async () => 'AA' });
  const first = runner.apply([['1234','AA'],['1235','BB']]);
  const queued = runner.apply([['5678','CC']]);
  await assert.rejects(first, /session changed/); await assert.rejects(queued, /session changed/);
  assert.equal(writes, 1);
});
test('partial failures report count and do not stop later valid operations', async () => {
  let calls = 0;
  const runner = createRunner({ session: () => 'console1', write: async () => { if (++calls === 2) throw new Error('network lost'); }, read: async () => 'AA' });
  await assert.rejects(runner.apply([['1234','AA'],['1235','AA']]), /1\/2 writes acknowledged/);
  assert.equal((await runner.apply([['1234','AA']])).status, 'verified');
});
