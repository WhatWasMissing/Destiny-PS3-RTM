const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

function loadCatalogApi() {
  const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'local-catalogs.js'), 'utf8');
  const window = {};
  const document = { readyState: 'complete', getElementById: () => null };
  vm.runInNewContext(source, { window, document });
  return window.rtmLocalSpawnCatalogs;
}

test('spawn catalog extraction accepts recognized tag columns and preserves bounded numeric types as unverified metadata', () => {
  const api = loadCatalogApi();
  const candidates = api.extractCandidates([{
    fileName: 'spawnable.tsv',
    category: 'spawn-hopon',
    columns: ['tag', 'type', 'family', 'candidates'],
    rows: [
      new Map([['tag', '815ba133'], ['type', '9'], ['family', 'vex'], ['candidates', 'Vex Minotaur']]),
      new Map([['tag', '0x8087600F'], ['type', '24'], ['family', '']]),
      new Map([['tag', 'not-a-tag'], ['candidates', 'Ignored']])
    ]
  }]);

  assert.equal(JSON.stringify(candidates), JSON.stringify([
    { tag: '815BA133', label: 'Vex Minotaur', source: 'spawnable.tsv', row: 1, catalogType: 9 },
    { tag: '8087600F', label: 'Catalog row 2', source: 'spawnable.tsv', row: 2, catalogType: 24 }
  ]));
  assert.equal(candidates[0].catalogType, 9);
});

test('spawn catalog extraction omits malformed and out-of-byte-range types', () => {
  const api = loadCatalogApi();
  const candidates = api.extractCandidates([{
    category: 'spawn-hopon',
    columns: ['tag', 'type'],
    rows: [
      new Map([['tag', '815BA133'], ['type', '-1']]),
      new Map([['tag', '815BA134'], ['type', '256']]),
      new Map([['tag', '815BA135'], ['type', 'biped']])
    ]
  }]);

  assert.equal(candidates.every(candidate => !Object.hasOwn(candidate, 'catalogType')), true);
});

test('spawn catalog extraction ignores other categories and catalogs without a tag field', () => {
  const api = loadCatalogApi();
  assert.equal(api.extractCandidates([
    { category: 'item-loot', columns: ['tag', 'name'], rows: [new Map([['tag', '815BA133'], ['name', 'item']])] },
    { category: 'spawn-hopon', columns: ['id', 'name'], rows: [new Map([['id', '815BA133'], ['name', 'entity']])] }
  ]).length, 0);
});
