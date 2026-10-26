(function (root) {
  'use strict';

  const NAME_POINTER = 0x01897C48;
  const OBJECTS_LIST_POINTER = 0x01B29450;
  const OBJECTS_BASE_POINTER = 0x01A1D9E8;
  const LOOKUP_TABLE = 0x01A21CB0;
  const RESOLVER = 0x002FF2D0;
  const LOOKUP_GETTER = 0x001FB3C8;
  const RESOLVER_SIGNATURE = '7C0802A648A99751F821FF81F801009038A0FFFF607F00002C04FFFF418200483C6001A2548404FE30C31CB08066000C7C8419D680E600083061007080C600247C87201490A100707885002080A500087CA530387C852010788400204BEFC09D80A1007090BF00003821008048A9974C';
  const LOOKUP_GETTER_SIGNATURE = '8084001C908300004E800020';
  const BLOCK_SIZE = 311600;
  const ENTRY_COUNT = 24;
  const ENTRY_STRIDE = 10544;
  const NAME_OFFSET = 1920;
  const HANDLE_OFFSET = 1604;
  const RECORD_STRIDE = 0xC0;
  const SCALE_OFFSET = 0x9C;
  const READ_CHUNK = 0x10000;
  const MAX_USER_ADDRESS = 0x7FFFFFFF;
  let busy = false;

  function userRange(address, length) {
    return Number.isInteger(address) && Number.isInteger(length) && address >= 0x10000 && length > 0 && address + length - 1 <= MAX_USER_ADDRESS;
  }

  function checkContext(context) {
    if (typeof root.ensureOperationSession === 'function') root.ensureOperationSession(context.generation);
    if (context.signal.aborted) throw new DOMException('Operation stopped', 'AbortError');
  }

  function createContext() {
    if (!root.ip) throw new Error('Connect to the Destiny process first');
    return { generation: root.connectionGeneration, signal: new AbortController().signal };
  }

  async function readHex(address, length, context) {
    if (!userRange(address, length)) throw new Error(`Invalid player-scale memory range at 0x${Number(address).toString(16).toUpperCase()}`);
    checkContext(context);
    const response = await root.ps3getmem(address, length);
    checkContext(context);
    const hex = String(response || '').replace(/\s/g, '').toUpperCase();
    if (hex.length !== length * 2 || !/^[0-9A-F]+$/.test(hex)) throw new Error(`Short or invalid read at 0x${address.toString(16).toUpperCase()}`);
    return hex;
  }

  async function readU32(address, context) {
    return parseInt(await readHex(address, 4, context), 16) >>> 0;
  }

  async function readRange(address, length, context) {
    if (!userRange(address, length)) throw new Error('Player-object memory block is outside the supported address range');
    let result = '';
    for (let offset = 0; offset < length; offset += READ_CHUNK) {
      const size = Math.min(READ_CHUNK, length - offset);
      result += await readHex(address + offset, size, context);
    }
    return result;
  }

  function asciiAt(hex, byteOffset, maxLength) {
    let result = '';
    for (let index = 0; index < maxLength; index++) {
      const position = (byteOffset + index) * 2;
      const byte = parseInt(hex.slice(position, position + 2), 16);
      if (!Number.isFinite(byte) || byte === 0) break;
      result += String.fromCharCode(byte);
    }
    return result;
  }

  function wordAt(hex, byteOffset) {
    const position = byteOffset * 2;
    if (position < 0 || position + 8 > hex.length) throw new Error('Player record is shorter than the source layout');
    return parseInt(hex.slice(position, position + 8), 16) >>> 0;
  }

  async function verifyCode(context) {
    const resolver = await readHex(RESOLVER, RESOLVER_SIGNATURE.length / 2, context);
    if (resolver !== RESOLVER_SIGNATURE) throw new Error('Player-size resolver bytes do not match the verified 1.24 source; no write was made');
    const getter = await readHex(LOOKUP_GETTER, LOOKUP_GETTER_SIGNATURE.length / 2, context);
    if (getter !== LOOKUP_GETTER_SIGNATURE) throw new Error('Player-size lookup helper bytes do not match the verified 1.24 source; no write was made');
  }

  async function lookupObjectIndex(handle, context) {
    const fields = await readHex(LOOKUP_TABLE + 8, 0x20, context);
    const base = wordAt(fields, 0);
    const stride = wordAt(fields, 4);
    const mask = wordAt(fields, 0x1C);
    if (!base || stride < 12 || stride > 0x10000) throw new Error('Player-handle lookup table is not initialized');
    const offset = (handle & 0x1FFF) * stride;
    const entry = base + offset;
    if (!userRange(entry, 12)) throw new Error('Player-handle table entry is outside the supported address range');
    const tag = await readU32(entry + 8, context);
    const maskedTag = tag & mask;
    const resolved = entry - maskedTag;
    if (!userRange(resolved + 0x1C, 4)) throw new Error('Resolved player object is outside the supported address range');
    const objectIndex = await readU32(resolved + 0x1C, context);
    if (objectIndex === 0xFFFFFFFF) throw new Error('Player-handle resolver returned its invalid -1 sentinel');
    return { base, stride, mask, entry, resolved, objectIndex };
  }

  async function resolveScaleTarget(context) {
    await verifyCode(context);
    const namePointer = await readU32(NAME_POINTER, context);
    const listPointer = await readU32(OBJECTS_LIST_POINTER, context);
    if (!userRange(namePointer + 0x90, 32) || !userRange(listPointer + 4, 4)) throw new Error('Player-name or object-list pointer is invalid');
    const playerName = asciiAt(await readHex(namePointer + 0x90, 32, context), 0, 32);
    if (!playerName) throw new Error('Current player name is empty; load into a character first');
    const objectsAddress = await readU32(listPointer + 4, context);
    if (!userRange(objectsAddress, BLOCK_SIZE)) throw new Error('Player-object records block is outside the supported address range');
    const records = await readRange(objectsAddress, BLOCK_SIZE, context);

    const matches = [];
    for (let index = 0; index < ENTRY_COUNT; index++) {
      const recordOffset = index * ENTRY_STRIDE;
      const recordName = asciiAt(records, recordOffset + NAME_OFFSET, 32);
      if (recordName === playerName) {
        matches.push({ recordOffset, handle: wordAt(records, recordOffset + HANDLE_OFFSET) });
      }
    }
    if (matches.length !== 1) throw new Error(matches.length ? `Player name matches ${matches.length} records; refusing an ambiguous scale write` : `Player ${playerName} was not found in the source object list`);
    const { recordOffset, handle } = matches[0];
    if (!handle || handle === 0xFFFFFFFF) throw new Error('Player object has a null or invalid handle');

    const lookup = await lookupObjectIndex(handle, context);
    const objectsBase = await readU32(OBJECTS_BASE_POINTER, context);
    if (!userRange(objectsBase, 4)) throw new Error('Scale-object base is null or invalid');
    const slot = lookup.objectIndex & 0x1FFF;
    const scaleAddress = objectsBase + slot * RECORD_STRIDE + SCALE_OFFSET;
    if (!userRange(scaleAddress, 4)) throw new Error('Resolved player scale field is outside the supported address range');
    const scaleRawHex = await readHex(scaleAddress, 4, context);
    const scale = readFloatBE(scaleRawHex);
    if (!Number.isFinite(scale)) throw new Error('Resolved player scale field is not a finite Float32');

    return {
      playerName, namePointer, listPointer, objectsAddress,
      recordAddress: objectsAddress + recordOffset,
      handle, lookup, objectsBase, slot, scaleAddress, scaleRawHex, scale
    };
  }

  async function verifyScaleTarget(state, context) {
    await verifyCode(context);
    if (await readU32(NAME_POINTER, context) !== state.namePointer || await readU32(OBJECTS_LIST_POINTER, context) !== state.listPointer) {
      throw new Error('Player identity pointers changed during the scale operation; no write was made');
    }
    if (await readU32(state.listPointer + 4, context) !== state.objectsAddress) throw new Error('Player-object records moved during the scale operation; no write was made');
    if (asciiAt(await readHex(state.namePointer + 0x90, 32, context), 0, 32) !== state.playerName) throw new Error('Current player name changed during the scale operation; no write was made');
    if (asciiAt(await readHex(state.recordAddress + NAME_OFFSET, 32, context), 0, 32) !== state.playerName || await readU32(state.recordAddress + HANDLE_OFFSET, context) !== state.handle) {
      throw new Error('Player record changed during the scale operation; no write was made');
    }
    const lookup = await lookupObjectIndex(state.handle, context);
    if (lookup.base !== state.lookup.base || lookup.stride !== state.lookup.stride || lookup.mask !== state.lookup.mask ||
        lookup.entry !== state.lookup.entry || lookup.resolved !== state.lookup.resolved || lookup.objectIndex !== state.lookup.objectIndex) {
      throw new Error('Player-handle lookup changed during the scale operation; no write was made');
    }
    if (await readU32(OBJECTS_BASE_POINTER, context) !== state.objectsBase) throw new Error('Scale-object base changed during the operation; no write was made');
    if (await readHex(state.scaleAddress, 4, context) !== state.scaleRawHex) throw new Error('Player scale field changed during the operation; no write was made');
  }

  function readFloatBE(hex) {
    const bytes = new Uint8Array(hex.match(/.{2}/g).map(part => parseInt(part, 16)));
    return new DataView(bytes.buffer).getFloat32(0, false);
  }

  function showStatus(message, type) {
    const status = document.getElementById('player-scale-status');
    if (status) status.textContent = message;
    if (root.log) root.log(message, type || '');
  }

  function displayScale(value) {
    const slider = document.getElementById('player-scale-slider');
    const label = document.getElementById('player-scale-value');
    const rounded = Math.round(value * 100) / 100;
    if (slider && rounded >= 0.3 && rounded <= 20) slider.value = String(rounded);
    if (label) label.textContent = `${rounded}×`;
  }

  async function runScaleAction(action) {
    if (busy) return;
    busy = true;
    try {
      return await action();
    } finally {
      busy = false;
    }
  }

  async function readPlayerScale() {
    return runScaleAction(async () => {
      try {
        const context = createContext();
        const state = await resolveScaleTarget(context);
        displayScale(state.scale);
        showStatus(`Read ${state.playerName}: ${state.scale}× at 0x${state.scaleAddress.toString(16).toUpperCase()}`, 'ok');
        return state;
      } catch (error) {
        showStatus(`Player size read failed: ${error.message}`, 'err');
        return null;
      }
    });
  }

  async function writePlayerScale(value) {
    return runScaleAction(async () => {
      try {
        const scale = Number(value);
        if (!Number.isFinite(scale) || scale < 0.3 || scale > 20) throw new Error('Scale must be between 0.3× and 20×');
        const context = createContext();
        const state = await resolveScaleTarget(context);
        await verifyScaleTarget(state, context);
        const valueHex = root.floatToHex(scale);
        checkContext(context);
        await root.verifiedMemoryValue(state.scaleAddress.toString(16).toUpperCase().padStart(8, '0'), valueHex);
        checkContext(context);
        const readback = await readHex(state.scaleAddress, 4, context);
        if (readback !== valueHex) throw new Error(`Readback mismatch: expected ${valueHex}, got ${readback}`);
        displayScale(scale);
        showStatus(`Set ${state.playerName} to ${scale}× · bytes verified at 0x${state.scaleAddress.toString(16).toUpperCase()}`, 'ok');
      } catch (error) {
        showStatus(`Player size write not confirmed: ${error.message}`, 'err');
      }
    });
  }

  function setPlayerScale() {
    const slider = document.getElementById('player-scale-slider');
    return writePlayerScale(slider ? slider.value : 1);
  }

  function resetPlayerScale() {
    displayScale(1);
    return writePlayerScale(1);
  }

  function setPlayerScaleValue(value) {
    displayScale(Number(value));
    return writePlayerScale(value);
  }

  function syncPlayerScaleValue(value) {
    const label = document.getElementById('player-scale-value');
    if (label) label.textContent = `${Number(value)}×`;
  }

  Object.assign(root, {
    readPlayerScale,
    setPlayerScale,
    setPlayerScaleValue,
    resetPlayerScale,
    syncPlayerScaleValue
  });
})(globalThis);
