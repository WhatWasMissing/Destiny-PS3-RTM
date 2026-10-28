(function (root, factory) {
  'use strict';

  const api = factory();
  if (typeof module === 'object' && module && module.exports) module.exports = api;
  if (root) root.RtmSpawnExecutorController = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  const PROFILE_NAME = 'BLES01857 01.24 signature profile';
  const JOURNAL_KEY = 'rtm_spawn_executor_journal_v1';
  const HOOK_ADDRESS = 0x00354074;
  const HOOK_ORIGINAL = '4BFFD07D';
  const HOOK_INSTALLED = '494E9F8D';
  const BOOTSTRAP_BASE = 0x0183E000;
  const PROGRAM_BASE = 0x0183E400;
  const DISPATCHER_HEADER = 0x01C74000;
  const TASK_TABLE = 0x01C74010;
  const DATA_BASE = 0x01C75000;
  const COMMAND_OFFSET = 0x150;
  const COMMAND_STRIDE = 0x370;
  const RESULT_OFFSET = 0x130;
  const RESULT_HEADER_BYTES = 0x40;
  const RESULT_ROW_BYTES = 0x10;
  const MEMORY_CHUNK = 0x100;
  const PROFILE_READ_LIMIT = 160;
  const PROFILE_BYTE_LIMIT = 0x7000;
  const DEFAULT_TIMEOUT_MS = 5000;
  const DEFAULT_POLL_MS = 30;
  const CANCEL_GRACE_MS = 1000;
  const LEGACY_BOOTSTRAP_SHA256 = '19168afea738565520cb7a803354398ff76222d91fad1a6ce7e8700ab4d10dea';
  const CORRECTED_BOOTSTRAP_SHA256 = '616e8294b969ad36fdb78844a0bafb7dcc9b11a25384c7288201c6aed6d24d1f';
  const SPAWN_PROGRAM_SHA256 = '3b4a46a3aa4246709d99c8e71a643dabdea134f5f387498d1cd375da0885fb76';

  function bytesToHex(bytes) {
    return Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('').toUpperCase();
  }

  function hexToBytes(hex) {
    const value = String(hex || '').replace(/\s/g, '').toUpperCase();
    if (!/^(?:[0-9A-F]{2})*$/.test(value)) throw new Error('Memory read returned invalid hexadecimal');
    const bytes = new Uint8Array(value.length / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = Number.parseInt(value.slice(i * 2, i * 2 + 2), 16);
    return bytes;
  }

  function u32(bytes, offset = 0) {
    if (offset < 0 || offset + 4 > bytes.length) throw new Error('Short big-endian U32 field');
    return (bytes[offset] * 0x1000000 + (bytes[offset + 1] << 16) + (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0;
  }

  function be32(value) {
    const bytes = new Uint8Array(4);
    new DataView(bytes.buffer).setUint32(0, value >>> 0, false);
    return bytes;
  }

  function repeatByteHex(byte, length) {
    return byte.toString(16).padStart(2, '0').repeat(length).toUpperCase();
  }

  function normalizeSessionHost(owner) {
    const value = String(owner || '');
    const separator = value.indexOf(':');
    return separator < 0 ? value : value.slice(separator + 1);
  }

  function parseResults(bytes, count) {
    if (!Number.isInteger(count) || count < 1 || count > 32) throw new RangeError('Spawn result count must be from 1 through 32');
    const required = RESULT_HEADER_BYTES + count * RESULT_ROW_BYTES;
    if (bytes.length < required) throw new Error(`Short spawn result block: ${bytes.length} bytes; expected ${required}`);
    const rows = [];
    for (let index = 0; index < count; index++) {
      const resultCode = (bytes[index * 2] << 8) | bytes[index * 2 + 1];
      const resultType = resultCode >>> 8;
      const detail = resultCode & 0xff;
      const offset = RESULT_HEADER_BYTES + index * RESULT_ROW_BYTES;
      rows.push({
        index,
        resultCode,
        resultCodeHex: `0x${resultCode.toString(16).toUpperCase().padStart(4, '0')}`,
        resultType,
        detail,
        actualTypeCandidate: resultType === 4 ? detail : null,
        interpretation: resultType === 0
          ? 'Blackrock host parser labels this not spawned (default result)'
          : resultType === 1
            ? 'Blackrock host parser labels this spawned; this code is not observed in the audited emitted path'
            : resultType === 2
              ? 'executor reported no handle returned'
              : resultType === 3
                ? 'Blackrock host parser labels this a dead handle, but the emitted worker also writes code 3 on other non-null paths; per-row outcome is ambiguous'
                : resultType === 4
                  ? 'Blackrock host parser labels this a type mismatch; the emitted worker does not appear to preserve this code'
                  : 'unknown result type; retain as raw code',
        handle: u32(bytes, offset),
        words: [u32(bytes, offset), u32(bytes, offset + 4), u32(bytes, offset + 8), u32(bytes, offset + 12)]
      });
    }
    return rows;
  }

  function create(options = {}) {
    const memory = options.memory;
    const profileAnchorCount = options.profileAnchorCount;
    const writeIfEqual = options.writeIfEqual;
    const signatures = options.signatures;
    const regions = options.regions;
    const packet = options.packet;
    const dataBlock = options.dataBlock;
    const images = options.images;
    const storage = options.storage || (typeof localStorage !== 'undefined' ? localStorage : null);
    const now = options.now || Date.now;
    const sleep = options.sleep || (ms => new Promise(resolve => setTimeout(resolve, ms)));
    const onProgress = options.onProgress || (() => {});
    if (!memory || typeof memory.session !== 'function' || typeof memory.verify !== 'function' || typeof memory.read !== 'function' ||
        !Number.isInteger(profileAnchorCount) || profileAnchorCount < 1 ||
        typeof writeIfEqual !== 'function' || !Array.isArray(signatures) || !Array.isArray(regions) ||
        !packet || !dataBlock || !images || !storage) {
      throw new TypeError('Spawn executor controller requires guarded memory, layouts, images, and persistent journal storage');
    }

    let busy = false;
    let activeCommand = null;

    function validatePendingCommand(command) {
      if (!command || !Number.isInteger(command.slotBase) ||
          !Array.from({ length: 8 }, (_, index) => DATA_BASE + COMMAND_OFFSET + index * COMMAND_STRIDE).includes(command.slotBase) ||
          !Number.isInteger(command.sequence) || command.sequence < 1 || command.sequence > 0xffffffff ||
          !Number.isInteger(command.count) || command.count < 1 || command.count > 32) {
        throw new Error('Spawn recovery journal contains an invalid pending command; no memory write was attempted');
      }
      return command;
    }

    function loadJournal() {
      let raw;
      try { raw = storage.getItem(JOURNAL_KEY); } catch (_) { throw new Error('Spawn recovery journal is unavailable'); }
      if (!raw) return null;
      try {
        const journal = JSON.parse(raw);
        const states = ['installing', 'installed', 'recovery-required', 'recovered-clean', 'recovered-resident'];
        if (!journal || journal.version !== 1 || !states.includes(journal.state) || typeof journal.targetHost !== 'string' ||
            journal.hookAddress !== HOOK_ADDRESS || journal.hookOriginal !== HOOK_ORIGINAL || journal.hookInstalled !== HOOK_INSTALLED ||
            journal.bootstrapBase !== BOOTSTRAP_BASE || journal.programBase !== PROGRAM_BASE || journal.dataBase !== DATA_BASE ||
            journal.descriptorAddress !== TASK_TABLE || journal.descriptorHex !== '0183E40001C75000' ||
            journal.dispatcherHeaderHex !== '4252583100000001' || !Number.isInteger(journal.sequence) ||
            journal.sequence < 0 || journal.sequence > 0xffffffff || !Number.isInteger(journal.spawnCount) || journal.spawnCount < 0) {
          throw new Error('Spawn recovery journal has an unknown format');
        }
        if (journal.pendingCommand !== null && journal.pendingCommand !== undefined) validatePendingCommand(journal.pendingCommand);
        return journal;
      } catch (error) {
        throw new Error(`Spawn recovery journal cannot be read: ${String(error?.message || error)}`);
      }
    }

    function saveJournal(journal) {
      const serialized = JSON.stringify(journal);
      try {
        storage.setItem(JOURNAL_KEY, serialized);
        if (storage.getItem(JOURNAL_KEY) !== serialized) throw new Error('journal readback did not match');
      } catch (error) {
        throw new Error(`Spawn recovery journal could not be saved: ${String(error?.message || error)}`);
      }
    }

    async function createReadSession(owner) {
      let profile = await memory.verify(owner);
      let reads = 0;
      let bytes = 0;
      function requireProfile(value) {
        if (!value || value.profile !== PROFILE_NAME || value.anchors !== profileAnchorCount) {
          throw new Error(`The ${profileAnchorCount}-anchor BLES01857 01.24 profile did not verify`);
        }
      }
      requireProfile(profile);

      async function refresh() {
        if (memory.session() !== owner) throw new Error('Console session changed during spawn operation');
        profile = await memory.verify(owner);
        requireProfile(profile);
        reads = 0;
        bytes = 0;
      }

      async function readHex(address, length) {
        if (!Number.isSafeInteger(address) || !Number.isInteger(length) || length < 1 || length > MEMORY_CHUNK ||
            address < 0x10000 || address >= 0x40000000 || address + length > 0x40000000) {
          throw new Error('Spawn executor read is outside its bounded 0x100-byte range');
        }
        if (memory.session() !== owner) throw new Error('Console session changed; spawn read refused');
        if (reads + 1 > PROFILE_READ_LIMIT || bytes + length > PROFILE_BYTE_LIMIT) await refresh();
        const raw = await memory.read(address, length, owner, profile.capability);
        const hex = String(raw || '').replace(/\s/g, '').toUpperCase();
        if (!/^(?:[0-9A-F]{2})+$/.test(hex) || hex.length !== length * 2) {
          throw new Error(`Short or invalid read at 0x${address.toString(16).toUpperCase()}`);
        }
        if (memory.session() !== owner) throw new Error('Console session changed during spawn read');
        reads++;
        bytes += length;
        return hex;
      }

      async function readBytes(address, length) {
        if (!Number.isSafeInteger(length) || length < 1) throw new Error('Invalid spawn read length');
        const output = new Uint8Array(length);
        for (let offset = 0; offset < length; offset += MEMORY_CHUNK) {
          const size = Math.min(MEMORY_CHUNK, length - offset);
          output.set(hexToBytes(await readHex(address + offset, size)), offset);
        }
        return output;
      }

      return {
        owner,
        readHex,
        readBytes,
        async recheck() {
          await refresh();
          if (memory.session() !== owner) throw new Error('Console session changed during BLES 01.24 recheck');
          return profile;
        }
      };
    }

    async function verifySpawnPath(owner, hookMode = 'original') {
      const reader = await createReadSession(owner);
      let hookState = '';
      for (const signature of signatures) {
        const expectedBytes = signature.expected.length / 2;
        const actual = await reader.readBytes(signature.address, expectedBytes);
        const observed = bytesToHex(actual);
        let allowed = [signature.expected];
        if (signature.address === HOOK_ADDRESS) {
          if (hookMode === 'installed') allowed = [HOOK_INSTALLED];
          else if (hookMode === 'either') allowed = [HOOK_ORIGINAL, HOOK_INSTALLED];
        }
        if (!allowed.includes(observed)) {
          throw new Error(`Spawn code signature mismatch at 0x${signature.address.toString(16).toUpperCase()}: read ${observed}`);
        }
        if (signature.address === HOOK_ADDRESS) hookState = observed === HOOK_INSTALLED ? 'installed' : 'original';
      }
      await reader.recheck();
      return { reader, hookState };
    }

    async function requireAllZero(owner) {
      const reader = await createReadSession(owner);
      let checkedBytes = 0;
      for (const region of regions) {
        for (let offset = 0; offset < region.size; offset += MEMORY_CHUNK) {
          const size = Math.min(MEMORY_CHUNK, region.size - offset);
          const observed = await reader.readHex(region.address + offset, size);
          checkedBytes += size;
          if (/[^0]/.test(observed)) {
            const first = observed.search(/[^0]/) / 2;
            throw new Error(`Executor scratch is occupied at 0x${(region.address + offset + first).toString(16).toUpperCase()}; no install writes were attempted`);
          }
        }
      }
      await reader.recheck();
      return checkedBytes;
    }

    async function writeBytes(owner, address, expected, replacement) {
      if (expected.length !== replacement.length) throw new Error('Compare-write image length mismatch');
      for (let offset = 0; offset < expected.length; offset += MEMORY_CHUNK) {
        if (memory.session() !== owner) throw new Error('Console session changed; spawn installation cancelled');
        const before = expected.subarray(offset, Math.min(offset + MEMORY_CHUNK, expected.length));
        const after = replacement.subarray(offset, Math.min(offset + MEMORY_CHUNK, replacement.length));
        await writeIfEqual(owner, address + offset, bytesToHex(before), bytesToHex(after));
      }
    }

    async function writeZeroFilled(owner, address, replacement) {
      for (let offset = 0; offset < replacement.length; offset += MEMORY_CHUNK) {
        if (memory.session() !== owner) throw new Error('Console session changed; spawn installation cancelled');
        const chunk = replacement.subarray(offset, Math.min(offset + MEMORY_CHUNK, replacement.length));
        await writeIfEqual(owner, address + offset, repeatByteHex(0, chunk.length), bytesToHex(chunk));
      }
    }

    function makeJournal(owner, descriptorAddress) {
      return {
        version: 1,
        id: `${now().toString(36)}-${Math.random().toString(36).slice(2, 10)}`,
        state: 'installing',
        phase: 'journaled',
        targetHost: normalizeSessionHost(owner),
        createdAt: new Date(now()).toISOString(),
        hookAddress: HOOK_ADDRESS,
        hookOriginal: HOOK_ORIGINAL,
        hookInstalled: HOOK_INSTALLED,
        bootstrapBase: BOOTSTRAP_BASE,
        programBase: PROGRAM_BASE,
        dataBase: DATA_BASE,
        descriptorAddress,
        descriptorHex: bytesToHex(new Uint8Array([0x01, 0x83, 0xe4, 0x00, 0x01, 0xc7, 0x50, 0x00])),
        dispatcherHeaderHex: '4252583100000001',
        sequence: 0,
        spawnCount: 0,
        pendingCommand: null
      };
    }

    function ensureSession(owner, journal) {
      if (!owner || memory.session() !== owner) throw new Error('Link Destiny on the same console before using the spawn executor');
      if (normalizeSessionHost(owner) !== journal.targetHost) throw new Error('Spawn recovery journal belongs to a different console IP');
    }

    async function install() {
      if (busy) throw new Error('A spawn executor operation is already running');
      busy = true;
      const owner = memory.session();
      try {
        if (!owner) throw new Error('Link the console and start Destiny before installing the spawn executor');
        const existing = loadJournal();
        if (existing && !['recovered-clean', 'recovered-resident'].includes(existing.state)) {
          throw new Error(`A ${existing.state} spawn executor journal exists; run Recover before installing again`);
        }
        const checkedPath = await verifySpawnPath(owner, 'original');
        if (checkedPath.hookState !== 'original') throw new Error('Executor hook is already patched; recover it before installing');
        const checkedBytes = await requireAllZero(owner);
        const bootstrap = images.createBootstrapExecutor();
        const program = images.createSpawnProgram();
        const data = dataBlock.buildDataBlock();
        if (bootstrap.length !== 292 || program.length !== 2908 || data.length !== 0x1cd0) {
          throw new Error('Reconstructed spawn image lengths do not match the audited BLES 01.24 layout');
        }
        const journal = makeJournal(owner, TASK_TABLE);
        journal.zeroCheckedBytes = checkedBytes;
        journal.programSha256 = images.metadata.spawn_program.sha256;
        journal.bootstrapSha256 = images.metadata.bootstrap_executor.sha256;
        saveJournal(journal); // Persist recovery information before the first console write.

        try {
          journal.phase = 'bootstrap'; saveJournal(journal);
          await writeZeroFilled(owner, BOOTSTRAP_BASE, bootstrap);
          journal.phase = 'spawn-program'; saveJournal(journal);
          await writeZeroFilled(owner, PROGRAM_BASE, program);
          journal.phase = 'data-block'; saveJournal(journal);
          await writeZeroFilled(owner, DATA_BASE, data);
          journal.phase = 'dispatcher-header'; saveJournal(journal);
          await writeIfEqual(owner, DISPATCHER_HEADER, repeatByteHex(0, 8), journal.dispatcherHeaderHex);
          journal.phase = 'task-registration'; saveJournal(journal);
          await writeIfEqual(owner, TASK_TABLE, repeatByteHex(0, 8), journal.descriptorHex);

          const beforeHook = await verifySpawnPath(owner, 'original');
          if (beforeHook.hookState !== 'original') throw new Error('Hook site changed before activation');
          journal.phase = 'hook-activation'; saveJournal(journal);
          await writeIfEqual(owner, HOOK_ADDRESS, HOOK_ORIGINAL, HOOK_INSTALLED);
          journal.state = 'installed';
          journal.phase = 'active';
          journal.installedAt = new Date(now()).toISOString();
          saveJournal(journal);
          return { state: journal.state, journal, note: 'Hook write was readback-verified; PPU instruction-cache behavior still needs a dev-console runtime check.' };
        } catch (error) {
          journal.state = 'recovery-required';
          journal.error = String(error?.message || error).slice(0, 500);
          journal.failedAt = new Date(now()).toISOString();
          try { saveJournal(journal); } catch (_) {}
          throw new Error(`Spawn executor install stopped during ${journal.phase}: ${journal.error}. Use Recover before retrying.`);
        }
      } finally {
        busy = false;
      }
    }

    async function readWord(reader, address) {
      return u32(hexToBytes(await reader.readHex(address, 4)));
    }

    async function pollCommand(owner, slotBase, sequence, count, timeoutMs, pollMs) {
      const reader = await createReadSession(owner);
      const deadline = now() + timeoutMs;
      let lastProgress = null;
      let lastStatus = null;
      async function sample() {
        const bytes = hexToBytes(await reader.readHex(slotBase, 0x1c));
        const status = u32(bytes, 0);
        const observedSequence = u32(bytes, 8);
        const progress = u32(bytes, 0x14);
        const successCount = u32(bytes, 0x18);
        if (observedSequence !== sequence) throw new Error('Spawn queue sequence changed; the command slot may have been reused');
        if (status !== 0 && status !== 1 && status !== 2 && status !== 3 && status !== 4) {
          throw new Error(`Spawn queue entered unexpected status ${status}`);
        }
        if (progress !== lastProgress || status !== lastStatus) {
          lastProgress = progress;
          lastStatus = status;
          onProgress({ slotBase, sequence, status, progress });
        }
        return { status, progress, successCount };
      }

      let sampleState;
      while (now() < deadline) {
        sampleState = await sample();
        if (sampleState.status === 3 || sampleState.status === 4) break;
        await sleep(Math.min(pollMs, Math.max(1, deadline - now())));
      }

      if (!sampleState || (sampleState.status !== 3 && sampleState.status !== 4)) {
        const cancelBefore = await reader.readHex(slotBase + 0x10, 4);
        if (cancelBefore !== '00000001') await writeIfEqual(owner, slotBase + 0x10, cancelBefore, '00000001');
        const cancelDeadline = now() + CANCEL_GRACE_MS;
        while (now() < cancelDeadline) {
          sampleState = await sample();
          if (sampleState.status === 3 || sampleState.status === 4) break;
          await sleep(Math.min(pollMs, Math.max(1, cancelDeadline - now())));
        }
        if (!sampleState || (sampleState.status !== 3 && sampleState.status !== 4)) {
          throw new Error('Spawn timed out; cancellation was requested but the worker did not finish. Leave the executor installed until the slot reaches status 3 or 4.');
        }
      }

      const beforeResults = hexToBytes(await reader.readHex(slotBase, 0x0c));
      if (u32(beforeResults, 0) !== sampleState.status || u32(beforeResults, 8) !== sequence) {
        throw new Error('Spawn slot changed before result retrieval; discard the result block');
      }
      const resultBytes = await reader.readBytes(slotBase + RESULT_OFFSET, RESULT_HEADER_BYTES + count * RESULT_ROW_BYTES);
      const afterResults = hexToBytes(await reader.readHex(slotBase, 0x0c));
      if (u32(afterResults, 0) !== sampleState.status || u32(afterResults, 8) !== sequence) {
        throw new Error('Spawn slot changed while results were read; discard the result block');
      }
      await reader.recheck();
      return {
        terminalStatus: sampleState.status,
        progress: sampleState.progress,
        successCount: sampleState.successCount,
        rows: parseResults(resultBytes, count)
      };
    }

    async function spawn(fields, optionsForSpawn = {}) {
      if (busy) throw new Error('A spawn executor operation is already running');
      busy = true;
      const owner = memory.session();
      let journal;
      let slotBase = 0;
      let sequence = 0;
      try {
        journal = loadJournal();
        if (!journal || journal.state !== 'installed') throw new Error('Install the BLES 01.24 spawn executor before submitting a spawn');
        ensureSession(owner, journal);
        const verified = await verifySpawnPath(owner, 'installed');
        if (verified.hookState !== 'installed') throw new Error('Spawn executor hook is not active');
        sequence = ((journal.sequence >>> 0) + 1) >>> 0;
        if (sequence === 0) sequence = 1;
        const command = packet.buildNativeOperation1Vector({ ...fields, sequence });
        const count = fields.count;
        let selectedSlot = null;
        for (let index = 0; index < 8; index++) {
          const candidate = DATA_BASE + COMMAND_OFFSET + index * COMMAND_STRIDE;
          const header = hexToBytes(await verified.reader.readHex(candidate, 0x0c));
          const status = u32(header, 0);
          const previousSequence = u32(header, 8);
          if (status === 1 || status === 2) continue;
          if (status !== 0 && status !== 3 && status !== 4) {
            throw new Error(`Spawn slot ${index} has unexpected status ${status}`);
          }
          if (selectedSlot === null) selectedSlot = { index, address: candidate, status, previousSequence };
        }
        if (selectedSlot === null) throw new Error('All eight spawn command slots are active');
        slotBase = selectedSlot.address;

        const beforePayloadHeader = hexToBytes(await verified.reader.readHex(slotBase, 0x0c));
        if (u32(beforePayloadHeader, 0) !== selectedSlot.status || u32(beforePayloadHeader, 8) !== selectedSlot.previousSequence) {
          throw new Error('Selected spawn slot changed during allocation; no payload write was attempted');
        }

        const payload = command.subarray(4);
        const previousPayload = await verified.reader.readBytes(slotBase + 4, payload.length);
        await writeBytes(owner, slotBase + 4, previousPayload, payload);

        const beforePublishHeader = hexToBytes(await verified.reader.readHex(slotBase, 0x0c));
        if (u32(beforePublishHeader, 0) !== selectedSlot.status || u32(beforePublishHeader, 8) !== sequence) {
          throw new Error('Selected spawn slot was claimed while its payload was being prepared; command was not published');
        }

        journal.sequence = sequence;
        journal.spawnCount = (journal.spawnCount || 0) + 1;
        journal.lastSlot = slotBase;
        journal.pendingCommand = { slotBase, sequence, count: fields.count };
        journal.lastSubmittedAt = new Date(now()).toISOString();
        saveJournal(journal); // Record possible ownership before publishing status=1.
        activeCommand = { owner, slotBase, sequence, count: fields.count };
        await writeIfEqual(owner, slotBase, bytesToHex(be32(selectedSlot.status)), '00000001');

        const result = await pollCommand(
          owner,
          slotBase,
          sequence,
          count,
          optionsForSpawn.timeoutMs || DEFAULT_TIMEOUT_MS,
          optionsForSpawn.pollMs || DEFAULT_POLL_MS
        );
        const finalProfile = await memory.verify(owner);
        if (memory.session() !== owner || finalProfile.profile !== PROFILE_NAME || finalProfile.anchors !== profileAnchorCount) {
          throw new Error('Final build/session recheck failed; discard spawn results');
        }
        journal.pendingCommand = null;
        journal.lastTerminalStatus = result.terminalStatus;
        journal.lastCompletedAt = new Date(now()).toISOString();
        saveJournal(journal);
        activeCommand = null;
        return {
          slot: slotBase,
          sequence,
          terminalStatus: result.terminalStatus,
          terminalMessage: result.terminalStatus === 3 ? 'Worker completed' : 'Worker reported command failure',
          progress: result.progress,
          successCount: result.successCount,
          rows: result.rows
        };
      } catch (error) {
        throw new Error(`Spawn request${slotBase ? ` at 0x${slotBase.toString(16).toUpperCase()}` : ''} failed: ${String(error?.message || error)}`);
      } finally {
        if (!journal?.pendingCommand) activeCommand = null;
        busy = false;
      }
    }

    async function cancelCurrent() {
      const journal = loadJournal();
      const currentOwner = memory.session();
      const command = activeCommand && activeCommand.owner === currentOwner ? activeCommand : (journal?.pendingCommand ? {
        owner: currentOwner,
        slotBase: journal.pendingCommand.slotBase,
        sequence: journal.pendingCommand.sequence,
        count: journal.pendingCommand.count
      } : null);
      if (!command) throw new Error('There is no active spawn request to cancel');
      if (!journal || journal.state !== 'installed') throw new Error('Spawn recovery journal is not in the installed state');
      validatePendingCommand(command);
      ensureSession(command.owner, journal);
      const reader = await createReadSession(command.owner);
      const header = hexToBytes(await reader.readHex(command.slotBase, 0x0c));
      const status = u32(header, 0);
      const sequence = u32(header, 8);
      if (sequence !== command.sequence) throw new Error('Spawn slot sequence changed; stale cancellation refused');
      if (status !== 1 && status !== 2) throw new Error(`Spawn slot is not active (status ${status}); cancellation refused`);
      const address = command.slotBase + 0x10;
      const current = await reader.readHex(address, 4);
      const beforeCancelWrite = hexToBytes(await reader.readHex(command.slotBase, 0x0c));
      if (u32(beforeCancelWrite, 0) !== status || u32(beforeCancelWrite, 8) !== command.sequence) {
        throw new Error('Spawn slot changed before cancellation; stale cancel refused');
      }
      if (current !== '00000001') await writeIfEqual(command.owner, address, current, '00000001');
      const afterCancelWrite = hexToBytes(await reader.readHex(command.slotBase, 0x0c));
      if (u32(afterCancelWrite, 8) !== command.sequence) throw new Error('Spawn slot was reused during cancellation; stop using this result');
      journal.cancelRequestedAt = new Date(now()).toISOString();
      saveJournal(journal);
      return { requested: true, slot: command.slotBase, sequence: command.sequence };
    }

    async function reconcilePending() {
      if (busy) throw new Error('A spawn executor operation is already running');
      busy = true;
      const owner = memory.session();
      try {
        const journal = loadJournal();
        if (!journal || journal.state !== 'installed' || !journal.pendingCommand) {
          throw new Error('There is no pending spawn request to reconcile');
        }
        ensureSession(owner, journal);
        const command = journal.pendingCommand;
        const verified = await verifySpawnPath(owner, 'installed');
        const reader = verified.reader;
        const before = hexToBytes(await reader.readHex(command.slotBase, 0x0c));
        const status = u32(before, 0);
        const sequence = u32(before, 8);
        if (sequence !== command.sequence) throw new Error('Spawn slot sequence changed; pending result cannot be safely reconciled');
        if (status === 1 || status === 2) return { pending: true, slot: command.slotBase, sequence, status };
        if (status !== 0 && status !== 3 && status !== 4) throw new Error(`Spawn slot has unexpected status ${status}`);

        let result = null;
        const successCount = status === 3 || status === 4 ? await readWord(reader, command.slotBase + 0x18) : 0;
        if (status === 3 || status === 4) {
          result = await reader.readBytes(command.slotBase + RESULT_OFFSET, RESULT_HEADER_BYTES + command.count * RESULT_ROW_BYTES);
        }
        const after = hexToBytes(await reader.readHex(command.slotBase, 0x0c));
        if (u32(after, 0) !== status || u32(after, 8) !== command.sequence) {
          throw new Error('Spawn slot changed during reconciliation; discard this sample');
        }
        await reader.recheck();
        const finalProfile = await memory.verify(owner);
        if (memory.session() !== owner || finalProfile.profile !== PROFILE_NAME || finalProfile.anchors !== profileAnchorCount) {
          throw new Error('Final build/session recheck failed; discard reconciliation sample');
        }
        journal.pendingCommand = null;
        journal.lastTerminalStatus = status;
        journal.lastCompletedAt = new Date(now()).toISOString();
        saveJournal(journal);
        activeCommand = null;
        return {
          pending: false,
          slot: command.slotBase,
          sequence,
          terminalStatus: status,
          successCount,
          terminalMessage: status === 0 ? 'Slot is idle; no worker completion was recorded' : status === 3 ? 'Worker completed' : 'Worker reported command failure',
          rows: result ? parseResults(result, command.count) : []
        };
      } finally {
        busy = false;
      }
    }

    async function cleanOwnedImage(owner, reader, address, expectedBytes) {
      let allClean = true;
      for (let offset = 0; offset < expectedBytes.length; offset += MEMORY_CHUNK) {
        const expected = expectedBytes.subarray(offset, Math.min(offset + MEMORY_CHUNK, expectedBytes.length));
        const actual = await reader.readHex(address + offset, expected.length);
        const expectedHex = bytesToHex(expected);
        const zeroHex = repeatByteHex(0, expected.length);
        if (actual === zeroHex) continue;
        if (actual !== expectedHex) {
          allClean = false;
          continue;
        }
        await writeIfEqual(owner, address + offset, expectedHex, zeroHex);
      }
      return allClean;
    }

    function recoveryBootstrapImage(journal) {
      const bootstrap = images.createBootstrapExecutor();
      const journalHash = typeof journal.bootstrapSha256 === 'string' ? journal.bootstrapSha256.toLowerCase() : '';
      // Only the pinned current image or the exact previous prerelease image
      // can be used as an ownership template. Unknown journal versions stay
      // resident until Destiny restarts.
      if (images.metadata.bootstrap_executor.sha256 !== CORRECTED_BOOTSTRAP_SHA256 || bootstrap.length !== 292 ||
          bytesToHex(bootstrap.subarray(0x60, 0x64)) !== '807E0004' ||
          bytesToHex(bootstrap.subarray(0x98, 0x9c)) !== '805E0008') return null;
      if (journalHash === CORRECTED_BOOTSTRAP_SHA256) return bootstrap;
      if (journalHash !== LEGACY_BOOTSTRAP_SHA256) return null;
      bootstrap.set(hexToBytes('806C0004'), 0x60);
      bootstrap.set(hexToBytes('E85E0008'), 0x98);
      return bootstrap;
    }

    async function recover() {
      if (busy) throw new Error('A spawn executor operation is already running');
      busy = true;
      const owner = memory.session();
      try {
        const journal = loadJournal();
        if (!journal) throw new Error('No spawn executor recovery journal exists');
        ensureSession(owner, journal);
        if (journal.pendingCommand) {
          throw new Error('A spawn request is still journaled; check or reconcile its result before recovering the hook');
        }
        if (journal.state === 'recovered-clean') return { state: journal.state, message: 'Spawn executor is already recovered.' };
        const checked = await verifySpawnPath(owner, 'either');
        const reader = checked.reader;

        for (let index = 0; index < 8; index++) {
          const status = await readWord(reader, DATA_BASE + COMMAND_OFFSET + index * COMMAND_STRIDE);
          if (status === 1 || status === 2) {
            throw new Error(`Spawn slot ${index} is active (status ${status}); wait for it to finish before recovery`);
          }
        }

        const hookNow = await reader.readHex(HOOK_ADDRESS, 4);
        const safePreActivationPhases = ['journaled', 'bootstrap', 'spawn-program', 'data-block', 'dispatcher-header', 'task-registration'];
        const preActivation = hookNow === HOOK_ORIGINAL &&
          ['installing', 'recovery-required'].includes(journal.state) &&
          safePreActivationPhases.includes(journal.phase) && journal.spawnCount === 0;
        const bootstrap = preActivation ? recoveryBootstrapImage(journal) : null;
        const programHash = typeof journal.programSha256 === 'string' ? journal.programSha256.toLowerCase() : '';
        const program = preActivation && programHash === SPAWN_PROGRAM_SHA256 &&
          images.metadata.spawn_program.sha256 === SPAWN_PROGRAM_SHA256 ? images.createSpawnProgram() : null;
        const canCleanImages = Boolean(preActivation && bootstrap && program && program.length === 2908);
        if (hookNow === HOOK_INSTALLED) await writeIfEqual(owner, HOOK_ADDRESS, HOOK_INSTALLED, HOOK_ORIGINAL);
        else if (hookNow !== HOOK_ORIGINAL) throw new Error(`Hook contains ${hookNow}; recovery stopped without overwriting unknown code`);

        let imagesCleaned = false;
        // The per-frame callback can run as soon as the hook is activated,
        // even when no spawn request has ever been submitted. Unhooking does
        // not prove that an in-flight callback has returned. The dispatcher
        // reads its callback and context separately, so its task entry and
        // header must remain intact along with code and data in that case.
        if (canCleanImages) {
          const descriptorNow = await reader.readHex(journal.descriptorAddress, 8);
          if (descriptorNow === journal.descriptorHex) {
            await writeIfEqual(owner, journal.descriptorAddress, journal.descriptorHex, repeatByteHex(0, 8));
          } else if (descriptorNow !== repeatByteHex(0, 8)) {
            throw new Error(`Task descriptor contains ${descriptorNow}; recovery retained the journal`);
          }

          const headerNow = await reader.readHex(DISPATCHER_HEADER, 8);
          if (headerNow === journal.dispatcherHeaderHex || headerNow === '4252583100000000') {
            await writeIfEqual(owner, DISPATCHER_HEADER, headerNow, repeatByteHex(0, 8));
          } else if (headerNow !== repeatByteHex(0, 8)) {
            throw new Error(`Dispatcher header contains ${headerNow}; recovery retained the journal`);
          }
          const data = dataBlock.buildDataBlock();
          const cleanBootstrap = await cleanOwnedImage(owner, reader, BOOTSTRAP_BASE, bootstrap);
          const cleanProgram = await cleanOwnedImage(owner, reader, PROGRAM_BASE, program);
          const cleanData = await cleanOwnedImage(owner, reader, DATA_BASE, data);
          imagesCleaned = cleanBootstrap && cleanProgram && cleanData;
        }

        journal.state = imagesCleaned ? 'recovered-clean' : 'recovered-resident';
        journal.pendingCommand = null;
        journal.phase = 'recovered';
        journal.recoveredAt = new Date(now()).toISOString();
        journal.recoveryMessage = imagesCleaned
          ? 'Hook restored and owned executor images returned to their prior zero state.'
          : 'Hook restored. Executor task, code and data may remain resident; restart Destiny before another install.';
        saveJournal(journal);
        activeCommand = null;
        return { state: journal.state, message: journal.recoveryMessage };
      } finally {
        busy = false;
      }
    }

    function state() {
      return { journal: loadJournal(), busy, active: activeCommand ? { ...activeCommand } : null };
    }

    return Object.freeze({ install, spawn, cancelCurrent, reconcilePending, recover, state });
  }

  return Object.freeze({
    PROFILE_NAME,
    JOURNAL_KEY,
    HOOK_ADDRESS,
    HOOK_ORIGINAL,
    HOOK_INSTALLED,
    BOOTSTRAP_BASE,
    PROGRAM_BASE,
    DISPATCHER_HEADER,
    TASK_TABLE,
    DATA_BASE,
    COMMAND_OFFSET,
    COMMAND_STRIDE,
    RESULT_OFFSET,
    RESULT_HEADER_BYTES,
    RESULT_ROW_BYTES,
    bytesToHex,
    hexToBytes,
    parseResults,
    create
  });
});
