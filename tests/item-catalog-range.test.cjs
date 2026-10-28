const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

function loadItemCatalogBuilders() {
  const file = path.join(__dirname, '..', 'source', 'js', 'local-catalogs.js');
  const source = fs.readFileSync(file, 'utf8');
  const marker = '  window.localInventoryItemName = localItemName;';
  assert.ok(source.includes(marker), 'production catalog module exposes its inventory lookup');
  const instrumented = source.replace(marker, `  window.__testBuildItemNameMap = buildItemNameMap;\n  window.__testBuildItemMetadataMap = buildItemMetadataMap;\n${marker}`);
  const window = {};
  const document = { readyState: 'complete', getElementById: () => null };
  vm.runInNewContext(instrumented, { window, document });
  return { buildItemNameMap: window.__testBuildItemNameMap, buildItemMetadataMap: window.__testBuildItemMetadataMap };
}

test('BLES 01.24 live item labels accept only the verified 0–6556 index range', () => {
  const { buildItemNameMap } = loadItemCatalogBuilders();
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
  const { buildItemNameMap } = loadItemCatalogBuilders();
  const rows = [new Map([['itemIndex', '710'], ['name', 'Striker']])];
  assert.equal(buildItemNameMap('general', ['itemIndex', 'name'], rows), null);
  assert.equal(buildItemNameMap('item-loot', ['tag', 'name'], rows), null);
});

test('BLES-indexed item catalogs preserve optional bucket and type labels without joining tags', () => {
  const { buildItemMetadataMap } = loadItemCatalogBuilders();
  const metadata = buildItemMetadataMap('item-loot', ['itemIndex', 'name', 'bucket', 'type', 'tag'], [
    new Map([['itemIndex', '9'], ['name', 'Prototype 0.9b'], ['bucket', 'Gauntlets'], ['type', 'Gauntlets'], ['tag', '80A5F3A5']]),
    new Map([['itemIndex', '10'], ['name', 'Prototype 0.9b'], ['bucket', ''], ['type', 'Trials Reward'], ['tag', '80A5F3A6']]),
    new Map([['itemIndex', '6557'], ['name', 'Out of range'], ['bucket', 'Invalid'], ['type', 'Invalid']])
  ]);

  assert.equal(JSON.stringify(Array.from(metadata.entries())), JSON.stringify([
    [9, { name: 'Prototype 0.9b', bucket: 'Gauntlets', type: 'Gauntlets' }],
    [10, { name: 'Prototype 0.9b', type: 'Trials Reward' }]
  ]));
  assert.equal(buildItemMetadataMap('item-loot', ['def', 'name', 'bucket', 'type', 'tag'], [
    new Map([['def', '9'], ['name', 'Prototype 0.9b'], ['bucket', 'Gauntlets'], ['type', 'Gauntlets'], ['tag', '80A5F3A5']])
  ]), null);
});
