const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const source = fs.readFileSync(require.resolve('../source/js/app-core.js'), 'utf8');
const start = source.indexOf('  function beginSafeAutoReconnect(');
const end = source.indexOf('\n  //', start);
const productionFunction = source.slice(start, end);
function harness() {
  let resolveProbe, finishCount = 0, interval;
  const nodes = new Proxy({}, { get: (target,key) => target[key] ||= { style: {}, classList: { remove() {} } } });
  const context = vm.createContext({
    ip: '192.168.0.62', connectionGeneration: 1, autoReconnectEnabled: true,
    _autoReconnectActive: false, _autoReconnectAttempts: 0, _autoReconnectTimer: null,
    AUTO_RECONNECT_MAX_ATTEMPTS: 3, AUTO_RECONNECT_INTERVAL: 100,
    stopHealthCheck() {}, setBadge() {}, log() {}, toast() {}, stopAutoReconnect() {},
    consoleTransport: { cancelAll() {} }, resetVerifiedStates() {},
    probePS3Reachable: () => new Promise(resolve => { resolveProbe = resolve; }),
    finishSafeAutoReconnect: async () => { finishCount++; },
    setInterval: fn => { interval = fn; return 1; }, document: { getElementById: id => nodes[id] }
  });
  vm.runInContext(productionFunction, context);
  return { context, resolve: () => resolveProbe(), tick: () => interval(), finished: () => finishCount };
}
const flush = () => new Promise(resolve => setImmediate(resolve));
test('late reconnect probe cannot restore a disconnected or replacement session', async () => {
  for (const invalidate of [h => { h.context._autoReconnectActive = false; }, h => { h.context.connectionGeneration++; }]) {
    const h = harness(); h.context.beginSafeAutoReconnect(); invalidate(h); h.resolve(); await flush();
    assert.equal(h.finished(), 0);
  }
});
test('interval does not overlap a pending probe and current session can reconnect', async () => {
  const h = harness(); h.context.beginSafeAutoReconnect(); await h.tick();
  assert.equal(h.context._autoReconnectAttempts, 1);
  h.resolve(); await flush(); assert.equal(h.finished(), 1);
});
