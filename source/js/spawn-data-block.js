(function (root, factory) {
  'use strict';

  const api = factory();
  if (typeof module === 'object' && module && module.exports) module.exports = api;
  if (root) root.DESTINY_RTM_SPAWN_DATA = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const DATA_BLOCK_LENGTH = 0x1cd0;
  const RECORDS_OFFSET = 0x150;
  const RECORD_LENGTH = 0x370;
  const RECORD_COUNT = 8;
  const HEADER_MAGIC = 0x42524331; // ASCII BRC1
  const HEADER_VERSION = 1;

  const STATIC_WORDS = Object.freeze([
    0x3f000000, 0x3f800000, 0x3a83126f, 0x3c23d70a,
    0x358637bd, 0x4b189680, 0x00000000, 0x7149f2ca
  ]);

  const TEMPLATE_WORDS = Object.freeze([
    0xf4a7aa80, 0x0c000000, 0x06098080, 0x00000000,
    0x402dccbc, 0x104f963b, 0xe61b663f, 0x75ffdf3e,
    0x6a8a3d42, 0xad0038c4, 0x61b21e41, 0xfeff7f3f,
    0xffffffff, 0x0480fb61, 0x00000000, 0x00000000,
    0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff,
    0x00000000, 0xffffffff, 0x00000000, 0x0000803f,
    0xffffffff, 0xc59d1c81, 0x00000310, 0x00000000,
    0xffffffff, 0xffffffff, 0x0c000000, 0x00000000
  ]);

  // FUN_140435960 writes this zero-filled image before Handler::ensure
  // registers its per-frame task. The three overrides are applied after the
  // source template copy, matching the recovered initializer.
  function buildDataBlock() {
    const bytes = new Uint8Array(DATA_BLOCK_LENGTH);
    const view = new DataView(bytes.buffer);
    view.setUint32(0x00, HEADER_MAGIC, false);
    view.setUint32(0x04, HEADER_VERSION, false);
    STATIC_WORDS.forEach((word, index) => view.setUint32(0x10 + index * 4, word, false));
    TEMPLATE_WORDS.forEach((word, index) => view.setUint32(0x40 + index * 4, word, false));
    view.setUint32(0x44, 0x0000000c, false);
    view.setUint32(0x48, 0x80800906, false);
    view.setUint32(0x74, 0xffffffff, false);
    return bytes;
  }

  function recordAddress(dataBase, index) {
    if (!Number.isInteger(dataBase) || dataBase < 0 || dataBase > 0xffffffff) {
      throw new RangeError('dataBase must be an unsigned 32-bit address');
    }
    if (!Number.isInteger(index) || index < 0 || index >= RECORD_COUNT) {
      throw new RangeError(`record index must be from 0 through ${RECORD_COUNT - 1}`);
    }
    return dataBase + RECORDS_OFFSET + index * RECORD_LENGTH;
  }

  return Object.freeze({
    DATA_BLOCK_LENGTH,
    RECORDS_OFFSET,
    RECORD_LENGTH,
    RECORD_COUNT,
    HEADER_MAGIC,
    HEADER_VERSION,
    STATIC_WORDS,
    TEMPLATE_WORDS,
    buildDataBlock,
    recordAddress
  });
});
