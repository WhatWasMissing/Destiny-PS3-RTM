(function (root) {
  'use strict';

  async function copyPngToClipboard(blob, clipboard = root.navigator?.clipboard, ClipboardItemType = root.ClipboardItem) {
    if (!blob || blob.type !== 'image/png') throw new Error('No captured PNG is available');
    if (!clipboard || typeof clipboard.write !== 'function' || typeof ClipboardItemType !== 'function') {
      throw new Error('This view does not support copying images to the clipboard. Use Download PNG instead.');
    }
    await clipboard.write([new ClipboardItemType({ 'image/png': blob })]);
  }

  const api = Object.freeze({ copyPngToClipboard });
  root.RtmScreenshotTools = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(globalThis);
