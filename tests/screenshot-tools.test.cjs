const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { copyPngToClipboard, createCaptureStore } = require('../source/js/screenshot-tools.js');

test('copies the current PNG blob as an image/png clipboard item', async () => {
  const blob = { type: 'image/png' };
  const writes = [];
  class FakeClipboardItem {
    constructor(items) { this.items = items; }
  }
  await copyPngToClipboard(blob, { write: async items => writes.push(items) }, FakeClipboardItem);
  assert.equal(writes.length, 1);
  assert.equal(writes[0].length, 1);
  assert.equal(writes[0][0].items['image/png'], blob);
});

test('reports unsupported image clipboard APIs and rejects non-PNG data', async () => {
  await assert.rejects(copyPngToClipboard({ type: 'image/png' }, {}, undefined), /does not support copying images/);
  await assert.rejects(copyPngToClipboard({ type: 'image/jpeg' }, { write() {} }, function ClipboardItem() {}), /No captured PNG/);
});

test('profile captures remain independent and replace/revoke only their own previous preview', () => {
  const created = [];
  const revoked = [];
  const urlApi = {
    createObjectURL: blob => { const url = `blob:test-${created.length + 1}`; created.push([url, blob]); return url; },
    revokeObjectURL: url => revoked.push(url)
  };
  const store = createCaptureStore(urlApi);
  const sd = store.set('sd', { type: 'image/png', id: 'sd-1' }, 'sd.png');
  const hd = store.set('hd-a', { type: 'image/png', id: 'hd-1' }, 'hd.png');
  const sdUpdated = store.set('sd', { type: 'image/png', id: 'sd-2' }, 'sd-new.png');

  assert.equal(store.get('sd'), sdUpdated);
  assert.equal(store.get('hd-a'), hd);
  assert.deepEqual(revoked, [sd.objectUrl]);
  assert.equal(store.entries().length, 2);

  store.clear();
  assert.equal(store.entries().length, 0);
  assert.deepEqual(new Set(revoked), new Set([sd.objectUrl, hd.objectUrl, sdUpdated.objectUrl]));
});

test('the capture UI renders five profile slots wired to the existing capture and clipboard flow', () => {
  const html = fs.readFileSync(require.resolve('../source/index.html'), 'utf8');
  const appCore = fs.readFileSync(require.resolve('../source/js/app-core.js'), 'utf8');
  const integrationHarness = fs.readFileSync(require.resolve('./desktop-integration.cjs'), 'utf8');
  assert.ok(html.indexOf('js/screenshot-tools.js') < html.indexOf('js/app-core.js'));
  assert.ok(integrationHarness.indexOf("'screenshot-tools'") < integrationHarness.indexOf("'app-core'"));
  assert.match(html, /id="screenshot-gallery"/);
  assert.doesNotMatch(html, /id="screenshot-preview"/);
  for (const profileId of ['sd', 'hd-a', 'hd-b', 'hd-1280', 'hd-512']) {
    assert.ok(new RegExp(`^\\s*['"]?${profileId}['"]?:`, 'm').test(appCore), `missing profile ${profileId}`);
  }
  assert.match(appCore, /screenshotCaptures\.set\(profileId, blob,/);
  assert.match(appCore, /copyPngToClipboard\(capture\.blob\)/);
  assert.match(appCore, /renderScreenshotGallery\(\)/);
});
