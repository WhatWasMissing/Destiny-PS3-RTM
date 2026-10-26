const { test } = require('node:test');
const assert = require('node:assert/strict');
const { capture, validate, restore } = require('../source/js/backups.js');
function memory(initial = {}) {
  const map = new Map(Object.entries(initial));
  return { get length() { return map.size; }, key: i => [...map.keys()][i],
    getItem: k => map.get(k) ?? null, setItem: (k,v) => map.set(k,v), removeItem: k => map.delete(k) };
}
test('backups round trip supported preferences and positions without active cheat state', () => {
  const source = memory({ rtm_saved_ip: '192.168.0.62', rtm_saved_teleports: '[{"name":"Tower","x":0,"y":2,"z":3}]', destinyRtmToolSetupV1:'1', rtm_active_cheats: '{"god":true}' });
  const bundle = capture(source), destination = memory();
  assert.equal(bundle.settings.rtm_active_cheats, undefined);
  restore(destination, bundle);
  assert.equal(destination.getItem('rtm_saved_teleports'), source.getItem('rtm_saved_teleports'));
  assert.equal(destination.getItem('destinyRtmToolSetupV1'),'1');
});
test('reject unknown keys, unsupported versions and nonfinite coordinates before mutation', () => {
  const bundle = capture(memory());
  assert.throws(() => validate({ ...bundle, version: 2 }));
  assert.throws(() => restore(memory(), { ...bundle, settings: { unrelated: 'x' } }));
  assert.throws(() => validate({ ...bundle, settings: { rtm_saved_teleports: '[{"name":"bad","x":"Infinity","y":0,"z":0}]' } }));
});
test('restore rolls back earlier changes when storage fails', () => {
  const storage = memory({ rtm_saved_ip: 'old' });
  const original = storage.setItem;
  storage.setItem = (k,v) => { if (k === 'henMode') throw new Error('full'); original(k,v); };
  assert.throws(() => restore(storage, { format: 'destiny-rtm-settings', version: 1, settings: { rtm_saved_ip: 'new', henMode: '1' } }));
  assert.equal(storage.getItem('rtm_saved_ip'), 'old');
});
test('reject coordinates coerced from null, booleans, arrays and float32 overflow', () => {
  for (const x of [null, true, [], {}, '', ' ', '1e100']) {
    assert.throws(() => validate({ format: 'destiny-rtm-settings', version: 1, settings: {
      rtm_saved_teleports: JSON.stringify([{name:'bad',x,y:0,z:0}])
    } }));
  }
});
test('reject malformed addresses, settings enums and duplicate folder IDs', () => {
  for (const settings of [ {cheatAddresses:'{"tog-god":"1234&val=BAD"}'}, {rtm_active_tab:'unknown'},
    {rtm_appearance:'other'}, {henMode:'yes'}, {rtm_saved_ip:'999.0.0.1'},
    {rtm_teleport_folders:'[{"id":"a","name":"One"},{"id":"a","name":"Two"}]'} ]) {
    assert.throws(() => validate({format:'destiny-rtm-settings',version:1,settings}));
  }
});
