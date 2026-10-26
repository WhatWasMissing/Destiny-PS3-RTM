const test = require('node:test');
const assert = require('node:assert/strict');

function loadOverwallsTools({ writeSequence, syncDamage }) {
  const reports = [];
  const status = { textContent: '' };
  global.localStorage = { getItem: () => null, setItem: () => {}, removeItem: () => {} };
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
  global.syncDamagePatchState = (...args) => syncDamage(...args);
  global.toast = (message, type) => reports.push([message, type]);
  global.log = (message, type) => reports.push([message, type]);

  const path = require.resolve('../source/js/overwalls-tools.js');
  delete require.cache[path];
  require(path);
  return { reports, status };
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
