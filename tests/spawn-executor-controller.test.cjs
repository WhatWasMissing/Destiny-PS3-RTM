const test = require('node:test');
const assert = require('node:assert/strict');
const packet = require('../source/js/spawn-packet.js');
const dataBlock = require('../source/js/spawn-data-block.js');
const images = require('../source/js/spawn-executor-images.js');
const controllerApi = require('../source/js/spawn-executor-controller.js');

const PROFILE = controllerApi.PROFILE_NAME;
const OWNER = '1:192.168.0.62';

const signatures = [
  { name: 'a', address: 0x00100000, expected: 'AABBCCDD' },
  { name: 'b', address: 0x00100100, expected: 'AABBCCDD' },
  { name: 'c', address: 0x00100200, expected: 'AABBCCDD' },
  { name: 'hook', address: controllerApi.HOOK_ADDRESS, expected: controllerApi.HOOK_ORIGINAL, installedExpected: controllerApi.HOOK_INSTALLED },
  { name: 'e', address: 0x00100300, expected: 'AABBCCDD' },
  { name: 'f', address: 0x00100400, expected: 'AABBCCDD' },
  { name: 'g', address: 0x00100500, expected: 'AABBCCDD' }
];

const regions = [
  { name: 'stub and program', address: 0x0183e000, size: 0x2000 },
  { name: 'dispatcher and data pools', address: 0x01c74000, size: 0xc000 },
  { name: 'reserved gap', address: 0x0183d200, size: 0x0e00 }
];

function makeHarness({ emulateWorker = false, workerSlot = 0 } = {}) {
  const memory = new Map();
  const storageMap = new Map();
  let activeOwner = OWNER;
  const storage = {
    getItem: key => storageMap.get(key) ?? null,
    setItem: (key, value) => storageMap.set(key, value)
  };
  function readBytes(address, length) {
    return Uint8Array.from({ length }, (_, index) => memory.get(address + index) ?? 0);
  }
  function writeBytes(address, bytes) {
    for (let index = 0; index < bytes.length; index++) memory.set(address + index, bytes[index]);
  }
  function readWord(address) {
    return new DataView(readBytes(address, 4).buffer).getUint32(0, false);
  }
  function writeWord(address, value) {
    const bytes = new Uint8Array(4);
    new DataView(bytes.buffer).setUint32(0, value >>> 0, false);
    writeBytes(address, bytes);
  }
  for (const signature of signatures) writeBytes(signature.address, Buffer.from(signature.expected, 'hex'));

  const memoryApi = {
    session: () => activeOwner,
    async verify(owner) {
      assert.equal(owner, activeOwner);
      return { profile: PROFILE, anchors: 3, capability: {} };
    },
    async read(address, length, owner) {
      assert.equal(owner, activeOwner);
      const workerAddress = controllerApi.DATA_BASE + controllerApi.COMMAND_OFFSET + workerSlot * controllerApi.COMMAND_STRIDE;
      if (emulateWorker && address === workerAddress && length >= 0x1c && readWord(workerAddress) === 1) {
        const slot = workerAddress;
        const count = readWord(slot + 0x9c);
        const results = new Uint8Array(0x40 + count * 0x10);
        results[0] = 3;
        results[1] = 0;
        new DataView(results.buffer).setUint32(0x40, 0x80abcdef, false);
        writeBytes(slot + controllerApi.RESULT_OFFSET, results);
        writeWord(slot + 0x18, 1);
        writeWord(slot, 3);
      }
      return controllerApi.bytesToHex(readBytes(address, length));
    }
  };

  const writer = async (owner, address, expected, replacement) => {
    assert.equal(owner, activeOwner);
    const before = Buffer.from(expected, 'hex');
    const after = Buffer.from(replacement, 'hex');
    assert.deepEqual(Buffer.from(readBytes(address, before.length)), before, `precondition at ${address.toString(16)}`);
    writeBytes(address, after);
    assert.deepEqual(Buffer.from(readBytes(address, after.length)), after, `readback at ${address.toString(16)}`);
  };

  const controller = controllerApi.create({
    memory: memoryApi,
    writeIfEqual: writer,
    signatures,
    regions,
    packet,
    dataBlock,
    images,
    storage,
    now: Date.now,
    sleep: async () => {}
  });
  return { controller, memory, readBytes, readWord, writeWord, storageMap, setSession: value => { activeOwner = value; } };
}

const spawnFields = {
  expectedType: -1,
  tag: 0x815ba133,
  placementMode94: 0,
  placementMode98: 0,
  count: 1,
  placementModeA0: 0,
  pose: [5, 1, 1, 0, 0, 0, 0, 0, 0]
};

