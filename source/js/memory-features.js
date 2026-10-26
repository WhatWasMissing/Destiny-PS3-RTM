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

  function float32FromBE(hex) {
    const normalized = String(hex).replace(/\s/g, '').toUpperCase();
    if (!/^(?:[0-9A-F]{2}){4}$/.test(normalized)) throw new Error('Expected exactly four bytes of hex');
    const bytes = Uint8Array.from(normalized.match(/../g), byte => parseInt(byte, 16));
    return new DataView(bytes.buffer).getFloat32(0, false);
  }

  const BRIGHTNESS_ADDRESS = 0x006101C0;
  const BRIGHTNESS_PRESETS = Object.freeze({
    bright: Object.freeze({ label: 'Bright', value: 50 }),
    normal: Object.freeze({ label: 'Normal', value: 1 }),
    inverted: Object.freeze({ label: 'Inverted', value: -20 })
  });
  // The emblem scan covers a broad player-memory range. These bytes are used
  // by the source wallet editor; 0x600E66C0 contains its 0x0366 marker.
  const EMBLEM_EXCLUDED_RANGES = Object.freeze([
    Object.freeze([0x600E66B0, 0x600E66B4]),
    Object.freeze([0x600E66C0, 0x600E66C2]),
    Object.freeze([0x600E66D0, 0x600E66D4]),
    Object.freeze([0x600E66F0, 0x600E66F4])
  ]);

  function brightnessPreset(name) {
    const preset = BRIGHTNESS_PRESETS[name];
    if (!preset) throw new Error('Unknown brightness preset');
    const hex = beFloat32(preset.value).map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
    return { name, label: preset.label, value: preset.value, hex };
  }

  function brightnessPresetForHex(hex) {
    const normalized = String(hex).replace(/\s/g, '').toUpperCase();
    return Object.keys(BRIGHTNESS_PRESETS).map(brightnessPreset).find(preset => preset.hex === normalized) || null;
  }

  function classifyDamagePatch(value) {
    const hex = String(value ?? '').replace(/[^0-9a-f]/gi, '').toUpperCase();
    if (!/^[0-9A-F]{4}$/.test(hex)) throw new Error('Damage patch must be exactly two bytes');
    const known = {
      '3F80': { kind: 'normal', label: 'Normal', multiplier: 1, isOneHit: false, isTenX: false },
      '4120': { kind: 'ten-x', label: '10× preset', multiplier: 10, isOneHit: false, isTenX: true },
      '447A': { kind: 'one-hit-main', label: 'One-Hit (main 447A variant)', multiplier: 1000, isOneHit: true, isTenX: false },
      '547A': { kind: 'one-hit-overwalls', label: 'One-Hit (Overwalls 547A variant)', multiplier: null, isOneHit: true, isTenX: false }
    };
    return Object.freeze({ hex, ...(known[hex] || { kind: 'custom', label: 'Custom', multiplier: null, isOneHit: false, isTenX: false }) });
  }

  function findEmblemMatches(bytes, baseAddress, emblemId, excludedRanges = []) {
    const needle = beU16(emblemId);
    const matches = [];
    for (let offset = 0; offset + 1 < bytes.length; offset += 2) {
      const address = baseAddress + offset;
      const excluded = excludedRanges.some(range => Array.isArray(range) && range.length === 2 &&
        Number.isFinite(range[0]) && Number.isFinite(range[1]) && address < range[1] && address + 2 > range[0]);
      if (!excluded && bytes[offset] === needle[0] && bytes[offset + 1] === needle[1]) matches.push(address);
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

  const api = { bytesFromHex, beU16, beU32, beFloat32, float32FromBE, BRIGHTNESS_ADDRESS, BRIGHTNESS_PRESETS, brightnessPreset, brightnessPresetForHex, classifyDamagePatch, EMBLEM_EXCLUDED_RANGES, findEmblemMatches, findAmmoCandidates, findSpecialAmmoCandidates, framebufferToRgba };
  root.RtmMemoryFeatures = api;
  if (typeof module !== 'undefined') module.exports = api;
})(globalThis);
