const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');

require('../source/js/scenario-library.js');
const library = global.DESTINY_RTM_SCENARIO_LIBRARY;

function loadOverwalls(initialScenario = [], confirmResult = true) {
  const values = new Map([['overwalls_scenario_positions', JSON.stringify(initialScenario)]]);
  const messages = [];
  const nodes = {
    'ow-scenario-library': { value: 'archive-crota-s-end', innerHTML: '' },
    'ow-scenario-list': { innerHTML: '' },
    'ow-job-status': { textContent: '' }
  };
  global.localStorage = {
    getItem: key => values.get(key) ?? null,
    setItem: (key, value) => values.set(key, value),
    removeItem: key => values.delete(key)
  };
  global.document = { addEventListener() {}, getElementById: id => nodes[id] || null };
  global.floatToHex = value => {
    const buffer = new ArrayBuffer(4);
    new DataView(buffer).setFloat32(0, Number(value), false);
    return [...new Uint8Array(buffer)].map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
  };
  global.ip = '192.0.2.1';
  global.connectionGeneration = 1;
  global.ensureOperationSession = () => {};
  global.verifiedMemorySequence = async () => {};
  global.ps3getmem = async () => { throw new Error('Unexpected memory read'); };
  global.verifiedMemoryValue = async () => { throw new Error('Unexpected memory write'); };
  global.syncDamagePatchState = () => {};
  global.RtmDialogs = { confirm: async message => { messages.push(message); return confirmResult; } };
  global.toast = (message, type) => messages.push([message, type]);
  global.log = () => {};

  const path = require.resolve('../source/js/overwalls-tools.js');
  delete require.cache[path];
  require(path);
  return { values, messages, nodes };
}

test('bundled Overwalls library contains all 19 archived scenarios and 276 positions', () => {
  assert.equal(library.length, 19);
  assert.equal(library.reduce((total, entry) => total + entry.positions.length, 0), 276);
  for (const entry of library) {
    assert.ok(entry.name);
    assert.ok(entry.sourceFile.endsWith('.xml'));
    assert.ok(entry.positions.length > 0 && entry.positions.length <= 500);
    for (const position of entry.positions) {
      assert.ok(position.legacyName);
      assert.ok(position.name);
      for (const axis of ['x', 'y', 'z']) {
        const value = Number(position[axis]);
        assert.ok(Number.isFinite(value), `${entry.name}: ${position.name} ${axis}`);
        assert.ok(Number.isFinite(Math.fround(value)), `${entry.name}: ${position.name} float32 ${axis}`);
      }
    }
  }
});

test('the embedded page loads the archive before Overwalls tools and exposes its selector', () => {
  const html = fs.readFileSync(require.resolve('../source/index.html'), 'utf8');
  assert.ok(html.indexOf('js/scenario-library.js') < html.indexOf('js/overwalls-tools.js'));
  assert.match(html, /id="ow-scenario-library"/);
  assert.match(html, /onclick="owLoadScenarioLibrary\(\)"/);
});

test('loading an archive scenario confirms replacement and persists all named coordinates', async () => {
  const h = loadOverwalls([{ name: 'Existing', x: 1, y: 2, z: 3 }]);
  await global.owLoadScenarioLibrary();

  assert.match(h.messages[0], /Replace the current Overwalls scenario with/);
  const saved = JSON.parse(h.values.get('overwalls_scenario_positions'));
  assert.equal(saved.length, 13);
  assert.equal(saved[0].name, 'Crystal');
  assert.equal(saved[0].legacyName, 'button58');
  assert.equal(saved[0].x, -695.3969);
  assert.equal(saved.at(-1).name, 'Start');
  assert.match(h.nodes['ow-scenario-list'].innerHTML, /Deathsinger Start/);
});

test('declining archive replacement leaves the current scenario untouched', async () => {
  const current = [{ name: 'Keep me', x: 10, y: 20, z: 30 }];
  const h = loadOverwalls(current, false);
  await global.owLoadScenarioLibrary();
  assert.deepEqual(JSON.parse(h.values.get('overwalls_scenario_positions')), current);
});
