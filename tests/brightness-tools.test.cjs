const test = require('node:test');
const assert = require('node:assert/strict');

global.RtmMemoryFeatures = require('../source/js/memory-features.js');
const elements = new Map();
for (const id of ['brightness-current', 'brightness-read-btn', 'brightness-preset-bright', 'brightness-preset-normal', 'brightness-preset-inverted', 'brightness-status', 'status-badge']) {
  elements.set(id, { textContent: '', disabled: false, dataset: {}, classList: { contains: () => false } });
}
elements.get('status-badge').classList = { contains: state => state === 'connected' };
global.document = { getElementById: id => elements.get(id) || null };
require('../source/js/brightness-tools.js');

test('brightness candidate probe reads only and reports source preset matches cautiously', async () => {
  global.ip = '';
  global.connectionGeneration = 0;
  let reads = 0;
  let writes = 0;
  global.ps3getmem = async (address, length) => {
    reads++;
    assert.equal(address, 0x006101C0);
    assert.equal(length, 4);
    return '3F800000';
  };
  global.ps3setmem = async () => { writes++; throw new Error('probe must not write'); };

  await global.readBrightnessCandidate();
  assert.equal(reads, 0);
  assert.match(elements.get('brightness-status').textContent, /Connect/);

  global.ip = '192.0.2.1';
  global.connectionGeneration = 1;
  await global.readBrightnessCandidate();
  assert.equal(reads, 1);
  assert.equal(writes, 0);
  assert.match(elements.get('brightness-current').textContent, /3F800000/);
  assert.match(elements.get('brightness-status').textContent, /source “Normal” value/);
  assert.match(elements.get('brightness-status').textContent, /01\.24 ELF stores Normal \(1\.0\)/);
  assert.match(elements.get('brightness-status').textContent, /does not verify the in-game effect/);
  assert.doesNotMatch(elements.get('brightness-status').textContent, /executable code/);

  global.ps3getmem = async () => '00000000';
  await global.readBrightnessCandidate();
  assert.equal(reads, 1);
  assert.equal(writes, 0);
  assert.match(elements.get('brightness-status').textContent, /Does not match any listed source preset/);
  assert.match(elements.get('brightness-status').textContent, /01\.24 ELF stores Normal/);
});

test('brightness probe discards a read if the console session changes', async () => {
  global.ip = '192.0.2.2';
  global.connectionGeneration = 4;
  global.ps3getmem = async () => {
    global.connectionGeneration++;
    return '42480000';
  };

  await global.readBrightnessCandidate();
  assert.equal(elements.get('brightness-current').textContent, 'Current word: not read');
  assert.match(elements.get('brightness-status').textContent, /result discarded/);
});

test('brightness presets write source values at the 01.24 candidate and show exact verification status', async () => {
  global.ip = '192.0.2.3';
  global.connectionGeneration = 5;
  const writes = [];
  global.verifiedMemoryValue = async (address, value) => {
    writes.push([address, value]);
  };

  for (const [name, hex] of [['bright', '42480000'], ['normal', '3F800000'], ['inverted', 'C1A00000']]) {
    await global.applyBrightnessPreset(name);
    assert.deepEqual(writes.at(-1), ['006101C0', hex]);
    assert.match(elements.get('brightness-current').textContent, new RegExp(hex));
    assert.match(elements.get('brightness-status').textContent, /write verified/);
    assert.match(elements.get('brightness-status').textContent, /effect has not been observed/);
  }
  assert.equal(writes.length, 3);
  assert.ok([...elements.values()].every(element => !element.disabled));
});

test('brightness preset reports a failed verified write without claiming success', async () => {
  global.ip = '192.0.2.4';
  global.connectionGeneration = 6;
  global.verifiedMemoryValue = async () => { throw new Error('readback mismatch'); };

  await global.applyBrightnessPreset('bright');
  assert.equal(elements.get('brightness-current').textContent, 'Current word: write not verified');
  assert.match(elements.get('brightness-status').textContent, /Brightness write failed: readback mismatch/);
  assert.doesNotMatch(elements.get('brightness-status').textContent, /write verified/);
});
