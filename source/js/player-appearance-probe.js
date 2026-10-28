// Read-only BLES 01.24 player appearance window probe.
// Layout constants come from independent BLES instruction analysis; owner
// identifiers are compared transiently and are never emitted.
(function (root) {
  'use strict';

  const PLAYER_POOL_ROOT = 0x1B29450;
  const LOCAL_PLAYER_ROOT = 0x1B29458;
  const EXPECTED_STRIDE = 0x2930;
  const MAX_SLOTS = 64;
  const PLAYER_IDENTITY_OFFSET = 0x620;
  const LOCAL_ACTIVE_OFFSET = 0xA8;
  const CHARACTER_OFFSET = 0x840;
  const APPEARANCE_OFFSET = 0x1510;
  const WINDOW_BYTES = 0x60;
  const MEMORY_START = 0x10000;
  const MEMORY_END = 0x40000000;
  const MAX_READ_BYTES = 0x600;
  const MAX_SNAPSHOT_READS = 96;
  const MAX_SNAPSHOT_BYTES = 0x1000;

  class ProbeDataError extends Error {}

  function checkedRange(address, size) {
    if (!Number.isSafeInteger(address) || !Number.isSafeInteger(size) || size < 1 ||
        address < MEMORY_START || address >= MEMORY_END ||
        address + size > MEMORY_END) {
      throw new ProbeDataError('A derived read is outside the bounded PS3 user-memory range');
    }
    return address;
  }

  function validPointer(address, label) {
    if (!Number.isInteger(address) || address < MEMORY_START || address >= MEMORY_END || (address & 3)) {
      throw new ProbeDataError(`${label} pointer is null, unaligned, or outside the bounded PS3 user-memory range`);
    }
    return address;
  }

  function fromHex(hex, size) {
    if (typeof hex !== 'string' || !new RegExp(`^(?:[0-9A-Fa-f]{2}){${size}}$`).test(hex)) {
      throw new ProbeDataError('Memory read was short or not valid hexadecimal');
    }
    const bytes = new Uint8Array(size);
    for (let index = 0; index < size; index++) bytes[index] = parseInt(hex.slice(index * 2, index * 2 + 2), 16);
    return bytes;
  }

  function hexFromBytes(bytes) {
    let hex = '';
    for (const byte of bytes) hex += byte.toString(16).padStart(2, '0');
    return hex.toUpperCase();
  }

  function sameBytes(left, right) {
    if (left.length !== right.length) return false;
    for (let index = 0; index < left.length; index++) if (left[index] !== right[index]) return false;
    return true;
  }

  function u32(bytes, offset = 0) {
    if (offset < 0 || offset + 4 > bytes.length) throw new ProbeDataError('Short player descriptor read');
    return (((bytes[offset] * 0x1000000) + (bytes[offset + 1] << 16) +
      (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0);
  }

  function hexAddress(value) {
    return `0x${value.toString(16).toUpperCase().padStart(8, '0')}`;
  }

  async function capture(api, owner, capability) {
    let reads = 0;
    let bytesRead = 0;
    async function read(address, size) {
      if (size > MAX_READ_BYTES) throw new ProbeDataError('A single memory read exceeds the inspector bound');
      checkedRange(address, size);
      if (++reads > MAX_SNAPSHOT_READS || bytesRead + size > MAX_SNAPSHOT_BYTES) {
        throw new ProbeDataError('Appearance sample reached its read budget');
      }
      bytesRead += size;
      return fromHex(await api.read(address, size, owner, capability), size);
    }
    async function readU32(address) { return u32(await read(address, 4)); }

    async function readRoots() {
      return {
        pool: validPointer(await readU32(PLAYER_POOL_ROOT), 'Player pool'),
        local: validPointer(await readU32(LOCAL_PLAYER_ROOT), 'Local player')
      };
    }

    async function readPoolHeader(address) {
      const fields = await read(address, 0x10);
      const active = (await read(address + 0x39, 1))[0];
      return {
        rawFields: fields,
        rawActive: active,
        bitmap: u32(fields, 0),
        storage: u32(fields, 4),
        stride: u32(fields, 8),
        upperBound: u32(fields, 0xC)
      };
    }

    async function readLocalIdentity(address) {
      // Retained only for this sample's equality check; never emitted.
      return await read(address + LOCAL_ACTIVE_OFFSET, 9);
    }

    const roots = await readRoots();
    const header = await readPoolHeader(roots.pool);
    const localIdentity = await readLocalIdentity(roots.local);
    const localActive = localIdentity[0] !== 0;
    const upperBound = header.upperBound;
    const scanBound = Math.min(upperBound, MAX_SLOTS);

    if (header.rawActive === 0) {
      throw new ProbeDataError('Player pool manager is inactive; player-slot scan refused');
    }
    if (header.stride !== EXPECTED_STRIDE) {
      throw new ProbeDataError(`Unexpected player record stride ${hexAddress(header.stride)}; expected ${hexAddress(EXPECTED_STRIDE)}`);
    }
    if (header.bitmap) validPointer(header.bitmap, 'Player pool bitmap');
    if (header.storage) validPointer(header.storage, 'Player record storage');
    if (scanBound && (!header.bitmap || !header.storage)) {
      throw new ProbeDataError('Player pool bitmap or record storage is not initialized');
    }
    if (scanBound) {
      checkedRange(header.storage, scanBound * EXPECTED_STRIDE);
      checkedRange(header.bitmap, Math.ceil(scanBound / 32) * 4);
    }

    const activeSlots = [];
    const bitmapWords = new Map();
    const matchingRows = [];
    const localOwner = localIdentity.slice(1); // Private transient match key; never returned.
    for (let slot = 0; slot < scanBound; slot++) {
      const wordIndex = slot >>> 5;
      if (!bitmapWords.has(wordIndex)) {
        bitmapWords.set(wordIndex, await readU32(header.bitmap + wordIndex * 4));
      }
      if (((bitmapWords.get(wordIndex) >>> (slot & 31)) & 1) === 0) continue;
      activeSlots.push(slot);
      if (!localActive) continue;

      const record = header.storage + slot * EXPECTED_STRIDE;
      checkedRange(record, EXPECTED_STRIDE);
      const recordOwner = await read(record + PLAYER_IDENTITY_OFFSET, 8);
      if (sameBytes(recordOwner, localOwner)) {
        matchingRows.push({ slot, record, generation: await read(record, 2) });
      }
    }

    async function recheckBitmapWords() {
      for (const [wordIndex, initialValue] of bitmapWords) {
        const currentValue = await readU32(header.bitmap + wordIndex * 4);
        if (currentValue !== initialValue) {
          throw new ProbeDataError('Player pool occupancy bitmap changed during the sample; result refused');
        }
      }
      return true;
    }

    const result = {
      atomic: false,
      note: 'Sequential live-memory reads; rechecks detect some changes but cannot make this sample atomic. Owner bytes are withheld.',
      pool: {
        root_cell: hexAddress(PLAYER_POOL_ROOT),
        descriptor: hexAddress(roots.pool),
        bitmap_address: header.bitmap ? hexAddress(header.bitmap) : null,
        storage_address: header.storage ? hexAddress(header.storage) : null,
        stride_bytes: header.stride,
        upper_bound: upperBound,
        scanned_slots: scanBound,
        active_slots: activeSlots,
        bitmap_has_holes: activeSlots.length !== scanBound,
        scan_truncated: upperBound > MAX_SLOTS
      },
      local_player: {
        root_cell: hexAddress(LOCAL_PLAYER_ROOT),
        descriptor: hexAddress(roots.local),
        active: localActive,
        match_method: '8-byte owner equality against active pool records; owner value withheld'
      },
      reads: { count: reads, bytes: bytesRead }
    };

    if (matchingRows.length > 1) {
      throw new ProbeDataError('More than one active pool slot matched the local descriptor; sample refused as ambiguous');
    }

    if (matchingRows.length === 1 && upperBound <= MAX_SLOTS) {
      const match = matchingRows[0];
      const characterBytes = await read(match.record + CHARACTER_OFFSET, WINDOW_BYTES);
      const appearanceBytes = await read(match.record + APPEARANCE_OFFSET, WINDOW_BYTES);

      const rootsAgain = await readRoots();
      const headerAgain = await readPoolHeader(roots.pool);
      const localIdentityAgain = await readLocalIdentity(roots.local);
      const bitmapWordsUnchanged = await recheckBitmapWords();
      const finalGeneration = await read(match.record, 2);
      const finalOwner = await read(match.record + PLAYER_IDENTITY_OFFSET, 8);
      const rootPointersUnchanged = rootsAgain.pool === roots.pool && rootsAgain.local === roots.local;
      const headerUnchanged = sameBytes(header.rawFields, headerAgain.rawFields) && header.rawActive === headerAgain.rawActive;
      const localIdentityUnchanged = sameBytes(localIdentity, localIdentityAgain);
      const slotStillActive = ((bitmapWords.get(match.slot >>> 5) >>> (match.slot & 31)) & 1) !== 0;
      const matchedRecordUnchanged = sameBytes(match.generation, finalGeneration) && sameBytes(localOwner, finalOwner);
      if (!rootPointersUnchanged || !headerUnchanged || !localIdentityUnchanged || !bitmapWordsUnchanged || !slotStillActive || !matchedRecordUnchanged) {
        throw new ProbeDataError('Roots, pool header, local identity, or matched slot changed during the sample; result refused');
      }

      result.local_player.matched_slot = match.slot;
      result.player_record = {
        address: hexAddress(match.record),
        stride_bytes: EXPECTED_STRIDE,
        windows: [
          { field: 'normalized character data', offset: hexAddress(CHARACTER_OFFSET), bytes: WINDOW_BYTES, hex: hexFromBytes(characterBytes) },
          { field: 'applied appearance data', offset: hexAddress(APPEARANCE_OFFSET), bytes: WINDOW_BYTES, hex: hexFromBytes(appearanceBytes) }
        ],
        rechecks: {
          root_pointers_unchanged: rootPointersUnchanged,
          pool_header_unchanged: headerUnchanged,
          local_identity_unchanged: localIdentityUnchanged,
          bitmap_words_unchanged: bitmapWordsUnchanged,
          slot_still_active: slotStillActive,
          matched_record_identity_unchanged: matchedRecordUnchanged
        }
      };
    } else if (matchingRows.length === 1) {
      const match = matchingRows[0];
      const rootsAgain = await readRoots();
      const headerAgain = await readPoolHeader(roots.pool);
      const localIdentityAgain = await readLocalIdentity(roots.local);
      const bitmapWordsUnchanged = await recheckBitmapWords();
      const finalGeneration = await read(match.record, 2);
      const finalOwner = await read(match.record + PLAYER_IDENTITY_OFFSET, 8);
      const rootPointersUnchanged = rootsAgain.pool === roots.pool && rootsAgain.local === roots.local;
      const headerUnchanged = sameBytes(header.rawFields, headerAgain.rawFields) && header.rawActive === headerAgain.rawActive;
      const localIdentityUnchanged = sameBytes(localIdentity, localIdentityAgain);
      const slotStillActive = ((bitmapWords.get(match.slot >>> 5) >>> (match.slot & 31)) & 1) !== 0;
      const matchedRecordUnchanged = sameBytes(match.generation, finalGeneration) && sameBytes(localOwner, finalOwner);
      if (!rootPointersUnchanged || !headerUnchanged || !localIdentityUnchanged || !bitmapWordsUnchanged || !slotStillActive || !matchedRecordUnchanged) {
        throw new ProbeDataError('Roots, pool header, local identity, or matched slot changed during the sample; result refused');
      }
      result.local_player.matched_slot = null;
      result.local_player.match_status = `Pool upper bound exceeds the ${MAX_SLOTS}-slot cap; partial matches are not accepted`;
      result.local_player.rechecks = {
        root_pointers_unchanged: rootPointersUnchanged,
        pool_header_unchanged: headerUnchanged,
        local_identity_unchanged: localIdentityUnchanged,
        bitmap_words_unchanged: bitmapWordsUnchanged,
        partial_slot_still_active: slotStillActive,
        partial_record_identity_unchanged: matchedRecordUnchanged
      };
    } else {
      const rootsAgain = await readRoots();
      const headerAgain = await readPoolHeader(roots.pool);
      const localIdentityAgain = await readLocalIdentity(roots.local);
      const bitmapWordsUnchanged = await recheckBitmapWords();
      if (rootsAgain.pool !== roots.pool || rootsAgain.local !== roots.local ||
          !sameBytes(header.rawFields, headerAgain.rawFields) || header.rawActive !== headerAgain.rawActive ||
          !sameBytes(localIdentity, localIdentityAgain) || !bitmapWordsUnchanged) {
        throw new ProbeDataError('Roots, pool header, or local identity changed during the sample; result refused');
      }
      result.local_player.matched_slot = null;
      result.local_player.match_status = upperBound > MAX_SLOTS
        ? `Pool upper bound exceeds the ${MAX_SLOTS}-slot cap; partial scan found no accepted match`
        : (localActive ? 'No active pool slot matched the local descriptor' : 'Local descriptor is inactive');
      result.local_player.rechecks = {
        root_pointers_unchanged: true,
        pool_header_unchanged: true,
        local_identity_unchanged: true,
        bitmap_words_unchanged: bitmapWordsUnchanged
      };
    }

    result.reads = { count: reads, bytes: bytesRead };
    return result;
  }

  let busy = false;
  async function inspectLocalPlayerAppearance() {
    const button = document.getElementById('player-appearance-probe-btn');
    const status = document.getElementById('player-appearance-probe-status');
    const output = document.getElementById('player-appearance-probe-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    output.hidden = true;
    output.textContent = '';
    status.dataset.state = 'pending';
    status.textContent = 'Checking the BLES01857 01.24 signature and reading one bounded player sample…';
    try {
      if (!api) throw new Error('Read-only session memory API is unavailable');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before inspecting a player');
      const build = await api.verify(owner);
      const result = await capture(api, owner, build.capability);
      await api.verify(owner);
      if (api.session() !== owner) throw new Error('Console session changed during inspection; discard this sample');
      result.build_profile = 'BLES01857 01.24';
      result.build_signature = `${build.anchors}/${root.RtmBuildCompatibility.anchorCount} anchors matched; this is not a full EBOOT hash or region check`;
      result.sampled_utc = new Date().toISOString();
      output.textContent = JSON.stringify(result, null, 2);
      output.hidden = false;
      status.dataset.state = 'matched';
      status.textContent = result.pool.scan_truncated
        ? 'Partial pool sample: the 64-slot cap was reached, so no appearance record was accepted.'
        : (result.local_player.matched_slot === null
          ? 'Bounded sample complete; no local player record matched. Reads are sequential and non-atomic.'
          : `Sample complete for pool slot ${result.local_player.matched_slot}. Owner bytes are withheld; the reads are sequential and non-atomic.`);
    } catch (error) {
      status.dataset.state = 'error';
      status.textContent = `Appearance probe refused or incomplete: ${String(error?.message || error)}`;
    } finally {
      busy = false;
      button.disabled = false;
    }
  }

  root.inspectLocalPlayerAppearance = inspectLocalPlayerAppearance;
})(window);
