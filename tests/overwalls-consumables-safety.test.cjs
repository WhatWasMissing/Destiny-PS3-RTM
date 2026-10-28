const assert = require('node:assert/strict');
const path = require('node:path');
const test = require('node:test');

function loadOverwalls({ profileCheck, memoryRead } = {}) {
  const storage = new Map();
  const reads = [];
  const writes = [];
  const messages = [];
  const status = { textContent: '' };
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
    const bytes = new Uint8Array(4);
    new DataView(bytes.buffer).setFloat32(0, Number(value), false);
    return [...bytes].map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
  };
  global.ensureDestinyWriteProfile = profileCheck || (async () => {});
  global.ps3getmem = async (address, length) => {
    reads.push([Number(address), length]);
    return memoryRead ? memoryRead(address, length) : '00'.repeat(length);
  };
  global.verifiedMemoryValue = async (address, value) => {
    writes.push([address, value]);
    return value;
  };
  global.verifiedMemorySequence = async pairs => {
    writes.push(...pairs);
    return { status: 'verified' };
  };
  global.verifiedConditionalMemorySequence = async () => ({ status: 'verified' });
  global.toast = (message, kind) => messages.push([message, kind]);
  global.log = (message, kind) => messages.push([message, kind]);

  const modulePath = path.join(__dirname, '..', 'source', 'js', 'overwalls-tools.js');
  delete require.cache[require.resolve(modulePath)];
  require(modulePath);
  return { reads, writes, messages, status, storage };
}

test('randomize consumables refuses to read or write its source-derived table when the BLES profile fails', async () => {
  let profileChecks = 0;
  const state = loadOverwalls({ profileCheck: async () => { profileChecks++; throw new Error('profile mismatch'); } });

  await global.owRandomizeConsumables();

  assert.equal(profileChecks, 1);
  assert.deepEqual(state.reads, []);
  assert.deepEqual(state.writes, []);
  assert.equal(state.storage.has('overwalls_consumable_backup'), false);
  assert.match(state.status.textContent, /profile mismatch/);
});

test('randomize consumables checks the BLES profile before reading and journaling its 20 rows', async () => {
  let profileChecks = 0;
  const state = loadOverwalls({ profileCheck: async () => { profileChecks++; } });

  await global.owRandomizeConsumables();

  assert.equal(profileChecks, 1);
  assert.equal(state.reads.length, 40);
  assert.equal(state.writes.length, 40);
  assert.equal(state.writes[0][0], '600EAE60');
  assert.equal(state.writes[1][0], '600EAE70');
  const backup = JSON.parse(state.storage.get('overwalls_consumable_backup'));
  assert.equal(backup.original.length, 40);
  assert.equal(backup.changed.length, 40);
});

test('consumable restore keeps its journal and does not read the target table if the BLES profile fails', async () => {
  let profileChecks = 0;
  const state = loadOverwalls({ profileCheck: async () => { profileChecks++; throw new Error('profile mismatch'); } });
  const backup = { ip: '192.0.2.1', original: [['600EAE60', '0000']], changed: [['600EAE60', '1234']] };
  state.storage.set('overwalls_consumable_backup', JSON.stringify(backup));

  await global.owRestoreConsumables();

  assert.equal(profileChecks, 1);
  assert.deepEqual(state.reads, []);
  assert.deepEqual(state.writes, []);
  assert.deepEqual(JSON.parse(state.storage.get('overwalls_consumable_backup')), backup);
  assert.match(state.status.textContent, /profile mismatch/);
});
