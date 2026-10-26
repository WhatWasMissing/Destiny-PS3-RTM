const test = require('node:test');
const assert = require('node:assert/strict');

global.RtmMemoryFeatures = require('../source/js/memory-features.js');
const elements = new Map();
for (const id of ['brightness-current', 'brightness-read-btn', 'brightness-status', 'status-badge']) {
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
  assert.match(elements.get('brightness-status').textContent, /Probe only/);
  assert.match(elements.get('brightness-status').textContent, /does not prove the address is a brightness field/);
  assert.doesNotMatch(elements.get('brightness-status').textContent, /executable code/);

  global.ps3getmem = async () => '00000000';
  await global.readBrightnessCandidate();
  assert.equal(reads, 1);
  assert.equal(writes, 0);
  assert.match(elements.get('brightness-status').textContent, /Does not match any listed source preset/);
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
