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
