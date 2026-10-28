const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

test('resident recovery visibly tells the user to restart Destiny', async () => {
  const status = { dataset: {}, textContent: '' };
  const output = { textContent: '', hidden: true };
  const elements = new Map([
    ['spawn-executor-status', status],
    ['spawn-executor-output', output]
  ]);
  const document = { readyState: 'loading', getElementById: id => elements.get(id) || null, addEventListener() {} };
  const message = 'Hook restored. Executor task, code and data may remain resident; restart Destiny before another install.';
  const executor = {
    async recover() { return { state: 'recovered-resident', message }; },
    state() { return { journal: { state: 'recovered-resident' }, active: null }; }
  };
  const window = {
    rtmActivitySessionMemory: {}, rtmSpawnExecutorMemory: {}, DESTINY_RTM_SPAWN_PACKET: {},
    DESTINY_RTM_SPAWN_DATA: {}, DESTINY_RTM_SPAWN_EXECUTOR_IMAGES: {},
    RtmSpawnExecutorController: { create: () => executor }, RtmBuildCompatibility: { anchorCount: 5 },
    rtmSpawnPathSignatures: [], rtmSpawnExecutorRegions: [], localStorage: {}
  };
  const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'spawn-executor-runtime.js'), 'utf8');
  vm.runInNewContext(source, { window, document });
  await window.recoverSpawnExecutor();
  assert.equal(status.dataset.state, 'warning');
  assert.equal(status.textContent, message);
  assert.equal(output.hidden, false);
  assert.equal(JSON.parse(output.textContent).state, 'recovered-resident');
});

test('catalog type guard action copies a bounded numeric candidate with an uncertainty notice', () => {
  const status = { textContent: '' };
  const expectedType = { value: '-1' };
  const catalogSelect = { value: '0' };
  const elements = new Map([
    ['spawn-catalog-entry', catalogSelect],
    ['spawn-catalog-status', status],
    ['spawn-expected-type', expectedType]
  ]);
  const document = { readyState: 'loading', getElementById: id => elements.get(id) || null, addEventListener() {} };
  const window = {
    rtmLocalSpawnCatalogs: { listCandidates: () => [{ tag: '815BA133', label: 'Vex Minotaur', source: 'spawnable.tsv', catalogType: 9 }] }
  };
  const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'spawn-executor-runtime.js'), 'utf8');
  vm.runInNewContext(source, { window, document });

  window.useSpawnCatalogTypeAsGuard();

  assert.equal(expectedType.value, '9');
  assert.match(status.textContent, /unverified/i);
  assert.match(status.textContent, /BLES 01\.24 name table has “biped” at ordinal 9/i);
  assert.match(status.textContent, /direct mapping to this guard remains unverified/i);
  assert.match(status.textContent, /does not prove the tag is hostile or will spawn/i);
});

test('catalog selection labels BLES object names as candidates without applying the guard', () => {
  const status = { textContent: '' };
  const expectedType = { value: '-1' };
  const tagInput = { value: '' };
  const select = { value: '0', options: [], addEventListener(type, handler) { if (type === 'change') this.onChange = handler; }, replaceChildren() { this.options = []; }, append(option) { this.options.push(option); } };
  const button = { disabled: true, textContent: '', addEventListener() {} };
  const search = { value: '', addEventListener() {} };
  const listeners = new Map();
  const elements = new Map([
    ['spawn-catalog-entry', select],
    ['spawn-catalog-status', status],
    ['spawn-tag-input', tagInput],
    ['spawn-catalog-use-type-btn', button],
    ['spawn-expected-type', expectedType],
    ['spawn-catalog-search', search],
    ['spawn-executor-panel', {}]
  ]);
  const document = {
    readyState: 'loading',
    getElementById: id => elements.get(id) || null,
    addEventListener(type, handler) { listeners.set(type, handler); },
    createElement() { return { value: '', textContent: '', disabled: false }; }
  };
  const window = {
    addEventListener() {},
    localStorage: {},
    rtmActivitySessionMemory: {}, rtmSpawnExecutorMemory: {}, DESTINY_RTM_SPAWN_PACKET: {},
    DESTINY_RTM_SPAWN_DATA: {}, DESTINY_RTM_SPAWN_EXECUTOR_IMAGES: {},
    RtmBuildCompatibility: { anchorCount: 5 }, rtmSpawnPathSignatures: [], rtmSpawnExecutorRegions: [],
    RtmSpawnExecutorController: { create: () => ({ state: () => ({ journal: null, active: null }) }) },
    rtmLocalSpawnCatalogs: { listCandidates: () => [{ tag: '815BA133', label: 'Vex Minotaur', source: 'spawnable.tsv', catalogType: 9 }] }
  };
  const source = fs.readFileSync(path.join(__dirname, '..', 'source', 'js', 'spawn-executor-runtime.js'), 'utf8');
  vm.runInNewContext(source, { window, document });

  listeners.get('DOMContentLoaded')();
  select.onChange();

  assert.equal(tagInput.value, '815BA133');
  assert.equal(expectedType.value, '-1');
  assert.equal(button.disabled, false);
  assert.match(button.textContent, /9.*biped.*candidate/i);
  assert.match(status.textContent, /possible BLES name-table label “biped”/i);
  assert.match(status.textContent, /table-to-guard link is unverified/i);
});
