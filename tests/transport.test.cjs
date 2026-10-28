const { test } = require('node:test');
const assert = require('node:assert/strict');
const { createTransport } = require('../source/js/transport.js');

test('process scope applies only to memory endpoints', async () => {
  const urls = [];
  const transport = createTransport({ fetch: async url => { urls.push(new URL(url)); return new Response('ok'); } });
  await transport.request('http://console/setmem.ps3mapi?addr=1234&val=00');
  await transport.request('http://console/getmem.ps3mapi?proc=77&addr=1234');
  await transport.request('http://console/beep.ps3?1');
  assert.equal(urls[0].searchParams.get('proc'), '0x1010200');
  assert.equal(urls[1].searchParams.get('proc'), '77');
  assert.equal(urls[2].searchParams.has('proc'), false);
});

test('HTTP errors reject instead of reporting success', async () => {
  const transport = createTransport({ fetch: async () => ({ ok: false, status: 503 }) });
  await assert.rejects(transport.request('http://console/index.ps3'), /HTTP 503/);
  assert.equal(transport.diagnostics()[0].outcome, 'failed');
});

const stalledFetch = (_, { signal }) => new Promise((resolve, reject) => {
  signal.addEventListener('abort', () => reject(new Error('aborted')), { once: true });
});
test('a stalled console request is aborted by its timeout', async () => {
  const transport = createTransport({ fetch: stalledFetch });
  await assert.rejects(transport.request('http://console/index.ps3', 10), /timed out/);
  assert.equal(transport.diagnostics()[0].outcome, 'timeout');
});
test('disconnect cancellation aborts pending requests', async () => {
  const transport = createTransport({ fetch: stalledFetch });
  const pending = transport.request('http://console/index.ps3');
  transport.cancelAll();
  await assert.rejects(pending, /cancelled|aborted/);
  assert.equal(transport.diagnostics()[0].outcome, 'cancelled');
});
test('timeout covers a response body that stalls after headers arrive', async () => {
  const transport = createTransport({fetch:async()=>{
    const response = new Response(''); response.arrayBuffer = () => new Promise(()=>{}); return response;
  }});
  await assert.rejects(transport.request('http://console/getmem.ps3mapi',10),/timed out/);
});
test('buffered response remains readable by existing memory parsers',async()=>{
  const transport=createTransport({fetch:async()=>new Response('00112233')});
  assert.equal(await (await transport.request('http://console/getmem.ps3mapi')).text(),'00112233');
});
test('diagnostics preserve HTTP status without exposing popup text or console addresses',async()=>{
  const transport=createTransport({fetch:async()=>new Response('accepted')});
  await transport.request('http://192.168.1.100/popup.ps3/private-message');
  const exported=JSON.stringify(transport.diagnostics());
  assert.equal(exported.includes('private-message'),false); assert.equal(exported.includes('192.168'),false);
  assert.equal(transport.diagnostics()[0].httpStatus,200);
});
