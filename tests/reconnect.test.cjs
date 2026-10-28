const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const source = fs.readFileSync(require.resolve('../source/js/connection.js'), 'utf8');

function extractBetween(startMarker, endMarker) {
  const start = source.indexOf(startMarker);
  const end = source.indexOf(endMarker, start);
  assert.ok(start >= 0 && end > start, `expected connection source markers: ${startMarker}`);
  return source.slice(start, end);
}

function makeElement(id) {
  const classes = new Set();
  return {
    id,
    value: id === 'ip' ? '192.168.0.62' : '',
    textContent: '',
    style: {},
    classList: {
      contains: name => classes.has(name),
      add: name => classes.add(name),
      remove: name => classes.delete(name),
      toggle: (name, on) => (on ?? !classes.has(name)) ? classes.add(name) : classes.delete(name)
    },
    setAttribute() {}
  };
}

function harness(overrides = {}) {
  let interval;
  let intervalId = 0;
  const elements = new Map();
  const context = vm.createContext({
    window: { rtmFramebufferBusy: false },
    document: { getElementById: id => elements.get(id) || (elements.set(id, makeElement(id)), elements.get(id)) },
    localStorage: { getItem: () => null, setItem() {}, removeItem() {} },
    ip: '192.168.0.62',
    connectionGeneration: 1,
    quietLinkEnabled: false,
    autoReconnectEnabled: true,
    _autoReconnectActive: false,
    _autoReconnectAttempts: 0,
    _autoReconnectTimer: null,
    _healthTimer: null,
    _healthMissed: 0,
    AUTO_RECONNECT_MAX_ATTEMPTS: 3,
    AUTO_RECONNECT_INTERVAL: 100,
    AUTO_RECONNECT_PROBE_TIMEOUT: 10,
    HEALTH_INTERVAL: 100,
    HEALTH_TIMEOUT: 10,
    HEALTH_MAX_MISS: 2,
    HEALTH_QUIET_WINDOW: 5,
    ps3mapiHealthProbeSupported: true,
    recordConnectionEvent() {},
    stopHealthCheck() {},
    stopAutoReconnect() {},
    resetVerifiedStates() {},
    setBadge() {},
    log() {},
    toast() {},
    saveIp() {},
    isDefiniteCommandRejection: () => false,
    RtmDialogs: { confirm: async () => true },
    consoleTransport: {
      cancelAll() {},
      shouldDeferBackgroundProbe: () => false,
      request: async () => new Response('ok')
    },
    probePs3mapiAvailable: async () => true,
    finishSafeAutoReconnect: async () => {},
    uninstallPlayerHook: async () => {},
    sleep: async () => {},
    setInterval: fn => { interval = fn; return ++intervalId; },
    clearInterval() {},
    ...overrides
  });
  return { context, elements, tick: () => interval?.() };
}

const flush = () => new Promise(resolve => setImmediate(resolve));

test('late reconnect probe cannot restore a disconnected or replacement session', async () => {
  const snippet = extractBetween('  function beginSafeAutoReconnect(', '\n  // ── Connection health-check');
  for (const invalidate of [
    context => { context._autoReconnectActive = false; },
    context => { context.connectionGeneration++; }
  ]) {
    let resolveProbe;
    let finished = 0;
    const h = harness({
      probePs3mapiAvailable: () => new Promise(resolve => { resolveProbe = resolve; }),
      finishSafeAutoReconnect: async () => { finished++; }
    });
    vm.runInContext(snippet, h.context);
    h.context.beginSafeAutoReconnect();
    invalidate(h.context);
    resolveProbe(true);
    await flush();
    assert.equal(finished, 0);
  }
});

test('Quiet connection suppresses background reconnect and health probes', async () => {
  let probes = 0;
  let intervals = 0;
  const h = harness({
    quietLinkEnabled: true,
    probePs3mapiAvailable: async () => { probes++; return true; },
    setInterval: fn => { intervals++; return ++intervals; }
  });
  vm.runInContext(extractBetween('  function beginSafeAutoReconnect(', '\n  // ── Connection health-check'), h.context);
  vm.runInContext(extractBetween('  function startHealthCheck(', '\n\n  function stopHealthCheck'), h.context);
  h.context.beginSafeAutoReconnect();
  h.context.startHealthCheck();
  await flush();
  assert.equal(probes, 0);
  assert.equal(intervals, 0);
  assert.equal(h.context._autoReconnectAttempts, 0);
});

test('reconnect interval does not overlap a pending probe and restores the current session', async () => {
  let resolveProbe;
  let finished = 0;
  const h = harness({
    probePs3mapiAvailable: () => new Promise(resolve => { resolveProbe = resolve; }),
    finishSafeAutoReconnect: async () => { finished++; }
  });
  vm.runInContext(extractBetween('  function beginSafeAutoReconnect(', '\n  // ── Connection health-check'), h.context);
  h.context.beginSafeAutoReconnect();
  assert.equal(h.context._autoReconnectAttempts, 1);
  await h.tick();
  assert.equal(h.context._autoReconnectAttempts, 1);
  resolveProbe(true);
  await flush();
  assert.equal(finished, 1);
});

test('quick relink cannot reconnect after the session is replaced', async () => {
  let resolveProbe;
  let finished = 0;
  const h = harness({
    probePs3mapiAvailable: () => new Promise(resolve => { resolveProbe = resolve; }),
    finishSafeAutoReconnect: async () => { finished++; }
  });
  vm.runInContext(extractBetween('  async function quickReconnectFromBadge(', '\n\n  window.quickReconnectFromBadge'), h.context);
  const pending = h.context.quickReconnectFromBadge();
  h.context.connectionGeneration++;
  resolveProbe(true);
  await pending;
  assert.equal(finished, 0);
});

test('restart wait does not start a reconnect loop after disconnect', async () => {
  let finishWait;
  let reconnects = 0;
  const h = harness({
    sleep: () => new Promise(resolve => { finishWait = resolve; }),
    beginSafeAutoReconnect: () => { reconnects++; }
  });
  vm.runInContext(extractBetween('  async function ps3Restart(', '\n\n  async function doubleBeep'), h.context);
  const pending = h.context.ps3Restart();
  await flush();
  h.context.connectionGeneration++;
  finishWait();
  await pending;
  assert.equal(reconnects, 0);
});

test('late disconnect cleanup cannot clear a newer connection', async () => {
  let finishCleanup;
  let cancellations = 0;
  const h = harness({
    consoleTransport: { cancelAll() { cancellations++; } },
    uninstallPlayerHook: () => new Promise(resolve => { finishCleanup = resolve; })
  });
  vm.runInContext(extractBetween('  async function disconnect(', '\n\n  // ── Reachability probe'), h.context);
  const pending = h.context.disconnect();
  h.context.connectionGeneration++;
  h.context.ip = '192.168.0.99';
  finishCleanup();
  await pending;
  assert.equal(h.context.ip, '192.168.0.99');
  assert.equal(cancellations, 1);
});

test('old health response cannot mark a replacement session at the same IP online', async () => {
  let finishProbe;
  let badgeUpdates = 0;
  const h = harness({
    setBadge: () => { badgeUpdates++; },
    probePs3mapiAvailable: () => new Promise(resolve => { finishProbe = resolve; })
  });
  vm.runInContext(extractBetween('  function startHealthCheck(', '\n\n  function stopHealthCheck'), h.context);
  h.context.startHealthCheck();
  h.context._healthMissed = 1;
  const pending = h.tick();
  h.context.connectionGeneration++;
  finishProbe(true);
  await pending;
  assert.equal(badgeUpdates, 0);
});
