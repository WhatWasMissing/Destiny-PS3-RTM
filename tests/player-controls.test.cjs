const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require.resolve('../source/js/app-core.js'), 'utf8');
function functionSource(name) {
  const start = source.indexOf('  async function ' + name + '(');
  return source.slice(start, source.indexOf('\n  }', start)+4);
}
function harness() {
  const writes = [], nodes = {'fov-slider':{value:'2.5'},'fov-custom':{value:'2.75'}};
  const context = vm.createContext({document:{getElementById:id=>nodes[id]},
    writeFOVValue: async value => writes.push(Number(value)), updateFOV() {}, toast() {}, log() {}});
  for (const name of ['applyFOV','applyCustomFOV','resetFOV']) vm.runInContext(functionSource(name),context);
  return {context,writes,nodes};
}
test('FOV slider uses direct values rather than obsolete offset formula', async () => {
  const h = harness(); await h.context.applyFOV(); assert.deepEqual(h.writes,[2.5]);
  await h.context.applyCustomFOV(); assert.deepEqual(h.writes,[2.5,2.75]); assert.equal(h.nodes['fov-slider'].value,'2.75');
});
test('FOV reset writes and displays 2.0 rather than zero', async () => {
  const h = harness(); await h.context.resetFOV(); assert.deepEqual(h.writes,[2]); assert.equal(h.nodes['fov-slider'].value,'2');
});
test('each FOV handler has exactly one production definition', () => {
  for (const name of ['applyFOV','resetFOV','writeFOVValue']) {
    assert.equal((source.match(new RegExp('function '+name+'\\(', 'g')) || []).length,1);
  }
});
