const test = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const images = require('../source/js/spawn-executor-images.js');

function sha256(bytes) {
  return crypto.createHash('sha256').update(bytes).digest('hex');
}

test('embedded PPC images exactly match the audited generated binaries', () => {
  const root = path.join(__dirname, '..');
  const cases = [
    ['spawn_program', images.createSpawnProgram(), 'research/spawn-program-first-slot.bin'],
    ['bootstrap_executor', images.createBootstrapExecutor(), 'research/bootstrap-executor.bin']
  ];
  for (const [key, bytes, relativePath] of cases) {
    const source = fs.readFileSync(path.join(root, relativePath));
    assert.deepEqual(Buffer.from(bytes), source);
    assert.equal(bytes.byteLength, images.metadata[key].length);
    assert.equal(sha256(bytes), images.metadata[key].sha256);
  }
});

test('bootstrap passes task-entry context to its registered callback', () => {
  const code = Buffer.from(images.createBootstrapExecutor());
  const callbackLoad = code.readUInt32BE(0x4c);
  const contextLoad = code.readUInt32BE(0x60);

  // D-form lwz: callback r12 <- [r30 + 0], context r3 <- [r30 + 4].
  // A previous image used r12 as the context base and read callback code instead.
  const decodeLoad = word => ({ opcode: word >>> 26, target: (word >>> 21) & 31, base: (word >>> 16) & 31, displacement: word & 0xffff });
  assert.deepEqual(decodeLoad(callbackLoad), { opcode: 32, target: 12, base: 30, displacement: 0 });
  assert.deepEqual(decodeLoad(contextLoad), { opcode: 32, target: 3, base: 30, displacement: 4 });
  assert.equal(code.readUInt32BE(0x64), 0x7d8903a6); // mtctr r12
  assert.equal(code.readUInt32BE(0x68), 0x4e800421); // bctrl
  assert.equal(code.readUInt32BE(0x90), 0x38000002); // li r0, 2
  assert.deepEqual(decodeLoad(code.readUInt32BE(0x98)), { opcode: 32, target: 2, base: 30, displacement: 8 });
});
