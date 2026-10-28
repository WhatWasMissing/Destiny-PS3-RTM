const { test } = require('node:test');
const assert = require('node:assert/strict');
const { create, signatures } = require('../source/js/build-compatibility.js');
assert.equal(signatures.length, 5, 'profile must include all audited code anchors');

function toBytes(hex) {
  return Uint8Array.from(hex.match(/../g), pair => parseInt(pair, 16));
}

test('the 01.24 profile verifies all independent code signatures and caches briefly', async () => {
  let reads = 0, now = 1000;
  const guard = create({
    session: () => '1:192.168.0.62',
    now: () => now,
    read: async (address, length) => {
      reads++;
      const signature = signatures.find(candidate => candidate.address === address);
      assert.equal(length, signature.hex.length / 2);
      return toBytes(signature.hex);
    }
  });
  const first = await guard.ensure();
  assert.equal(first.status, 'verified');
  assert.equal(first.cached, false);
  assert.equal(reads, signatures.length);
  assert.equal((await guard.ensure()).cached, true);
  assert.equal(reads, signatures.length);
  now += 5001;
  assert.equal((await guard.ensure()).cached, false);
  assert.equal(reads, signatures.length * 2);
});

test('a signature mismatch fails closed and leaves the profile uncached', async () => {
  let reads = 0;
  const guard = create({
    session: () => '1:192.168.0.62',
    read: async address => {
      reads++;
      const signature = signatures.find(candidate => candidate.address === address);
      return toBytes(address === signatures[1].address ? '00000000' : signature.hex);
    }
  });
  await assert.rejects(guard.ensure(), /signature mismatch at 0x001FB3C8/);
  await assert.rejects(guard.ensure(), /signature mismatch at 0x001FB3C8/);
  assert.equal(reads, 4, 'failed profile checks are retried, never cached');
});

test('writes cannot overwrite any of the signatures used for future verification', async () => {
  let reads = 0;
  const guard = create({
    session: () => '1:192.168.0.62',
    read: async () => { reads++; return ''; }
  });
  await assert.rejects(guard.ensure([[parseInt(signatures[2].address, 16) - 1, '000000']]), /overlaps protected 01.24 code signature/);
  assert.equal(reads, 0);
});

test('the profile is bound to a connected session and detects a session change mid-read', async () => {
  let current = '1:192.168.0.62', reads = 0;
  const guard = create({
    session: () => current,
    read: async address => {
      reads++;
      const signature = signatures.find(candidate => candidate.address === address);
      current = '2:192.168.0.62';
      return toBytes(signature.hex);
    }
  });
  await assert.rejects(guard.ensure(), /session changed during build verification/);
  assert.equal(reads, 1);
  current = '';
  await assert.rejects(guard.ensure(), /Connect to a console/);
});
