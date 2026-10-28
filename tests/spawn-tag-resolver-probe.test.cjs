const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'spawn-tag-resolver-probe.js'), 'utf8');
const resolverSignature = source.match(/const HANDLE_RESOLVER_SIGNATURE = '([0-9A-F]+)'/)[1];
const owner = '1:192.168.1.100';
const profile = 'BLES01857 01.24 signature profile';

function writeU32(memory, address, value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value >>> 0, false);
  bytes.forEach((byte, index) => memory.set(address + index, byte));
}

function makeHarness({ badResolver = false } = {}) {
  const memory = new Map();
  const button = { disabled: false };
  const input = { value: '815BA133' };
  const status = { textContent: '', dataset: {} };
  const output = { textContent: '', hidden: true };
  const elements = new Map([
    ['spawn-tag-resolver-btn', button],
    ['spawn-tag-resolver-input', input],
    ['spawn-tag-resolver-status', status],
    ['spawn-tag-resolver-output', output]
  ]);
  const document = { getElementById: id => elements.get(id) || null };
  const window = {
    RtmBuildCompatibility: { anchorCount: 5 },
    rtmActivitySessionMemory: {
      session: () => owner,
      async verify(session) {
        assert.equal(session, owner);
        return { profile, anchors: 5, capability: { session } };
      },
      async read(address, length, session, capability) {
        assert.equal(session, owner);
        assert.equal(capability.session, owner);
        if (badResolver && address === 0x0013B7E8) return '00'.repeat(length);
        return Array.from({ length }, (_, index) => (memory.get(address + index) || 0).toString(16).padStart(2, '0')).join('').toUpperCase();
      }
    }
  };

  const signatureBytes = Buffer.from(resolverSignature, 'hex');
  signatureBytes.forEach((byte, index) => memory.set(0x0013B7E8 + index, byte));

  const handle = 0x815BA133;
  const shifted = (handle | 0) >> 13;
  const bucketMask = ((shifted >>> 18) | 0x3ff) >>> 0;
  const bucket = (shifted & bucketMask) >>> 0;
  const slot = handle & 0x1fff;
  assert.equal(bucket, 0x0add);
  assert.equal(slot, 0x0133);
  const root = 0x01a00000;
  const descriptor = root + bucket * 0x40;
  const storage = 0x02000000;
  const stride = 0x10;
  writeU32(memory, 0x01896d50, root);
  writeU32(memory, descriptor + 0x10, storage);
  writeU32(memory, descriptor + 0x30, stride);
  writeU32(memory, descriptor + 0x34, 0xffffffff);
  writeU32(memory, storage + slot * stride + 8, 0);

  vm.runInNewContext(source, { window, document });
  return { window, button, status, output, memory };
}

test('tag probe checks the BLES packed-handle function and reproduces its address arithmetic', async () => {
  const h = makeHarness();
  await h.window.inspectSpawnTagResolver();
  assert.equal(h.status.dataset.state, 'matched');
  assert.equal(h.button.disabled, false);
  const result = JSON.parse(h.output.textContent);
  assert.deepEqual(JSON.parse(JSON.stringify(result.resolver_function)), {
    address: '0x0013B7E8',
    signature_bytes: 100,
    signature_matches_before_and_after: true
  });
  assert.equal(result.decode.bucket, 0x0add);
  assert.equal(result.decode.slot, 0x0133);
  assert.equal(result.resolved_pointer_candidate, '0x02001330');
  assert.match(result.note, /does not validate object class/);
});

test('tag probe refuses a different resolver body before reading the handle table', async () => {
  const h = makeHarness({ badResolver: true });
  await h.window.inspectSpawnTagResolver();
  assert.equal(h.status.dataset.state, 'error');
  assert.match(h.status.textContent, /packed-handle resolver signature did not match/);
  assert.equal(h.output.hidden, true);
});
