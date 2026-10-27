(function (root) {
  'use strict';

  async function copyPngToClipboard(blob, clipboard = root.navigator?.clipboard, ClipboardItemType = root.ClipboardItem) {
    if (!blob || blob.type !== 'image/png') throw new Error('No captured PNG is available');
    if (!clipboard || typeof clipboard.write !== 'function' || typeof ClipboardItemType !== 'function') {
      throw new Error('This view does not support copying images to the clipboard. Use Download PNG instead.');
    }
    await clipboard.write([new ClipboardItemType({ 'image/png': blob })]);
  }

  function createCaptureStore(urlApi = root.URL) {
    if (!urlApi || typeof urlApi.createObjectURL !== 'function' || typeof urlApi.revokeObjectURL !== 'function') {
      throw new Error('Screenshot preview URLs are not supported in this view');
    }
    const captures = new Map();
    return Object.freeze({
      get(profileId) { return captures.get(profileId) || null; },
      entries() { return Array.from(captures.entries()); },
      set(profileId, blob, downloadName) {
        if (typeof profileId !== 'string' || !profileId) throw new Error('A screenshot profile is required');
        if (!blob || blob.type !== 'image/png') throw new Error('Only captured PNG images can be stored');
        const capture = Object.freeze({ profileId, blob, downloadName, objectUrl: urlApi.createObjectURL(blob) });
        const previous = captures.get(profileId);
        captures.set(profileId, capture);
        if (previous) { try { urlApi.revokeObjectURL(previous.objectUrl); } catch (_) {} }
        return capture;
      },
      clear() {
        for (const capture of captures.values()) { try { urlApi.revokeObjectURL(capture.objectUrl); } catch (_) {} }
        captures.clear();
      }
    });
  }

  const api = Object.freeze({ copyPngToClipboard, createCaptureStore });
  root.RtmScreenshotTools = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(globalThis);
