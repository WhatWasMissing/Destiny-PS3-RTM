const test = require('node:test');
const assert = require('node:assert/strict');

function loadOverwallsTools({ writeSequence, syncDamage, readMemory, writeMemory }) {
  const reports = [];
  const status = { textContent: '' };
  const storage = new Map();
  const memory = new Map([
    [0x00444CA4, 'C04100F0'],
    [0x01891AE0, '37343036'],
    [0x0190EBB0, '01A00000']
  ]);
  const conditionalWrites = [];
  global.localStorage = {
    getItem: key => storage.get(key) ?? null,
    setItem: (key, value) => storage.set(key, value),
    removeItem: key => storage.delete(key)
  };
  global.document = {
    addEventListener: () => {},
    getElementById: id => id === 'ow-job-status' ? status : null
  };
  global.ip = '192.0.2.1';
  global.connectionGeneration = 1;
  global.ensureOperationSession = generation => {
    if (generation !== global.connectionGeneration) throw new Error('Console session changed');
  };
  global.floatToHex = value => {
    const buffer = new ArrayBuffer(4);
    new DataView(buffer).setFloat32(0, Number(value), false);
    return [...new Uint8Array(buffer)].map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
  };
  global.verifiedMemorySequence = writeSequence;
  global.verifiedConditionalMemorySequence = async (pairs, preconditions) => {
    for (const [address, expected] of preconditions) {
      const current = memory.get(parseInt(address, 16));
      if (current !== expected) throw new Error(`Conditional memory changed at ${address}`);
    }
    conditionalWrites.push(pairs);
    for (const [address, value] of pairs) memory.set(parseInt(address, 16), value);
    return { status: 'verified' };
  };
  global.ps3getmem = readMemory || (async (address, length) => memory.get(Number(address)) || '00'.repeat(length));
  global.verifiedMemoryValue = writeMemory || (async () => { throw new Error('Unexpected memory write'); });
  global.syncDamagePatchState = (...args) => syncDamage(...args);
  global.toast = (message, type) => reports.push([message, type]);
  global.log = (message, type) => reports.push([message, type]);

  const path = require.resolve('../source/js/overwalls-tools.js');
  delete require.cache[path];
  require(path);
  return { reports, status, memory, conditionalWrites, storage };
}

test('Overwalls One-Hit toggle syncs the shared panel after verified writes', async () => {
  const writes = [];
  const synced = [];
  loadOverwallsTools({
    writeSequence: async pairs => { writes.push(pairs); },
    syncDamage: (...args) => synced.push(args)
  });

  await global.owApplyCheatToggle(9, true);
  assert.deepEqual(writes[0], [['0043E626', '547A']]);
  assert.deepEqual(synced, [['547A', 'Overwalls One-Hit toggle']]);

  await global.owApplyCheatToggle(9, false);
  assert.deepEqual(writes[1], [['0043E626', '3F80']]);
  assert.deepEqual(synced[1], ['3F80', 'Overwalls One-Hit toggle']);
});

test('Overwalls presets sync damage only after the full preset verifies', async () => {
  const synced = [];
  loadOverwallsTools({
    writeSequence: async () => {},
    syncDamage: (...args) => synced.push(args)
  });

  await global.owApplyCheatPreset('usual');
  assert.deepEqual(synced, [['547A', 'Overwalls usual preset']]);

  await global.owApplyCheatPreset('off');
  assert.deepEqual(synced[1], ['3F80', 'Overwalls off preset']);
});

test('failed Overwalls readback does not replace the displayed damage state', async () => {
  const synced = [];
  const { status } = loadOverwallsTools({
    writeSequence: async () => { throw new Error('readback mismatch'); },
    syncDamage: (...args) => synced.push(args)
  });

  await global.owApplyCheatToggle(9, true);
  assert.deepEqual(synced, []);
  assert.match(status.textContent, /not confirmed/);
});

test('Overwalls Warpriest variant restores its source-matched primary instruction', async () => {
  const { conditionalWrites, memory } = loadOverwallsTools({
    writeSequence: async () => {},
    syncDamage: () => {}
  });

  await global.owApplyCheatToggle(4, true);
  assert.deepEqual(conditionalWrites[0], [['00444CA4', 'C0420348'], ['01891AE0', '3F800000']]);
  assert.equal(memory.get(0x00444CA4), 'C0420348');
  assert.equal(memory.get(0x01891AE0), '3F800000');

  await global.owApplyCheatToggle(4, false);
  assert.deepEqual(conditionalWrites[1], [['00444CA4', 'C04100F0'], ['01891AE0', '37343036']]);
  assert.equal(memory.get(0x00444CA4), 'C04100F0');
  assert.equal(memory.get(0x01891AE0), '37343036');
});

test('Overwalls plate hook refuses an unknown branch site before writing', async () => {
  const writes = [];
  const reads = [];
  const { reports } = loadOverwallsTools({
    readMemory: async (address, length) => {
      reads.push([address, length]);
      return '6000000060000000';
    },
    writeMemory: async (...args) => { writes.push(args); },
    writeSequence: async () => {},
    syncDamage: () => {}
  });

  await global.owInstallPlateHook();
  assert.deepEqual(reads, [[0x00A015AC, 8]]);
  assert.deepEqual(writes, []);
  assert.match(reports.at(-1)[0], /Unsupported plate-hook instruction pair/);
});

test('Overwalls plate hook requires the pristine branch pair, empty cave, and clear flag', async () => {
  const writes = [];
  const reads = new Map([
    [0x00A015AC, '3063FFF44BFFFB15'],
    [0x010FB514, '00'.repeat(104)],
    [0x0190EC98, '00000000']
  ]);
  loadOverwallsTools({
    readMemory: async address => reads.get(address),
    writeMemory: async (...args) => { writes.push(args); },
    writeSequence: async () => {},
    syncDamage: () => {}
  });

  await global.owInstallPlateHook();
  assert.deepEqual(writes.map(([address]) => address), ['010FB514', '0190EC98', '00A015AC']);
});

test('Overwalls plate hook preserves an occupied cave or active state flag', async t => {
  for (const scenario of [
    { name: 'occupied cave', body: 'AA'.repeat(104), flag: '00000000', message: /code cave is not empty/ },
    { name: 'active flag', body: '00'.repeat(104), flag: '00000001', message: /state flag is already in use/ }
  ]) {
    await t.test(scenario.name, async () => {
      const writes = [];
      const { reports } = loadOverwallsTools({
        readMemory: async address => new Map([
          [0x00A015AC, '3063FFF44BFFFB15'],
          [0x010FB514, scenario.body],
          [0x0190EC98, scenario.flag]
        ]).get(address),
        writeMemory: async (...args) => { writes.push(args); },
        writeSequence: async () => {},
        syncDamage: () => {}
      });

      await global.owInstallPlateHook();
      assert.deepEqual(writes, []);
      assert.match(reports.at(-1)[0], scenario.message);
    });
  }
});