test('installer persists a journal before writes and recovery restores/clears exact owned bytes', async () => {
  const h = makeHarness();
  const installed = await h.controller.install();
  assert.equal(installed.state, 'installed');
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.HOOK_ADDRESS, 4)), Buffer.from(controllerApi.HOOK_INSTALLED, 'hex'));
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.TASK_TABLE, 8)), Buffer.from('0183E40001C75000', 'hex'));
  assert.equal(h.storageMap.get(controllerApi.JOURNAL_KEY).includes('"state":"installed"'), true);

  const recovered = await h.controller.recover();
  assert.equal(recovered.state, 'recovered-clean');
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.HOOK_ADDRESS, 4)), Buffer.from(controllerApi.HOOK_ORIGINAL, 'hex'));
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.TASK_TABLE, 8)), Buffer.alloc(8));
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.BOOTSTRAP_BASE, 292)), Buffer.alloc(292));
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.PROGRAM_BASE, 2908)), Buffer.alloc(2908));
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.DATA_BASE, 0x1cd0)), Buffer.alloc(0x1cd0));
});

test('installer refuses an occupied executor range before writing or journaling', async () => {
  const h = makeHarness();
  h.memory.set(controllerApi.BOOTSTRAP_BASE, 0xaa);
  await assert.rejects(h.controller.install(), /scratch is occupied/);
  assert.equal(h.readBytes(controllerApi.BOOTSTRAP_BASE, 1)[0], 0xaa);
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.HOOK_ADDRESS, 4)), Buffer.from(controllerApi.HOOK_ORIGINAL, 'hex'));
  assert.equal(h.storageMap.has(controllerApi.JOURNAL_KEY), false);
});

test('recovery refuses to overwrite an unknown hook instruction', async () => {
  const h = makeHarness();
  await h.controller.install();
  h.memory.set(controllerApi.HOOK_ADDRESS, 0xde);
  await assert.rejects(h.controller.recover(), /signature mismatch/);
  assert.equal(h.readBytes(controllerApi.HOOK_ADDRESS, 1)[0], 0xde);
  assert.equal(h.readBytes(controllerApi.TASK_TABLE, 1)[0], 0x01);
});

test('recovery preserves a terminal request until its journaled result is reconciled', async () => {
  const h = makeHarness();
  await h.controller.install();
  const slot = controllerApi.DATA_BASE + controllerApi.COMMAND_OFFSET;
  const journal = JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY));
  journal.sequence = 9;
  journal.pendingCommand = { slotBase: slot, sequence: 9, count: 1 };
  h.storageMap.set(controllerApi.JOURNAL_KEY, JSON.stringify(journal));
  h.writeWord(slot, 3);
  h.writeWord(slot + 8, 9);

  await assert.rejects(h.controller.recover(), /reconcile its result/);
  const stillPending = JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY));
  assert.equal(stillPending.pendingCommand.sequence, 9);
  assert.equal(h.readWord(controllerApi.HOOK_ADDRESS), parseInt(controllerApi.HOOK_INSTALLED, 16));
});

test('spawn submits the native packet, parses result rows, and leaves executed images resident on recovery', async () => {
  const h = makeHarness({ emulateWorker: true });
  await h.controller.install();
  const result = await h.controller.spawn(spawnFields, { timeoutMs: 100, pollMs: 1 });
  assert.equal(result.terminalStatus, 3);
  assert.equal(result.rows.length, 1);
  assert.equal(result.rows[0].resultCode, 0x0300);
  assert.equal(result.rows[0].resultType, 3);
  assert.equal(result.rows[0].detail, 0);
  assert.equal(result.rows[0].interpretation.includes('ambiguous'), true);
  assert.equal(result.rows[0].handle, 0x80abcdef);
  assert.equal(result.successCount, 1);
  assert.equal(result.sequence, 1);

  const recovered = await h.controller.recover();
  assert.equal(recovered.state, 'recovered-resident');
  assert.deepEqual(Buffer.from(h.readBytes(controllerApi.HOOK_ADDRESS, 4)), Buffer.from(controllerApi.HOOK_ORIGINAL, 'hex'));
  assert.notDeepEqual(Buffer.from(h.readBytes(controllerApi.PROGRAM_BASE, 16)), Buffer.alloc(16));
  assert.equal(recovered.message.includes('restart Destiny'), true);
});

test('spawn skips a busy first slot and submits through the next reusable slot', async () => {
  const h = makeHarness({ emulateWorker: true, workerSlot: 1 });
  await h.controller.install();
  const firstSlot = controllerApi.DATA_BASE + controllerApi.COMMAND_OFFSET;
  const secondSlot = firstSlot + controllerApi.COMMAND_STRIDE;
  h.writeWord(firstSlot, 1);
  h.writeWord(firstSlot + 8, 44);

  const result = await h.controller.spawn(spawnFields, { timeoutMs: 100, pollMs: 1 });
  assert.equal(result.slot, secondSlot);
  assert.equal(result.sequence, 1);
  h.writeWord(firstSlot, 3);
  assert.equal((await h.controller.recover()).state, 'recovered-resident');
});

