const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const { copyPngToClipboard } = require('../source/js/screenshot-tools.js');

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

test('the capture UI wires the clipboard helper and stays disabled until a PNG exists', () => {
  const html = fs.readFileSync(require.resolve('../source/index.html'), 'utf8');
  const appCore = fs.readFileSync(require.resolve('../source/js/app-core.js'), 'utf8');
  assert.ok(html.indexOf('js/screenshot-tools.js') < html.indexOf('js/app-core.js'));
  assert.match(html, /id="screenshot-copy-btn"[^>]*onclick="copyDestinyScreenshot\(\)"[^>]*disabled/);
  assert.match(appCore, /screenshotBlob = blob;/);
  assert.match(appCore, /copyPngToClipboard\(screenshotBlob\)/);
});
