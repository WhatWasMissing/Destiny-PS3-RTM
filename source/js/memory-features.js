(function (root) {
  'use strict';

  function bytesFromHex(hex) {
    const clean = String(hex || '').replace(/\s+/g, '').toUpperCase();
    if (!/^(?:[0-9A-F]{2})*$/.test(clean)) throw new Error('Memory response is not whole-byte hex');
    const bytes = new Uint8Array(clean.length / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(clean.slice(i * 2, i * 2 + 2), 16);
    return bytes;
  }

  function beU16(value) {
    if (!Number.isInteger(value) || value < 0 || value > 0xFFFF) throw new Error('Value must fit in an unsigned 16-bit integer');
    return [value >>> 8, value & 0xFF];
  }

  function beU32(value) {
    if (!Number.isInteger(value) || value < 0 || value > 0x7FFFFFFF) throw new Error('Ammo value must be between 0 and 2147483647');
    return [(value >>> 24) & 0xFF, (value >>> 16) & 0xFF, (value >>> 8) & 0xFF, value & 0xFF];
  }

  function beFloat32(value) {
    if (typeof value !== 'number' || !Number.isFinite(value)) throw new Error('Value must be a finite number');
    const buffer = new ArrayBuffer(4);
    const view = new DataView(buffer);
    view.setFloat32(0, value, false);
    if (!Number.isFinite(view.getFloat32(0, false))) throw new Error('Value is outside the Float32 range');
    return Array.from(new Uint8Array(buffer));
  }

  function findEmblemMatches(bytes, baseAddress, emblemId) {
    const needle = beU16(emblemId);
    const matches = [];
    for (let offset = 0; offset + 1 < bytes.length; offset += 2) {
      if (bytes[offset] === needle[0] && bytes[offset + 1] === needle[1]) matches.push(baseAddress + offset);
    }
    return matches;
  }

  function findAmmoCandidates(bytes, baseAddress, currentMag, currentReserve) {
    const signature = [...beU32(currentReserve), ...beU32(currentMag)];
    const matches = [];
    for (let offset = 0; offset + 30 <= bytes.length; offset++) {
      let equal = true;
      for (let index = 0; index < signature.length; index++) {
        if (bytes[offset + index] !== signature[index]) { equal = false; break; }
      }
      if (equal && bytes[offset + 28] === 0x80 && bytes[offset + 29] === 0xA6) matches.push(baseAddress + offset);
    }
    return matches;
  }

  function findSpecialAmmoCandidates(bytes, baseAddress, currentValue) {
    const signature = [...beFloat32(currentValue), 0x01, 0x00, 0x00, 0x00];
    const matches = [];
    for (let offset = 0; offset + 42 <= bytes.length; offset++) {
      let equal = true;
      for (let index = 0; index < signature.length; index++) {
        if (bytes[offset + index] !== signature[index]) { equal = false; break; }
      }
      if (equal && bytes[offset + 40] === 0x80 && bytes[offset + 41] === 0x80) matches.push(baseAddress + offset);
    }
    return matches;
  }

  function framebufferToRgba(bytes, width, height, rowStrideBytes = width * 4) {
    if (!Number.isInteger(width) || width < 1 || !Number.isInteger(height) || height < 1 ||
        !Number.isInteger(rowStrideBytes) || rowStrideBytes < width * 4 || bytes.length < rowStrideBytes * height) {
      throw new Error('Framebuffer dimensions or memory length are invalid');
    }
    const rgba = new Uint8ClampedArray(width * height * 4);
    for (let y = 0; y < height; y++) {
      const row = y * rowStrideBytes;
      for (let x = 0; x < width; x++) {
        const source = row + x * 4;
        const target = (y * width + x) * 4;
        // The local RTM stores framebuffer pixels as A,B,G,R; Canvas wants R,G,B,A.
        rgba[target] = bytes[source + 1];
        rgba[target + 1] = bytes[source + 2];
        rgba[target + 2] = bytes[source + 3];
        rgba[target + 3] = 0xFF;
      }
    }
    return rgba;
  }

  const api = { bytesFromHex, beU16, beU32, beFloat32, findEmblemMatches, findAmmoCandidates, findSpecialAmmoCandidates, framebufferToRgba };
  root.RtmMemoryFeatures = api;
  if (typeof module !== 'undefined') module.exports = api;
})(globalThis);
