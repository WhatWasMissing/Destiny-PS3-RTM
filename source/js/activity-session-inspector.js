// SPDX-License-Identifier: GPL-2.0-only
// Session snapshot algorithm ported from p3n1cillin/d1reimagined-source
// scripts/rpcs3_final124_session.py at commit 6501804. PS3MAPI gating, read
// budgets, and the UI adapter are RTM-specific. See third-party-licenses/GPL-2.0.txt.
(function (root) {
  'use strict';

  const ENGINE_PTR = 0x1ABC8AC;
  const MANAGER_PTR = 0x1ABC8A4;
  const SLOT_MAP_PTR = 0x1ABC79C;
  const CONTEXT_DESCRIPTOR = 0x1A20C70;
  const DATUM_TABLE_PTR = 0x1896D50;
  const SLICE_TABLE_PTR = 0x1A1DC80;
  const UPDATE_GATE_FLAGS_PTR = 0x1B28860;
  const UPDATE_GATE_COUNT_PTR = 0x1B28864;
  const ACTIVITY_CLOCK_PTR = 0x1AB6710;
  // BLES01857 01.24 initializer 0xBE148 confirms these registry names,
  // element sizes, and capacities. Only C70's descriptor fields are decoded:
  // caller 0x1D56D0 passes it to resolver 0x108960. B70/BF0 stay raw-only.
  const ACTIVITY_POOL_DESCRIPTORS = Object.freeze([
    Object.freeze({ name: 'Activity clients', address: 0x1A20B70, element_bytes: 0x6BC0, capacity: 3 }),
    Object.freeze({ name: 'Activity client worlds', address: 0x1A20BF0, element_bytes: 0x0B80, capacity: 3 }),
    Object.freeze({ name: 'Activity client world globals', address: 0x1A20C70, element_bytes: 0x5710, capacity: 3 })
  ]);
  const MEMORY_START = 0x10000;
  const MEMORY_END = 0x40000000;
  const MAX_READ_BYTES = 0x600;
  const MAX_SNAPSHOT_READS = 192;
  const MAX_SNAPSHOT_BYTES = 0x8000;

  class SnapshotDataError extends Error {}

  function checkedRegion(address, size) {
    if (!Number.isSafeInteger(address) || !Number.isSafeInteger(size) || size < 1 ||
        address < MEMORY_START || address >= MEMORY_END ||
        address + size > MEMORY_END) {
      throw new Error('Derived address or region is outside the bounded PS3 user-memory range');
    }
    return address;
  }

  function checkedAddress(address, size) {
    if (!Number.isInteger(size) || size > MAX_READ_BYTES) throw new Error('Read length exceeds the inspector bound');
    return checkedRegion(address, size);
  }

  function checkedAdd(base, offset, size = 1) {
    if (!Number.isSafeInteger(base) || !Number.isSafeInteger(offset) || offset < 0) {
      throw new Error('Invalid pointer arithmetic in session snapshot');
    }
    return checkedAddress(base + offset, size);
  }

  function bytesFromHex(hex) {
    const bytes = new Uint8Array(hex.length / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    return bytes;
  }

  function hexFromBytes(bytes) {
    let result = '';
    for (const byte of bytes) result += byte.toString(16).padStart(2, '0');
    return result;
  }

  function sameBytes(left, right) {
    if (left.length !== right.length) return false;
    for (let i = 0; i < left.length; i++) if (left[i] !== right[i]) return false;
    return true;
  }

  function u32(bytes, offset = 0) {
    if (offset < 0 || offset + 4 > bytes.length) throw new SnapshotDataError('Short structured read');
    return (((bytes[offset] * 0x1000000) + (bytes[offset + 1] << 16) +
      (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0);
  }

  function i32(bytes, offset = 0) {
    return u32(bytes, offset) | 0;
  }

  function rotl32(value, shift) {
    return ((value << shift) | (value >>> (32 - shift))) >>> 0;
  }

  async function snapshot(api, owner, capability) {
    let readCount = 0;
    let readBytes = 0;
    async function read(address, size) {
      checkedAddress(address, size);
      if (++readCount > MAX_SNAPSHOT_READS || readBytes + size > MAX_SNAPSHOT_BYTES) {
        throw new Error('Snapshot read budget reached; take a smaller or simpler sample');
      }
      readBytes += size;
      return bytesFromHex(await api.read(address, size, owner, capability));
    }
    async function readU32(address) { return u32(await read(address, 4)); }
    async function checkedReadU32(base, offset) { return readU32(checkedAdd(base, offset, 4)); }

    function pair(bytes, absoluteOffset) {
      const offset = absoluteOffset - 0x88;
      return [i32(bytes, offset), u32(bytes, offset + 4)];
    }

    async function resolveContext(handle) {
      if (handle === 0xFFFFFFFF) return null;
      const descriptor = await read(CONTEXT_DESCRIPTOR, 0x34);
      const storage = u32(descriptor, 8);
      const offset = u32(descriptor, 0x18);
      const stride = u32(descriptor, 0x1C);
      const mask = u32(descriptor, 0x20);
      const flags = u32(descriptor, 0x30);
      if (!storage || !stride || stride > 0x100000) throw new SnapshotDataError('Uninitialized or unexpected context descriptor');

      const index = handle & 0x1FFF;
      const row = checkedAdd(storage, offset + index * stride, 4);
      const generation = await readU32(row);
      const data = generation & mask;
      let packed;
      if (flags & 0x40000000) {
        packed = ((mask | 0x80000000) & ~0x40000000) >>> 0;
        packed = ((packed & ~0x38000000) | (rotl32(data, 27) & 0x38000000)) >>> 0;
        packed = ((packed & ~0x07FFE000) | (rotl32(flags, 13) & 0x07FFE000)) >>> 0;
      } else {
        packed = mask & 0x7FFFFFFF;
        packed = ((packed & ~0x7F800000) | (rotl32(data, 23) & 0x7F800000)) >>> 0;
        packed = ((packed & ~0x007FE000) | (rotl32(flags, 13) & 0x007FE000)) >>> 0;
      }
      packed = ((packed & 0xFFFFE000) | index) >>> 0;
      const shifted = (packed | 0) >> 13;
      const group = (shifted & ((shifted >>> 18) | 0x3FF)) >>> 0;
      if (group > 0xFFFF) throw new SnapshotDataError('Context datum group is outside the inspector bound');

      const tablePointer = await readU32(DATUM_TABLE_PTR);
      const table = await readU32(tablePointer);
      const entry = checkedAdd(table, group * 0x40, 0x38);
      const raw = await read(entry, 0x38);
      const datumStride = u32(raw, 0x30);
      const datumStorage = u32(raw, 0x10);
      const datumMask = u32(raw, 0x34);
      if (!datumStorage || !datumStride || datumStride > 0x100000) throw new SnapshotDataError('Uninitialized or unexpected datum descriptor');
      const datum = checkedAdd(datumStorage, index * datumStride, 0xC);
      const tag = await checkedReadU32(datum, 8);
      const address = (datum - (tag & datumMask)) >>> 0;
      checkedRegion(address, 0x5088);
      const descriptorAgain = await read(CONTEXT_DESCRIPTOR, 0x34);
      const generationAgain = await readU32(row);
      const tablePointerAgain = await readU32(DATUM_TABLE_PTR);
      const tableAgain = await readU32(tablePointer);
      const rawAgain = await read(entry, 0x38);
      const tagAgain = await checkedReadU32(datum, 8);
      return {
        address: `0x${address.toString(16).toUpperCase()}`,
        handle: `0x${handle.toString(16).padStart(8, '0').toUpperCase()}`,
        datum_group: group,
        lookup_rechecks_match: sameBytes(descriptor, descriptorAgain) && generationAgain === generation &&
          tablePointerAgain === tablePointer && tableAgain === table && sameBytes(raw, rawAgain) && tagAgain === tag
      };
    }

    const engine = await readU32(ENGINE_PTR);
    const manager = await readU32(MANAGER_PTR);
    const mapping = await readU32(SLOT_MAP_PTR);
    if (!engine || !manager || !mapping) throw new SnapshotDataError('Activity engine or session manager is not initialized');
    const engineRaw = await read(checkedAdd(engine, 0x88, 0x4E0), 0x4E0);
    const selectedRaw = await read(mapping, 16);
    const slots = [0, 1, 2, 3].map(index => u32(selectedRaw, index * 4));
    const gateFlags = await readU32(UPDATE_GATE_FLAGS_PTR);
    const gateCount = await readU32(UPDATE_GATE_COUNT_PTR);
    const gateOperands = gateFlags ? Array.from(await read(gateFlags, 2)) : [0, 0];
    const gateValue = gateCount ? await readU32(gateCount) : 0;
    const clock = await readU32(ACTIVITY_CLOCK_PTR);
    const sessions = [];

    for (const slot of [...new Set(slots.filter(value => value !== 0xFFFFFFFF))].sort((a, b) => a - b)) {
      if (slot >= 16) throw new SnapshotDataError(`Unexpected mapped activity slot ${slot}`);
      const record = checkedAdd(manager, 0x4E0 + slot * 0x170, 0x170);
      const handleAddress = checkedAdd(mapping, slot * 8 + 0x14, 4);
      const state = await readU32(record);
      const handle = await readU32(handleAddress);
      const session = {
        slot,
        state,
        context_handle: `0x${handle.toString(16).padStart(8, '0').toUpperCase()}`,
        transport_handle: `0x${(await checkedReadU32(record, 0x70)).toString(16).padStart(8, '0').toUpperCase()}`
      };
      if (state === 6 && handle !== 0xFFFFFFFF) {
        try {
          const lookup = await resolveContext(handle);
          const address = parseInt(lookup.address.slice(2), 16) >>> 0;
          const header = await read(address, 0x34);
          const incoming = await read(checkedAdd(address, 0x5078, 16), 16);
          session.context = {
            lookup,
            header_00_33: hexFromBytes(header),
            active_4: header[4],
            deferred_5: header[5],
            pending_18: header[0x18],
            pending_since_1c: u32(header, 0x1C),
            pending_20: header[0x20],
            pending_since_30: u32(header, 0x30),
            incoming_5078_5087: hexFromBytes(incoming),
            unchanged_handle_state: (await readU32(handleAddress)) === handle && (await readU32(record)) === state
          };
        } catch (error) {
          if (!(error instanceof SnapshotDataError)) throw error;
          session.context_error = error.message;
        }
      }
      sessions.push(session);
    }

    const sliceTable = await readU32(SLICE_TABLE_PTR);
    const sliceRaw = await read(checkedAdd(sliceTable, 0x110C, 72), 72);
    const loadedWords = [];
    for (let index = 0; index < 16; index++) loadedWords.push(u32(sliceRaw, 8 + index * 4));
    const pointersUnchanged = (await readU32(ENGINE_PTR)) === engine &&
      (await readU32(MANAGER_PTR)) === manager && (await readU32(SLOT_MAP_PTR)) === mapping &&
      (await readU32(SLICE_TABLE_PTR)) === sliceTable && sameBytes(await read(mapping, 16), selectedRaw);
    const activityPools = [];
    for (const pool of ACTIVITY_POOL_DESCRIPTORS) {
      const descriptor = await read(pool.address, 0x34);
      const descriptorAgain = await read(pool.address, 0x34);
      const observation = {
        name: pool.name,
        descriptor_address: `0x${pool.address.toString(16).toUpperCase()}`,
        element_bytes_from_01_24_initializer: pool.element_bytes,
        capacity_from_01_24_initializer: pool.capacity,
        descriptor_hex: hexFromBytes(descriptor),
        descriptor_unchanged_during_read: sameBytes(descriptor, descriptorAgain),
        note: 'Descriptor only. Backing storage is not traversed or decoded.'
      };
      if (pool.address === 0x1A20C70) {
        observation.descriptor_fields = {
          storage_pointer_08: `0x${u32(descriptor, 8).toString(16).padStart(8, '0').toUpperCase()}`,
          offset_18: u32(descriptor, 0x18),
          stride_1c: u32(descriptor, 0x1C),
          mask_20: `0x${u32(descriptor, 0x20).toString(16).padStart(8, '0').toUpperCase()}`,
          flags_30: `0x${u32(descriptor, 0x30).toString(16).padStart(8, '0').toUpperCase()}`
        };
        observation.note = 'Field meanings are corroborated at the 01.24 context resolver. Backing storage is not traversed except by the guarded session-handle resolver.';
      } else {
        observation.note = 'Field meanings are not confirmed for this pool. Raw descriptor only; backing storage is not traversed.';
      }
      activityPools.push(observation);
    }

    return {
      atomic: false,
      note: 'Sequential live-memory reads; rechecks detect some changes but cannot make this sample atomic.',
      engine: {
        address: `0x${engine.toString(16).toUpperCase()}`,
        flags_88_8b: Array.from(engineRaw.slice(0, 4)),
        update_gate_bytes_80_81: Array.from(await read(checkedAdd(engine, 0x80, 2), 2)),
        last_sent_flags: Array.from(engineRaw.slice(0x1F0 - 0x88, 0x1F4 - 0x88)),
        native_state_differs_from_last_sent: !sameBytes(engineRaw.slice(0, 0x168), engineRaw.slice(0x168, 0x2D0)),
        native_current: pair(engineRaw, 0x90),
        native_target: pair(engineRaw, 0x130),
        last_sent_current: pair(engineRaw, 0x1F8),
        last_sent_target: pair(engineRaw, 0x298),
        engine_version: u32(engineRaw, 0x3B0 - 0x88),
        local_peer: u32(engineRaw, 0x3B4 - 0x88),
        reported_peer_byte: engineRaw[0x1D0 - 0x88],
        target_state_13c: engineRaw[0x13C - 0x88],
        target_phase_141: engineRaw[0x141 - 0x88],
        target_resolved_index_138: i32(engineRaw, 0x138 - 0x88),
        follow_player_phase_1d2: engineRaw[0x1D2 - 0x88],
        follow_record_raw_36c_393: hexFromBytes(engineRaw.slice(0x36C - 0x88, 0x394 - 0x88)),
        group0_raw: hexFromBytes(engineRaw.slice(0x3B8 - 0x88, 0x490 - 0x88)),
        group1_raw: hexFromBytes(engineRaw.slice(0x490 - 0x88, 0x568 - 0x88))
      },
      mapped_slots: slots.map(slot => slot === 0xFFFFFFFF ? null : slot),
      sessions,
      global_update_gate: {
        flags_pointer: gateFlags ? `0x${gateFlags.toString(16).toUpperCase()}` : null,
        count_pointer: gateCount ? `0x${gateCount.toString(16).toUpperCase()}` : null,
        flags: gateOperands,
        count: gateValue,
        predicate: Boolean(gateOperands.some(Boolean) && gateValue)
      },
      activity_clock: {
        pointer: clock ? `0x${clock.toString(16).toUpperCase()}` : null,
        tick_0c: clock ? await checkedReadU32(clock, 0xC) : null
      },
      active_loading: [i32(sliceRaw, 0), i32(sliceRaw, 4)],
      loaded_words: loadedWords,
      activity_pool_descriptors: activityPools,
      pointers_unchanged: pointersUnchanged,
      reads: { count: readCount, bytes: readBytes }
    };
  }

  let busy = false;
  async function inspectActivitySessions() {
    const button = document.getElementById('activity-session-inspect-btn');
    const status = document.getElementById('activity-session-status');
    const output = document.getElementById('activity-session-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;
    busy = true;
    button.disabled = true;
    output.hidden = true;
    output.textContent = '';
    status.dataset.state = 'pending';
    status.textContent = 'Checking the BLES01857 01.24 signature and reading one bounded snapshot…';
    try {
      if (!api) throw new Error('Read-only session memory API is unavailable');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before inspecting a session');
      const build = await api.verify(owner);
      const result = await snapshot(api, owner, build.capability);
      await api.verify(owner);
      if (api.session() !== owner) throw new Error('Console session changed during inspection; discard this sample');
      result.build_profile = 'BLES01857 01.24';
      result.build_signature = `${build.anchors}/3 anchors matched; this is not a full EBOOT hash or region check`;
      result.sampled_utc = new Date().toISOString();
      output.textContent = JSON.stringify(result, null, 2);
      output.hidden = false;
      status.dataset.state = 'matched';
      status.textContent = 'Snapshot complete. Reads are sequential and non-atomic; pointer and handle rechecks matched only where reported.';
    } catch (error) {
      status.dataset.state = 'error';
      status.textContent = `Session inspection refused or incomplete: ${String(error?.message || error)}`;
    } finally {
      busy = false;
      button.disabled = false;
    }
  }

  root.inspectActivitySessions = inspectActivitySessions;
})(window);
