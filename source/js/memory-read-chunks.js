(function (root, factory) {
  'use strict';
  const api = factory();
  if (root) root.RtmMemoryReadChunks = api;
  if (typeof module === 'object' && module && module.exports) module.exports = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const READ_HEX = /^(?:[0-9A-F]{2})+$/;

  async function readHexChunks(address, length, readChunk, options = {}) {
    const chunkBytes = options.chunkBytes === undefined ? 0x100 : options.chunkBytes;
    const assertCurrent = options.assertCurrent || (() => {});
    if (!Number.isSafeInteger(address) || address < 0 ||
        !Number.isSafeInteger(length) || length < 1 ||
        !Number.isSafeInteger(chunkBytes) || chunkBytes < 1 ||
        typeof readChunk !== 'function' || typeof assertCurrent !== 'function') {
      throw new TypeError('Invalid bounded memory-read arguments');
    }

    let hex = '';
    for (let offset = 0; offset < length; offset += chunkBytes) {
      assertCurrent();
      const chunkLength = Math.min(chunkBytes, length - offset);
      const chunkHex = String(await readChunk(address + offset, chunkLength) || '')
        .replace(/\s/g, '')
        .toUpperCase();
      assertCurrent();
      if (chunkHex.length !== chunkLength * 2 || !READ_HEX.test(chunkHex)) {
        throw new Error(`Short or invalid read at 0x${(address + offset).toString(16).toUpperCase()}`);
      }
      hex += chunkHex;
    }
    return hex;
  }

  return Object.freeze({ readHexChunks });
});