test('pending request can be reconciled after a late terminal completion', async () => {
  const h = makeHarness();
  await h.controller.install();
  const slot = controllerApi.DATA_BASE + controllerApi.COMMAND_OFFSET;
  const journal = JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY));
  journal.sequence = 7;
  journal.pendingCommand = { slotBase: slot, sequence: 7, count: 1 };
  h.storageMap.set(controllerApi.JOURNAL_KEY, JSON.stringify(journal));
  h.writeWord(slot, 3);
  h.writeWord(slot + 8, 7);
  h.writeWord(slot + 0x14, 1);
  h.writeWord(slot + 0x18, 1);
  const results = new Uint8Array(0x50);
  results[0] = 3;
  new DataView(results.buffer).setUint32(0x40, 0x80abcdef, false);
  for (let index = 0; index < results.length; index++) h.memory.set(slot + controllerApi.RESULT_OFFSET + index, results[index]);

  const result = await h.controller.reconcilePending();
  assert.equal(result.pending, false);
  assert.equal(result.terminalStatus, 3);
  assert.equal(result.successCount, 1);
  assert.equal(result.rows[0].handle, 0x80abcdef);
  assert.equal(JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY)).pendingCommand, null);
});

test('same-console reconnect can reconcile a pending spawn then recover its hook', async () => {
  const h = makeHarness();
  await h.controller.install();
  const slot = controllerApi.DATA_BASE + controllerApi.COMMAND_OFFSET;
  const journal = JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY));
  journal.sequence = 14;
  journal.spawnCount = 1;
  journal.pendingCommand = { slotBase: slot, sequence: 14, count: 1 };
  h.storageMap.set(controllerApi.JOURNAL_KEY, JSON.stringify(journal));
  h.writeWord(slot, 3);
  h.writeWord(slot + 8, 14);
  h.writeWord(slot + 0x18, 1);
  const result = new Uint8Array(0x50);
  result[0] = 3;
  new DataView(result.buffer).setUint32(0x40, 0x80abcdef, false);
  for (let index = 0; index < result.length; index++) h.memory.set(slot + controllerApi.RESULT_OFFSET + index, result[index]);

  h.setSession('2:192.168.0.62');
  const reconciled = await h.controller.reconcilePending();
  assert.equal(reconciled.pending, false);
  assert.equal(reconciled.rows[0].handle, 0x80abcdef);
  assert.equal(JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY)).pendingCommand, null);

  const recovered = await h.controller.recover();
  assert.equal(recovered.state, 'recovered-resident');
  assert.equal(h.readWord(controllerApi.HOOK_ADDRESS), parseInt(controllerApi.HOOK_ORIGINAL, 16));
});

test('cancellation checks the pending slot sequence before writing its flag', async () => {
  const h = makeHarness();
  await h.controller.install();
  const slot = controllerApi.DATA_BASE + controllerApi.COMMAND_OFFSET;
  const journal = JSON.parse(h.storageMap.get(controllerApi.JOURNAL_KEY));
  journal.sequence = 7;
  journal.pendingCommand = { slotBase: slot, sequence: 7, count: 1 };
  h.storageMap.set(controllerApi.JOURNAL_KEY, JSON.stringify(journal));
  h.writeWord(slot, 1);
  h.writeWord(slot + 8, 7);
  assert.equal((await h.controller.cancelCurrent()).requested, true);
  assert.equal(h.readWord(slot + 0x10), 1);

  h.writeWord(slot + 0x10, 0);
  h.writeWord(slot + 8, 8);
  await assert.rejects(h.controller.cancelCurrent(), /sequence changed/);
  assert.equal(h.readWord(slot + 0x10), 0);
});

test('result parser reads the raw big-endian result code and row words', () => {
  const bytes = new Uint8Array(0x40 + 0x10);
  bytes[0] = 3;
  bytes[1] = 0;
  new DataView(bytes.buffer).setUint32(0x40, 0x8087600f, false);
  new DataView(bytes.buffer).setUint32(0x44, 0x11223344, false);
  const [row] = controllerApi.parseResults(bytes, 1);
  assert.equal(row.resultCode, 0x0300);
  assert.equal(row.resultCodeHex, '0x0300');
  assert.equal(row.resultType, 3);
  assert.equal(row.detail, 0);
  assert.equal(row.handle, 0x8087600f);
  assert.equal(row.words[1], 0x11223344);
});
