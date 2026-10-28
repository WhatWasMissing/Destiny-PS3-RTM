const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

function loadItemCatalogMapBuilder() {
  const file = path.join(__dirname, '..', 'source', 'js', 'local-catalogs.js');
  const source = fs.readFileSync(file, 'utf8');
  const marker = '  window.localInventoryItemName = localItemName;';
  assert.ok(source.includes(marker), 'production catalog module exposes its inventory lookup');
  const instrumented = source.replace(marker, `  window.__testBuildItemNameMap = buildItemNameMap;\n${marker}`);
  const window = {};
  const document = { readyState: 'complete', getElementById: () => null };
  vm.runInNewContext(instrumented, { window, document });
  return window.__testBuildItemNameMap;
}

test('BLES 01.24 live item labels accept only the verified 0–6556 index range', () => {
  const buildItemNameMap = loadItemCatalogMapBuilder();
  const names = buildItemNameMap('item-loot', ['itemIndex', 'name'], [
    new Map([['itemIndex', '0'], ['name', 'Zero']]),
    new Map([['itemIndex', '6556'], ['name', 'Generalist Shell']]),
    new Map([['itemIndex', '6557'], ['name', 'Out of range']]),
    new Map([['itemIndex', '32767'], ['name', 'Signed maximum']])
  ]);

  assert.equal(names.get(0), 'Zero');
  assert.equal(names.get(6556), 'Generalist Shell');
  assert.equal(names.has(6557), false);
  assert.equal(names.has(32767), false);
});

test('live BLES item labels require an item catalog and recognized index/name columns', () => {
  const buildItemNameMap = loadItemCatalogMapBuilder();
  const rows = [new Map([['itemIndex', '710'], ['name', 'Striker']])];
  assert.equal(buildItemNameMap('general', ['itemIndex', 'name'], rows), null);
  assert.equal(buildItemNameMap('item-loot', ['tag', 'name'], rows), null);
});
