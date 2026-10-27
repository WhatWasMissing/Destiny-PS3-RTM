// SPDX-License-Identifier: GPL-2.0-only
// BLES01857 01.24 resource/bootflow status snapshot. Read-only, bounded, non-atomic.
(function (root) {
  'use strict';

  const BOOTFLOW_POINTER_CELL = 0x01ABC89C;
  const BOOTFLOW_NAMES = 0x012F7F00;
  const RESOURCE_NAMES = 0x01315588;
  const RESOURCE_STATES = 0x01ADA1B0;
  const RESOURCE_MANAGER_POINTER_CELL = 0x01A1DCE0;
  const RESOURCE_SCHEDULER = 0x01A1DCE8;
  const AUXILIARY_POINTER_CELL = 0x01ABC8B8;
  const READINESS_TABLE = 0x01B558F0;
  const BOOTFLOW_STATE_COUNT = 38;
  const RESOURCE_SLOT_COUNT = 8;
  const MAX_STRING_BYTES = 96;
  const READINESS_BYTES = 0xEC;
  const READ_HEX = /^(?:[0-9A-F]{2})+$/;

  let busy = false;

  function checkedRange(address, size) {
    if (!Number.isSafeInteger(address) || !Number.isSafeInteger(size) || size < 1 ||
        address < 0x10000 || address + size > 0x40000000) {
      throw new Error('An address or size is outside the bounded PS3 user-memory range.');
    }
    return address;
  }

  function checkedAdd(base, offset, size) {
    if (!Number.isSafeInteger(base) || !Number.isSafeInteger(offset) || offset < 0 ||
        base > Number.MAX_SAFE_INTEGER - offset) {
      throw new Error('The resource snapshot address calculation overflowed.');
    }
    return checkedRange(base + offset, size);
  }

  function bytesFromHex(value, expectedBytes) {
    const hex = String(value || '').replace(/\s/g, '').toUpperCase();
    if (!READ_HEX.test(hex) || hex.length !== expectedBytes * 2) {
      throw new Error(`Expected exactly ${expectedBytes} bytes from the console.`);
    }
    const bytes = new Uint8Array(expectedBytes);
    for (let i = 0; i < expectedBytes; i++) bytes[i] = parseInt(hex.slice(i * 2, i * 2 + 2), 16);
    return bytes;
  }

  function hexFromBytes(bytes) {
    let hex = '';
    for (const byte of bytes) hex += byte.toString(16).padStart(2, '0');
    return hex.toUpperCase();
  }

  function u16(bytes, offset) {
    return (bytes[offset] << 8) | bytes[offset + 1];
  }

  function i16(bytes, offset) {
    const value = u16(bytes, offset);
    return value & 0x8000 ? value - 0x10000 : value;
  }

  function u32(bytes, offset) {
    return (((bytes[offset] * 0x1000000) + (bytes[offset + 1] << 16) +
      (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0);
  }

  function i32(bytes, offset) {
    return u32(bytes, offset) | 0;
  }

  function hex32(value) {
    return `0x${(value >>> 0).toString(16).padStart(8, '0').toUpperCase()}`;
  }

  async function readBytes(api, owner, capability, address, size) {
    checkedRange(address, size);
    if (size > 0x600) throw new Error('A snapshot read exceeded the 0x600-byte request limit.');
    return bytesFromHex(await api.read(address, size, owner, capability), size);
  }

  async function readU32(api, owner, capability, address) {
    return u32(await readBytes(api, owner, capability, address, 4), 0);
  }

  async function readCStringCapture(api, owner, capability, address) {
    if (!address || address === 0xFFFFFFFF) return { value: '', rawHex: null };
    checkedRange(address, MAX_STRING_BYTES);
    const bytes = await readBytes(api, owner, capability, address, MAX_STRING_BYTES);
    const end = bytes.indexOf(0);
    const length = end < 0 ? bytes.length : end;
    let value = '';
    for (let i = 0; i < length; i++) {
      const byte = bytes[i];
      value += byte >= 0x20 && byte <= 0x7E ? String.fromCharCode(byte) : '�';
    }
    return { value, rawHex: hexFromBytes(bytes) };
  }

  async function verifiedCapability(api, owner) {
    const verification = await api.verify(owner);
    if (verification.profile !== 'BLES01857 01.24 signature profile' || verification.anchors !== 3) {
      throw new Error('The three-anchor BLES01857 01.24 profile did not verify.');
    }
    return verification.capability;
  }

  async function captureDynamic(api, owner, capability) {
    const bootflow = await readU32(api, owner, capability, BOOTFLOW_POINTER_CELL);
    checkedRange(bootflow, 0x140);
    const bootflowFieldsAddress = checkedAdd(bootflow, 0x118, 0x1C);
    const bootflowFields = await readBytes(api, owner, capability, bootflowFieldsAddress, 0x1C);
    const current = u32(bootflowFields, 0);
    if (current >= BOOTFLOW_STATE_COUNT) throw new Error(`Unexpected BLES 01.24 bootflow state ${current}.`);
    const subcurrent = u32(bootflowFields, 4);
    const target = u32(bootflowFields, 0x10);
    const subtarget = u32(bootflowFields, 0x14);
    const stagePointerAddress = checkedAdd(bootflow, 0x80 + current * 4, 4);
    const stageObject = await readU32(api, owner, capability, stagePointerAddress);
    let stageFields = null;
    if (stageObject) {
      checkedRange(stageObject, 0x40);
      stageFields = await readBytes(api, owner, capability, checkedAdd(stageObject, 0x30, 0x10), 0x10);
    }

    const stateNamePointer = await readU32(
      api, owner, capability, checkedAdd(BOOTFLOW_NAMES, current * 4, 4)
    );
    const stateNameCapture = await readCStringCapture(api, owner, capability, stateNamePointer);

    const resourceBlock = await readBytes(api, owner, capability, RESOURCE_STATES, 0x64);
    const resourceNamesRaw = await readBytes(api, owner, capability, RESOURCE_NAMES, RESOURCE_SLOT_COUNT * 4);
    const resources = [];
    for (let index = 0; index < RESOURCE_SLOT_COUNT; index++) {
      const offset = index * 12;
      const namePointer = u32(resourceNamesRaw, index * 4);
      const nameCapture = await readCStringCapture(api, owner, capability, namePointer);
      resources.push({
        slot: index,
        name: nameCapture.value,
        namePointer,
        nameRawHex: nameCapture.rawHex,
        status: u32(resourceBlock, offset),
        handle: u32(resourceBlock, offset + 4),
        extra: u32(resourceBlock, offset + 8)
      });
    }
    const pendingCount = u32(resourceBlock, 0x60);

    const manager = await readU32(api, owner, capability, RESOURCE_MANAGER_POINTER_CELL);
    let managerFields = null;
    if (manager) {
      const managerFieldsAddress = checkedAdd(manager, 0x1405C, 0x1C);
      const raw = await readBytes(api, owner, capability, managerFieldsAddress, 0x1C);
      managerFields = {
        rawHex: hexFromBytes(raw),
        queuePresent: u32(raw, 0) !== 0,
        retryCount: u32(raw, 8),
        retryHandles: [u32(raw, 0x0C), u32(raw, 0x10)],
        activeHandle: u32(raw, 0x14),
        flaggedCount: u32(raw, 0x18)
      };
    }

    const scheduler = await readBytes(api, owner, capability, RESOURCE_SCHEDULER, 12);
    const token = u16(scheduler, 0);
    const schedulerSnapshot = {
      rawHex: hexFromBytes(scheduler),
      token,
      jobState: u32(scheduler, 4),
      deferredEvent: u32(scheduler, 8),
      batchStatus: null,
      batchHeaderHex: null,
      batchIdentity: null
    };
    if (token !== 0xFFFF) {
      const batchHeader = await readBytes(api, owner, capability, 0x019A3F80, 0x20);
      const relativeOffset = u32(batchHeader, 0);
      const stride = u32(batchHeader, 8);
      const capacity = u16(batchHeader, 0x0C);
      if (!relativeOffset || stride !== 0x40 || capacity !== 2 || token >= capacity) {
        throw new Error('The BLES 01.24 active-batch descriptor failed its fixed stride/capacity checks.');
      }
      schedulerSnapshot.batchHeaderHex = hexFromBytes(batchHeader);
      const recordAddress = checkedAdd(0x019A3F80, relativeOffset + token * stride, 0x40);
      const record = await readBytes(api, owner, capability, recordAddress, 0x40);
      schedulerSnapshot.batchStatus = i32(record, 0);
      // Retain only the record address for equality checks; it is never shown.
      schedulerSnapshot.batchIdentity = { address: recordAddress, rawHex: hexFromBytes(record) };
    }

    const auxiliary = await readU32(api, owner, capability, AUXILIARY_POINTER_CELL);
    let auxiliaryFields = null;
    if (auxiliary) {
      auxiliaryFields = await readBytes(api, owner, capability, checkedAdd(auxiliary, 0x80, 8), 8);
    }
    const readiness = await readBytes(api, owner, capability, READINESS_TABLE, READINESS_BYTES);
    const mode = auxiliaryFields ? i32(auxiliaryFields, 0) : null;
    const modeMasks = [];
    for (let i = 0; i < 10; i++) modeMasks.push(u16(readiness, i * 2));
    const initialized = readiness[0xE8] !== 0;
    const readinessMask = u16(readiness, 0xBA);
    const requiredMask = initialized && mode !== null && mode >= 0 && mode < modeMasks.length
      ? modeMasks[mode] : null;
    const missingMask = requiredMask === null ? null : requiredMask & (0x7FF & ~readinessMask);
    const descriptorCount = u32(readiness, 0x8C);
    const descriptorLimit = Math.floor((0x8C - 0x14) / 0x18);
    const descriptors = [];
    for (let index = 0; index < Math.min(descriptorCount, descriptorLimit); index++) {
      const offset = 0x14 + index * 0x18;
      descriptors.push({
        index,
        bitIndex: u32(readiness, offset),
        contentHandle: u32(readiness, offset + 4),
        byte14: readiness[offset + 0x14]
      });
    }

    return {
      bootflow,
      bootflowFieldsHex: hexFromBytes(bootflowFields),
      bootflowFields: { current, subcurrent, target, subtarget, stateName: stateNameCapture.value },
      stageObject,
      stageFieldsHex: stageFields ? hexFromBytes(stageFields) : null,
      stageFields: stageFields && current === 19
        ? { state30: u32(stageFields, 0), state34: u32(stageFields, 4) }
        : null,
      resources,
      resourceBlockHex: hexFromBytes(resourceBlock),
      resourceNamesRawHex: hexFromBytes(resourceNamesRaw),
      stateNamePointer,
      stateNameRawHex: stateNameCapture.rawHex,
      pendingCount,
      manager,
      managerFields,
      scheduler: schedulerSnapshot,
      auxiliary,
      auxiliaryHex: auxiliaryFields ? hexFromBytes(auxiliaryFields) : null,
      readinessHex: hexFromBytes(readiness),
      readinessState: {
        mode,
        auxiliaryWord84: auxiliaryFields ? u32(auxiliaryFields, 4) : null,
        initialized,
        modeMasks,
        readinessMask,
        requiredMask,
        missingMask,
        pollReady: mode === -1 ? true : (missingMask === null ? null : missingMask === 0),
        descriptorCount,
        requestedCount: u32(readiness, 0xA4),
        controlWord: u32(readiness, 0xA8),
        loadingWord: i32(readiness, 0xB0),
        descriptors,
        descriptorsTruncated: descriptorCount > descriptorLimit
      }
    };
  }

  async function captureRecheck(api, owner, capability, before) {
    const bootflow = await readU32(api, owner, capability, BOOTFLOW_POINTER_CELL);
    if (bootflow !== before.bootflow) return false;
    const bootflowFields = await readBytes(api, owner, capability, checkedAdd(bootflow, 0x118, 0x1C), 0x1C);
    if (hexFromBytes(bootflowFields) !== before.bootflowFieldsHex) return false;
    const current = u32(bootflowFields, 0);
    if (current >= BOOTFLOW_STATE_COUNT) return false;
    const stateNamePointer = await readU32(
      api, owner, capability, checkedAdd(BOOTFLOW_NAMES, current * 4, 4)
    );
    if (stateNamePointer !== before.stateNamePointer) return false;
    if (before.stateNamePointer && before.stateNamePointer !== 0xFFFFFFFF) {
      const stateName = await readBytes(api, owner, capability, before.stateNamePointer, MAX_STRING_BYTES);
      if (hexFromBytes(stateName) !== before.stateNameRawHex) return false;
    }
    const stageObject = await readU32(api, owner, capability, checkedAdd(bootflow, 0x80 + current * 4, 4));
    if (stageObject !== before.stageObject) return false;
    if (stageObject) {
      const stageFields = await readBytes(api, owner, capability, checkedAdd(stageObject, 0x30, 0x10), 0x10);
      if (hexFromBytes(stageFields) !== before.stageFieldsHex) return false;
    }

    const resourceBlock = await readBytes(api, owner, capability, RESOURCE_STATES, 0x64);
    if (hexFromBytes(resourceBlock) !== before.resourceBlockHex) return false;
    const resourceNamesRaw = await readBytes(api, owner, capability, RESOURCE_NAMES, RESOURCE_SLOT_COUNT * 4);
    if (hexFromBytes(resourceNamesRaw) !== before.resourceNamesRawHex) return false;
    for (const resource of before.resources) {
      if (!resource.namePointer || resource.namePointer === 0xFFFFFFFF) continue;
      const nameBytes = await readBytes(api, owner, capability, resource.namePointer, MAX_STRING_BYTES);
      if (hexFromBytes(nameBytes) !== resource.nameRawHex) return false;
    }

    const manager = await readU32(api, owner, capability, RESOURCE_MANAGER_POINTER_CELL);
    if (manager !== before.manager) return false;
    if (manager) {
      const managerFields = await readBytes(api, owner, capability, checkedAdd(manager, 0x1405C, 0x1C), 0x1C);
      if (hexFromBytes(managerFields) !== before.managerFields.rawHex) return false;
    }

    const scheduler = await readBytes(api, owner, capability, RESOURCE_SCHEDULER, 12);
    if (hexFromBytes(scheduler) !== before.scheduler.rawHex) return false;
    if (before.scheduler.batchIdentity) {
      const batchHeader = await readBytes(api, owner, capability, 0x019A3F80, 0x20);
      if (hexFromBytes(batchHeader) !== before.scheduler.batchHeaderHex) return false;
      const batchRecord = await readBytes(api, owner, capability,
        before.scheduler.batchIdentity.address, 0x40);
      if (hexFromBytes(batchRecord) !== before.scheduler.batchIdentity.rawHex) return false;
    }

    const auxiliary = await readU32(api, owner, capability, AUXILIARY_POINTER_CELL);
    if (auxiliary !== before.auxiliary) return false;
    if (auxiliary) {
      const auxiliaryFields = await readBytes(api, owner, capability, checkedAdd(auxiliary, 0x80, 8), 8);
      if (hexFromBytes(auxiliaryFields) !== before.auxiliaryHex) return false;
    }
    const readiness = await readBytes(api, owner, capability, READINESS_TABLE, READINESS_BYTES);
    return hexFromBytes(readiness) === before.readinessHex;
  }

  function makeElement(tag, className, value) {
    const element = document.createElement(tag);
    if (className) element.className = className;
    element.textContent = String(value);
    return element;
  }

  function addTable(container, columns, rows) {
    const table = document.createElement('table');
    table.className = 'resource-snapshot-table';
    const thead = document.createElement('thead');
    const heading = document.createElement('tr');
    for (const column of columns) {
      const cell = document.createElement('th');
      cell.scope = 'col';
      cell.textContent = column;
      heading.append(cell);
    }
    thead.append(heading);
    const tbody = document.createElement('tbody');
    for (const row of rows) {
      const tr = document.createElement('tr');
      for (const value of row) tr.append(makeElement('td', '', value));
      tbody.append(tr);
    }
    table.append(thead, tbody);
    container.append(table);
  }

  function renderSnapshot(output, snapshot) {
    output.replaceChildren();

    const bootflowSection = document.createElement('section');
    bootflowSection.className = 'resource-snapshot-block';
    bootflowSection.append(makeElement('h4', '', 'Bootflow'));
    addTable(bootflowSection, ['State', 'Substate', 'Target', 'Target substate'], [[
      `${snapshot.bootflowFields.current} · ${snapshot.bootflowFields.stateName || 'unnamed'}`,
      snapshot.bootflowFields.subcurrent,
      snapshot.bootflowFields.target,
      snapshot.bootflowFields.subtarget
    ]]);
    if (snapshot.stageFields) {
      addTable(bootflowSection, ['BAP sign-in state +0x30', 'BAP sign-in state +0x34'], [[
        snapshot.stageFields.state30, snapshot.stageFields.state34
      ]]);
    }
    output.append(bootflowSection);

    const resourceSection = document.createElement('section');
    resourceSection.className = 'resource-snapshot-block';
    resourceSection.append(makeElement('h4', '', `Resource slots · ${snapshot.pendingCount} pending`));
    addTable(resourceSection, ['Slot', 'Name', 'Status', 'Handle', 'Extra'], snapshot.resources.map(row => [
      row.slot, row.name || '—', row.status, hex32(row.handle), hex32(row.extra)
    ]));
    output.append(resourceSection);

    const manager = snapshot.managerFields;
    const schedulerSection = document.createElement('section');
    schedulerSection.className = 'resource-snapshot-block';
    schedulerSection.append(makeElement('h4', '', 'Resource scheduler'));
    addTable(schedulerSection, ['Queue', 'Retries', 'Retry handles', 'Active handle', 'Flagged'], [[
      manager ? (manager.queuePresent ? 'non-empty' : 'empty') : 'manager unavailable',
      manager ? manager.retryCount : '—',
      manager ? manager.retryHandles.map(hex32).join(' · ') : '—',
      manager ? hex32(manager.activeHandle) : '—',
      manager ? manager.flaggedCount : '—'
    ]]);
    addTable(schedulerSection, ['Active token', 'Job state', 'Deferred event', 'Batch status'], [[
      snapshot.scheduler.token === 0xFFFF ? 'none' : `0x${snapshot.scheduler.token.toString(16).padStart(4, '0').toUpperCase()}`,
      hex32(snapshot.scheduler.jobState),
      hex32(snapshot.scheduler.deferredEvent),
      snapshot.scheduler.batchStatus === null ? '—' : snapshot.scheduler.batchStatus
    ]]);
    output.append(schedulerSection);

    const readiness = snapshot.readinessState;
    const readinessSection = document.createElement('section');
    readinessSection.className = 'resource-snapshot-block';
    readinessSection.append(makeElement('h4', '', 'Asset readiness · not a tag-loader queue'));
    addTable(readinessSection, ['Mode', 'Initialized', 'Ready mask', 'Required mask', 'Missing mask', 'Poll ready'], [[
      readiness.mode === null ? 'unknown' : readiness.mode,
      readiness.initialized ? 'yes' : 'no',
      `0x${readiness.readinessMask.toString(16).padStart(4, '0').toUpperCase()}`,
      readiness.requiredMask === null ? 'unknown' : `0x${readiness.requiredMask.toString(16).padStart(4, '0').toUpperCase()}`,
      readiness.missingMask === null ? 'unknown' : `0x${readiness.missingMask.toString(16).padStart(4, '0').toUpperCase()}`,
      readiness.pollReady === null ? 'unknown' : (readiness.pollReady ? 'yes' : 'no')
    ]]);
    addTable(readinessSection, ['Descriptor', 'Bit index', 'Content handle', 'Byte +0x14'],
      readiness.descriptors.map(row => [row.index, row.bitIndex, hex32(row.contentHandle), row.byte14]));
    const note = makeElement('p', 'resource-snapshot-note',
      `Captured ${readiness.descriptors.length} of ${readiness.descriptorCount} readiness descriptors. Pointer fields are not followed.`);
    readinessSection.append(note);
    output.append(readinessSection);

    output.hidden = false;
  }

  async function inspectResourceBootflow() {
    const button = document.getElementById('resource-bootflow-read-btn');
    const clearButton = document.getElementById('resource-bootflow-clear-btn');
    const status = document.getElementById('resource-bootflow-status');
    const output = document.getElementById('resource-bootflow-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    if (clearButton) clearButton.disabled = true;
    output.replaceChildren();
    output.hidden = true;
    status.dataset.state = 'pending';
    status.textContent = 'Checking BLES 01.24 and reading bootflow, resource, scheduler, and readiness state…';

    try {
      if (!api) throw new Error('The session-bound read-only memory API is unavailable.');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before reading resource state.');
      const firstCapability = await verifiedCapability(api, owner);
      const snapshot = await captureDynamic(api, owner, firstCapability);
      const finalCapability = await verifiedCapability(api, owner);
      const stable = await captureRecheck(api, owner, finalCapability, snapshot);
      if (!stable || api.session() !== owner) {
        throw new Error('The root pointers or sampled state changed during capture; the snapshot was discarded.');
      }

      renderSnapshot(output, snapshot);
      status.dataset.state = 'matched';
      status.textContent = 'BLES 01.24 resource/bootflow snapshot captured. The sample was sequential and non-atomic; root pointers and sampled state were unchanged on recheck. This is not a tag-loader queue view.';
    } catch (error) {
      output.replaceChildren();
      output.hidden = true;
      status.dataset.state = 'error';
      status.textContent = `Resource/bootflow snapshot refused: ${String(error?.message || error).slice(0, 220)}`;
    } finally {
      busy = false;
      button.disabled = false;
      if (clearButton) clearButton.disabled = false;
    }
  }

  function clearSnapshot() {
    const output = document.getElementById('resource-bootflow-output');
    const status = document.getElementById('resource-bootflow-status');
    if (output) {
      output.replaceChildren();
      output.hidden = true;
    }
    if (status) {
      status.textContent = 'No resource/bootflow snapshot taken.';
      delete status.dataset.state;
    }
  }

  root.inspectResourceBootflow = inspectResourceBootflow;
  function initialize() {
    document.getElementById('resource-bootflow-clear-btn')?.addEventListener('click', clearSnapshot);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
})(window);
