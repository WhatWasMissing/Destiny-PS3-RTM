(function (root) {
  'use strict';

  async function copyPngToClipboard(blob, clipboard = root.navigator?.clipboard, ClipboardItemType = root.ClipboardItem) {
    if (!blob || blob.type !== 'image/png') throw new Error('No captured PNG is available');
    if (!clipboard || typeof clipboard.write !== 'function' || typeof ClipboardItemType !== 'function') {
      throw new Error('This view does not support copying images to the clipboard. Use Download PNG instead.');
    }
    await clipboard.write([new ClipboardItemType({ 'image/png': blob })]);
  }

  async function pngToJpeg(blob, quality = 0.92, documentApi = root.document, bitmapFactory = root.createImageBitmap, urlApi = root.URL) {
    if (!blob || blob.type !== 'image/png') throw new Error('No captured PNG is available');
    if (!documentApi || typeof documentApi.createElement !== 'function') throw new Error('Image conversion is unavailable in this view');
    if (!Number.isFinite(quality) || quality < 0 || quality > 1) throw new Error('JPEG quality must be between 0 and 1');

    let image = null;
    let bitmap = null;
    let decodeUrl = null;
    const canvas = documentApi.createElement('canvas');
    try {
      if (typeof bitmapFactory === 'function') {
        try {
          bitmap = await bitmapFactory(blob);
          image = bitmap;
        } catch (_) {
          // Use the HTMLImageElement path for webviews without working createImageBitmap support.
        }
      }
      if (!image) {
        if (!urlApi || typeof urlApi.createObjectURL !== 'function' || typeof urlApi.revokeObjectURL !== 'function') {
          throw new Error('Image decoding is unavailable in this view');
        }
        image = documentApi.createElement('img');
        decodeUrl = urlApi.createObjectURL(blob);
        await new Promise((resolve, reject) => {
          image.onload = resolve;
          image.onerror = () => reject(new Error('Could not decode the captured PNG'));
          image.src = decodeUrl;
        });
      }

      const width = image.width || image.naturalWidth;
      const height = image.height || image.naturalHeight;
      if (!width || !height) throw new Error('The captured image has no dimensions');
      canvas.width = width;
      canvas.height = height;
      const context = canvas.getContext('2d');
      if (!context) throw new Error('Canvas conversion is unavailable in this view');
      context.fillStyle = '#000';
      context.fillRect(0, 0, width, height);
      context.drawImage(image, 0, 0, width, height);

      const jpeg = await new Promise((resolve, reject) => {
        try {
          canvas.toBlob(result => {
            if (!result || result.type !== 'image/jpeg') reject(new Error('JPEG encoding is unavailable in this view'));
            else resolve(result);
          }, 'image/jpeg', quality);
        } catch (error) {
          reject(new Error(`JPEG encoding failed: ${error.message}`));
        }
      });
      return jpeg;
    } finally {
      if (bitmap && typeof bitmap.close === 'function') bitmap.close();
      if (decodeUrl) { try { urlApi.revokeObjectURL(decodeUrl); } catch (_) {} }
      canvas.width = 0;
      canvas.height = 0;
    }
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

  const api = Object.freeze({ copyPngToClipboard, pngToJpeg, createCaptureStore });
  root.RtmScreenshotTools = api;
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
})(globalThis);
