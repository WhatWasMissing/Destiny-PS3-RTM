const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

function loadSnapshotApi() {
  const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'offline-inventory-snapshot.js'), 'utf8');
  const window = {};
  const document = { readyState: 'complete', getElementById: () => null };
  vm.runInNewContext(source, { window, document });
  return window.rtmOfflineInventorySnapshot;
}

function characterPayload(api, rows = []) {
  const buffer = new ArrayBuffer(api.characterFileBytes);
  const view = new DataView(buffer);
  view.setUint32(0, 0xEB4B8439, false);
  view.setUint32(12, api.characterFileBytes - 0x10, false);
  const inventoryStart = 0x10 + 0x30 + 0x08;
  for (const row of rows) {
    const offset = inventoryStart + row.slot * 0x20;
    view.setInt16(offset, row.itemIndex, false);
    view.setUint32(offset + 0x08, row.instanceWord || 1, false);
    view.setUint32(offset + 0x0C, row.instanceTail || 0, false);
  }
  return buffer;
}

test('character decoder validates the BLES 01.24 profile and never returns instance IDs', () => {
  const api = loadSnapshotApi();
  const rows = api.decodeCharacterBuffer(characterPayload(api, [{ slot: 43, itemIndex: 451, instanceWord: 0x12345678, instanceTail: 0x87654321 }]));
  assert.deepEqual(JSON.parse(JSON.stringify(rows)), [{ slot: 43, equippedLabel: 'Helmet', itemIndex: 451 }]);
  assert.throws(() => api.decodeCharacterBuffer(new ArrayBuffer(1)), /Expected exactly/);
  const invalidHeader = characterPayload(api);
  new DataView(invalidHeader).setUint32(0, 0, false);
  assert.throws(() => api.decodeCharacterBuffer(invalidHeader), /type hash/);
});

test('character comparison reports slot additions, removals, item changes and unchanged rows', () => {
  const api = loadSnapshotApi();
  const before = api.decodeCharacterBuffer(characterPayload(api, [
    { slot: 1, itemIndex: 7 },
    { slot: 3, itemIndex: 9 },
    { slot: 5, itemIndex: 10 }
  ]));
  const after = api.decodeCharacterBuffer(characterPayload(api, [
    { slot: 1, itemIndex: 7, instanceWord: 0xCAFE },
    { slot: 3, itemIndex: 11 },
    { slot: 4, itemIndex: 6 }
  ]));
  const result = api.compareCharacterRows(before, after);
  assert.deepEqual(JSON.parse(JSON.stringify(result)), {
    changes: [
      { slot: 3, change: 'Changed', before: { slot: 3, equippedLabel: '', itemIndex: 9 }, after: { slot: 3, equippedLabel: '', itemIndex: 11 } },
      { slot: 4, change: 'Added', before: null, after: { slot: 4, equippedLabel: '', itemIndex: 6 } },
      { slot: 5, change: 'Removed', before: { slot: 5, equippedLabel: '', itemIndex: 10 }, after: null }
    ],
    unchangedCount: 1
  });
});
