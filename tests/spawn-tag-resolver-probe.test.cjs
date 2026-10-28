const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'spawn-tag-resolver-probe.js'), 'utf8');
const resolverSignature = source.match(/const HANDLE_RESOLVER_SIGNATURE = '([0-9A-F]+)'/)[1];
const typeCopySignature = source.match(/const CONSTRUCTOR_TYPE_COPY_SIGNATURE = '([0-9A-F]+)'/)[1];
const owner = '1:192.168.1.100';
const profile = 'BLES01857 01.24 signature profile';

function writeU32(memory, address, value) {
  const bytes = new Uint8Array(4);
  new DataView(bytes.buffer).setUint32(0, value >>> 0, false);
  bytes.forEach((byte, index) => memory.set(address + index, byte));
}

function makeHarness({ badResolver = false, badTypeCopy = false, expectedType = -1, changeTypeDuringRead = false } = {}) {
  const memory = new Map();
  const readCounts = new Map();
  const button = { disabled: false };
  const input = { value: '815BA133' };
  const expectedTypeInput = { value: String(expectedType) };
  const status = { textContent: '', dataset: {} };
  const output = { textContent: '', hidden: true };
  const elements = new Map([
    ['spawn-tag-resolver-btn', button],
    ['spawn-tag-resolver-input', input],
    ['spawn-expected-type', expectedTypeInput],
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
        if (badTypeCopy && address === 0x0013EE1C) return '00'.repeat(length);
        if (changeTypeDuringRead && address === 0x0200137E) {
          const reads = readCounts.get(address) || 0;
          readCounts.set(address, reads + 1);
          return reads === 0 ? '09' : '08';
        }
        return Array.from({ length }, (_, index) => (memory.get(address + index) || 0).toString(16).padStart(2, '0')).join('').toUpperCase();
      }
    }
  };

  const signatureBytes = Buffer.from(resolverSignature, 'hex');
  signatureBytes.forEach((byte, index) => memory.set(0x0013B7E8 + index, byte));
  const typeCopyBytes = Buffer.from(typeCopySignature, 'hex');
  typeCopyBytes.forEach((byte, index) => memory.set(0x0013EE1C + index, byte));

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
  memory.set(0x0200137E, 9);

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
  assert.deepEqual(JSON.parse(JSON.stringify(result.constructor_definition_type)), {
    address: '0x0200137E',
    field_offset: '0x4E',
    value: 9,
    signature_address: '0x0013EE1C',
    signature_bytes: 16,
    signature_matches_before_and_after: true,
    copied_to_created_object_offset: '0x08',
    expected_type_guard: { value: -1, enabled: false, matches: null }
  });
  assert.match(result.note, /does not establish enemy identity/);
});

test('tag probe compares the live constructor byte with the selected optional guard', async () => {
  const matching = makeHarness({ expectedType: 9 });
  await matching.window.inspectSpawnTagResolver();
  let result = JSON.parse(matching.output.textContent);
  assert.deepEqual(JSON.parse(JSON.stringify(result.constructor_definition_type.expected_type_guard)), {
    value: 9, enabled: true, matches: true
  });

  const mismatching = makeHarness({ expectedType: 8 });
  await mismatching.window.inspectSpawnTagResolver();
  result = JSON.parse(mismatching.output.textContent);
  assert.deepEqual(JSON.parse(JSON.stringify(result.constructor_definition_type.expected_type_guard)), {
    value: 8, enabled: true, matches: false
  });
});

test('tag probe refuses a different resolver body before reading the handle table', async () => {
  const h = makeHarness({ badResolver: true });
  await h.window.inspectSpawnTagResolver();
  assert.equal(h.status.dataset.state, 'error');
  assert.match(h.status.textContent, /packed-handle resolver signature did not match/);
  assert.equal(h.output.hidden, true);
});

test('tag probe refuses a different constructor type-copy body', async () => {
  const h = makeHarness({ badTypeCopy: true });
  await h.window.inspectSpawnTagResolver();
  assert.equal(h.status.dataset.state, 'error');
  assert.match(h.status.textContent, /constructor type-copy signature did not match/);
  assert.equal(h.output.hidden, true);
});

test('tag probe discards a definition type byte that changes during the sample', async () => {
  const h = makeHarness({ changeTypeDuringRead: true });
  await h.window.inspectSpawnTagResolver();
  assert.equal(h.status.dataset.state, 'error');
  assert.match(h.status.textContent, /definition type byte changed/);
  assert.equal(h.output.hidden, true);
});
