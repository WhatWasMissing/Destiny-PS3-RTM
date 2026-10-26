const test = require('node:test');
const assert = require('node:assert/strict');

const RESOLVER = 0x002FF2D0;
const GETTER = 0x001FB3C8;
const NAME_ROOT = 0x01897C48;
const LIST_ROOT = 0x01B29450;
const LOOKUP_ROOT = 0x01A21CB0;
const SCALE_ROOT = 0x01A1D9E8;
const RECORDS = 0x10020000;
const SCALE_FIELD = 0x1008201C;
const RESOLVER_BYTES = '7C0802A648A99751F821FF81F801009038A0FFFF607F00002C04FFFF418200483C6001A2548404FE30C31CB08066000C7C8419D680E600083061007080C600247C87201490A100707885002080A500087CA530387C852010788400204BEFC09D80A1007090BF00003821008048A9974C';
const GETTER_BYTES = '8084001C908300004E800020';

function setup() {
  const memory = new Map();
  const elements = new Map([
    ['player-scale-slider', { value: '1' }],
    ['player-scale-value', { textContent: '' }],
    ['player-scale-status', { textContent: '' }]
  ]);
  const writes = [];
  function set(address, bytes) {
    bytes.forEach((byte, offset) => memory.set(address + offset, byte));
  }
  function setHex(address, hex) {
    set(address, Buffer.from(hex, 'hex'));
  }
  function setU32(address, value) {
    const bytes = Buffer.alloc(4);
    bytes.writeUInt32BE(value >>> 0);
    set(address, bytes);
  }
  function read(address, length) {
    const bytes = Buffer.alloc(length);
    for (let offset = 0; offset < length; offset++) bytes[offset] = memory.get(address + offset) || 0;
    return bytes;
  }
  setHex(RESOLVER, RESOLVER_BYTES);
  setHex(GETTER, GETTER_BYTES);
  setU32(NAME_ROOT, 0x10000000);
  set(0x10000090, Buffer.from('TestGuardian'));
  setU32(LIST_ROOT, 0x10010000);
  setU32(0x10010004, RECORDS);
  const record = RECORDS + 2 * 10544;
  set(record + 1920, Buffer.from('TestGuardian'));
  setU32(record + 1604, 0x12345);
  setU32(LOOKUP_ROOT + 8, 0x10070000);
  setU32(LOOKUP_ROOT + 12, 0x20);
  setU32(LOOKUP_ROOT + 0x24, 0xFFFF0000);
  const entry = 0x10070000 + 0x345 * 0x20;
  setU32(entry + 8, 0x00010000);
  setU32(entry - 0x10000 + 0x1C, 42);
  setU32(SCALE_ROOT, 0x10080000);
  setHex(SCALE_FIELD, '3F800000');

  global.document = { getElementById: id => elements.get(id) || null };
  global.ip = '192.0.2.1';
  global.connectionGeneration = 1;
  global.ensureOperationSession = generation => {
    if (generation !== global.connectionGeneration) throw new Error('Console session changed');
  };
  global.ps3getmem = async (address, length) => read(address, length).toString('hex').toUpperCase();
  global.floatToHex = value => {
    const bytes = Buffer.alloc(4);
    bytes.writeFloatBE(Number(value));
    return bytes.toString('hex').toUpperCase();
  };
  global.verifiedMemoryValue = async (address, value) => {
    const numericAddress = Number.parseInt(address, 16);
    writes.push([numericAddress, value]);
    setHex(numericAddress, value);
  };
  global.log = () => {};

  const path = require.resolve('../source/js/player-scale-tools.js');
  delete require.cache[path];
  require(path);
  return { elements, memory, read, writes };
}

test('Player Size reads the 1.24 source pointer chain without writing', async () => {
  const { elements, writes } = setup();
  const state = await global.readPlayerScale();
  assert.equal(state.playerName, 'TestGuardian');
  assert.equal(state.handle, 0x12345);
  assert.equal(state.lookup.objectIndex, 42);
  assert.equal(state.scaleAddress, SCALE_FIELD);
  assert.equal(state.scale, 1);
  assert.equal(writes.length, 0);
  assert.equal(elements.get('player-scale-slider').value, '1');
  assert.equal(elements.get('player-scale-value').textContent, '1×');
  assert.match(elements.get('player-scale-status').textContent, /Read TestGuardian/);
});

test('Player Size applies source scales with verified Float32 readback', async () => {
  const { elements, read, writes } = setup();
  await global.setPlayerScaleValue(0.3);
  assert.deepEqual(writes, [[SCALE_FIELD, '3E99999A']]);
  assert.equal(read(SCALE_FIELD, 4).toString('hex').toUpperCase(), '3E99999A');
  assert.equal(elements.get('player-scale-slider').value, '0.3');
  assert.equal(elements.get('player-scale-value').textContent, '0.3×');
  assert.match(elements.get('player-scale-status').textContent, /bytes verified/);
});

test('Player Size refuses unsupported resolver bytes before any write', async () => {
  const { elements, memory, writes } = setup();
  memory.set(RESOLVER, 0);
  await global.setPlayerScaleValue(2);
  assert.equal(writes.length, 0);
  assert.match(elements.get('player-scale-status').textContent, /do not match the verified 1\.24 source/);
});
