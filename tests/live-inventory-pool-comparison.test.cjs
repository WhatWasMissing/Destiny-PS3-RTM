const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

function loadApi() {
  const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'live-inventory-pool.js'), 'utf8');
  const window = { addEventListener() {} };
  const document = { readyState: 'loading', addEventListener() {}, getElementById: () => null };
  vm.runInNewContext(source, { window, document });
  return window.rtmLiveInventoryPool;
}

test('live pool comparison reports added, removed, changed and unchanged slots only', () => {
  const api = loadApi();
  const before = [
    { slot: 2, itemIndex: 44, instanceId: 'must not escape' },
    { slot: 5, itemIndex: 77 },
    { slot: 8, itemIndex: 88 }
  ];
  const after = [
    { slot: 2, itemIndex: 44, instanceId: 'different hidden instance' },
    { slot: 5, itemIndex: 78 },
    { slot: 9, itemIndex: 99 }
  ];
  const result = JSON.parse(JSON.stringify(api.compareRows(before, after)));
  assert.deepEqual(result, {
    changes: [
      { slot: 5, change: 'Changed', before: { slot: 5, itemIndex: 77 }, after: { slot: 5, itemIndex: 78 } },
      { slot: 8, change: 'Removed', before: { slot: 8, itemIndex: 88 }, after: null },
      { slot: 9, change: 'Added', before: null, after: { slot: 9, itemIndex: 99 } }
    ],
    unchangedCount: 1
  });
  assert.equal(JSON.stringify(result).includes('instanceId'), false);
});

test('live pool comparison treats identical slot and item index as unchanged', () => {
  const api = loadApi();
  assert.deepEqual(
    JSON.parse(JSON.stringify(api.compareRows([{ slot: 0, itemIndex: 12 }], [{ slot: 0, itemIndex: 12 }]))),
    { changes: [], unchangedCount: 1 }
  );
});

test('live pool comparison rejects duplicate or malformed rows', () => {
  const api = loadApi();
  assert.throws(() => api.compareRows([{ slot: 1, itemIndex: 1 }, { slot: 1, itemIndex: 2 }]), /Duplicate/);
  assert.throws(() => api.compareRows([{ slot: -1, itemIndex: 1 }]), /nonnegative slot/);
  assert.throws(() => api.compareRows([{ slot: 1, itemIndex: NaN }]), /integer item index/);
});
