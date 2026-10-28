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
