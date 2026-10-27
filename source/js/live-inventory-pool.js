// SPDX-License-Identifier: GPL-2.0-only
// BLES 01.24 self-family item-object pool snapshot; read-only and bounded.
(function (root) {
  'use strict';

  const PROFILE_MANAGER_CELL = 0x01AD5FA4;
  // Conservative same-build range caps; descriptor fields are independently checked at runtime.
  const PROFILE_MANAGER_BYTES = 0x50000;
  const ITEM_DESCRIPTOR_OFFSET = 0x49548;
  const ITEM_STORAGE_OFFSET = 0x40A0;
  const ITEM_STORAGE_RELATIVE_LIMIT = 0x45341;
  const ITEM_TYPE_HASH = 0x80801732;
  const ITEM_STRIDE = 0xA8;
  const ITEM_CAPACITY_LIMIT = 646;
  // The inventory resolver's cached item-list pointer (research: 0x18E9514 + 0x64).
  // Runtime reads validate this pointer, then the full 3,181-entry table contents.
  const READ_CHUNK_BYTES = Math.floor(0x600 / ITEM_STRIDE) * ITEM_STRIDE;
  const READ_CHUNK_RECORDS = READ_CHUNK_BYTES / ITEM_STRIDE;
  const READ_GROUP_CHUNKS = 18;
  const READ_HEX = /^(?:[0-9A-F]{2})+$/;
  const MEMORY_SIGNATURES = Object.freeze([
    Object.freeze({
      address: 0x0047C1A0,
      expected: '3C6001AD30635FA0806300044E800020'
    }),
    Object.freeze({
      address: 0x0047E3CC,
      expected: '78842EA478A526E47CE420147C8438143CE000047C83201460E793987C8428143CA00004788A00203C80000460A9939C'
    })
  ]);
  let busy = false;
  let lastSnapshotRows = null;

  function checkedRange(address, size) {
    if (!Number.isSafeInteger(address) || !Number.isSafeInteger(size) || size < 1 ||
        address < 0x10000 || address + size > 0x40000000) {
      throw new Error('An address or size is outside the bounded PS3 user-memory range.');
    }
    return address;
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

  function u32(bytes, offset) {
    return (((bytes[offset] * 0x1000000) + (bytes[offset + 1] << 16) +
      (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0);
  }

  function i16(bytes, offset) {
    const value = (bytes[offset] << 8) | bytes[offset + 1];
    return value & 0x8000 ? value - 0x10000 : value;
  }

  function hexBytes(bytes) {
    let value = '';
    for (const byte of bytes) value += byte.toString(16).padStart(2, '0');
    return value.toUpperCase();
  }

  function addChecked(base, offset, size) {
    if (!Number.isSafeInteger(base) || !Number.isSafeInteger(offset) || offset < 0 ||
        base > Number.MAX_SAFE_INTEGER - offset) {
      throw new Error('The live item-pool address calculation overflowed.');
    }
    return checkedRange(base + offset, size);
  }

  async function verifiedCapability(api, owner) {
    const verification = await api.verify(owner);
    if (verification.profile !== 'BLES01857 01.24 signature profile' || verification.anchors !== 3) {
      throw new Error('The three-anchor BLES01857 01.24 profile did not verify.');
    }
    for (const signature of MEMORY_SIGNATURES) {
      const raw = await api.read(signature.address, signature.expected.length / 2, owner, verification.capability);
      const observed = String(raw || '').replace(/\s/g, '').toUpperCase();
      if (observed !== signature.expected) {
        throw new Error(`BLES inventory code signature mismatch at 0x${signature.address.toString(16).toUpperCase()}.`);
      }
    }
    return verification.capability;
  }

  async function readPoolMetadata(api, owner, capability) {
    const managerBytes = bytesFromHex(await api.read(PROFILE_MANAGER_CELL, 4, owner, capability), 4);
    const manager = u32(managerBytes, 0);
    checkedRange(manager, PROFILE_MANAGER_BYTES);
    if ((manager & 3) !== 0) throw new Error('The profile-manager pointer is not four-byte aligned.');

    const descriptorAddress = addChecked(manager, ITEM_DESCRIPTOR_OFFSET, 0x10);
    const descriptorBytes = bytesFromHex(await api.read(descriptorAddress, 0x10, owner, capability), 0x10);
    const dataOffset = u32(descriptorBytes, 0);
    // Native BLES lookup sign-extends the low halfword with extsh.
    // lwz at descriptor +4 followed by extsh uses that word's low halfword
    // (descriptor bytes +6..+7), not its high halfword at +4..+5.
    const count = i16(descriptorBytes, 6);
    const stride = u32(descriptorBytes, 8);
    const typeHash = u32(descriptorBytes, 0x0C);

    if (typeHash !== ITEM_TYPE_HASH) throw new Error('The self-family kind-3 descriptor has an unexpected item type.');
    if (stride !== ITEM_STRIDE) throw new Error('The BLES item-object stride is not 0xA8.');
    if (count < 1 || count > ITEM_CAPACITY_LIMIT) throw new Error('The item-object count is outside the verified 1–646 slot bound.');

    const byteLength = count * stride;
    if (!Number.isSafeInteger(byteLength) || dataOffset + byteLength >= ITEM_STORAGE_RELATIVE_LIMIT) {
      throw new Error('The item-object pool range is outside its verified manager-storage bound.');
    }
    const poolAddress = addChecked(manager, ITEM_STORAGE_OFFSET + dataOffset, byteLength);
    if ((poolAddress & 7) !== 0) throw new Error('The item-object pool is not eight-byte aligned.');
    if (poolAddress + byteLength > manager + PROFILE_MANAGER_BYTES) {
      throw new Error('The item-object pool extends past the bounded profile-manager region.');
    }

    return {
      manager,
      descriptorAddress,
      descriptorHex: hexBytes(descriptorBytes),
      dataOffset,
      count,
      stride,
      typeHash,
      poolAddress,
      byteLength
    };
  }

  function sameMetadata(a, b) {
    return a.manager === b.manager && a.descriptorAddress === b.descriptorAddress &&
      a.descriptorHex === b.descriptorHex && a.poolAddress === b.poolAddress &&
      a.byteLength === b.byteLength;
  }

  function appendOccupiedRows(rows, bytes, firstSlot, records) {
    for (let record = 0; record < records; record++) {
      const offset = record * ITEM_STRIDE;
      // Item instance SOID is only reduced to a transient zero/nonzero predicate.
      if (u32(bytes, offset) === 0 && u32(bytes, offset + 4) === 0) continue;
      rows.push({ slot: firstSlot + record, itemIndex: i16(bytes, offset + 0x24) });
    }
  }

  function renderRows(output, rows) {
    output.replaceChildren();
    if (!rows.length) {
      const empty = document.createElement('p');
      empty.className = 'live-inventory-empty';
      empty.textContent = 'The validated item pool contains no populated item objects.';
      output.append(empty);
      output.hidden = false;
      return;
    }

    const table = document.createElement('table');
    table.className = 'live-inventory-table';
    const head = document.createElement('thead');
    const heading = document.createElement('tr');
    for (const label of ['Pool slot', 'Item index', 'Catalog name']) {
      const cell = document.createElement('th');
      cell.scope = 'col';
      cell.textContent = label;
      heading.append(cell);
    }
    head.append(heading);
    const body = document.createElement('tbody');
    for (const row of rows) {
      const tr = document.createElement('tr');
      const slot = document.createElement('td');
      const index = document.createElement('td');
      const name = document.createElement('td');
      slot.textContent = String(row.slot);
      index.textContent = String(row.itemIndex);
      name.textContent = window.localInventoryItemName?.(row.itemIndex) || '—';
      tr.append(slot, index, name);
      body.append(tr);
    }
    table.append(head, body);
    output.append(table);
    output.hidden = false;
  }

  async function inspectLiveItemPool() {
    const button = document.getElementById('live-inventory-pool-btn');
    const clearButton = document.getElementById('live-inventory-pool-clear');
    const status = document.getElementById('live-inventory-pool-status');
    const output = document.getElementById('live-inventory-pool-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    if (clearButton) clearButton.disabled = true;
    lastSnapshotRows = null;
    output.replaceChildren();
    output.hidden = true;
    status.dataset.state = 'pending';
    status.textContent = 'Checking the BLES profile, inventory code, and self-family item descriptor…';

    try {
      if (!api) throw new Error('The session-bound read-only memory API is unavailable.');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before reading the item pool.');

      const firstCapability = await verifiedCapability(api, owner);
      const initial = await readPoolMetadata(api, owner, firstCapability);
      const rows = [];
      let firstIdentity = '';
      let lastIdentity = '';
      let nextSlot = 0;

      while (nextSlot < initial.count) {
        const groupCapability = nextSlot === 0 ? firstCapability : await verifiedCapability(api, owner);
        const groupMetadata = nextSlot === 0 ? initial : await readPoolMetadata(api, owner, groupCapability);
        if (!sameMetadata(initial, groupMetadata)) throw new Error('The profile manager or item descriptor changed during the snapshot.');

        const groupEnd = Math.min(initial.count, nextSlot + READ_CHUNK_RECORDS * READ_GROUP_CHUNKS);
        while (nextSlot < groupEnd) {
          const records = Math.min(READ_CHUNK_RECORDS, groupEnd - nextSlot);
          const byteLength = records * ITEM_STRIDE;
          const address = addChecked(initial.poolAddress, nextSlot * ITEM_STRIDE, byteLength);
          const bytes = bytesFromHex(await api.read(address, byteLength, owner, groupCapability), byteLength);
          if (nextSlot === 0) firstIdentity = hexBytes(bytes.slice(0, 8));
          if (nextSlot + records === initial.count) {
            const lastOffset = (records - 1) * ITEM_STRIDE;
            lastIdentity = hexBytes(bytes.slice(lastOffset, lastOffset + 8));
          }
          appendOccupiedRows(rows, bytes, nextSlot, records);
          nextSlot += records;
        }
      }

      const finalCapability = await verifiedCapability(api, owner);
      const finalMetadata = await readPoolMetadata(api, owner, finalCapability);
      if (!sameMetadata(initial, finalMetadata) || api.session() !== owner) {
        throw new Error('The final profile-manager, descriptor, or console-session recheck failed.');
      }
      const firstAgain = bytesFromHex(await api.read(initial.poolAddress, 8, owner, finalCapability), 8);
      const lastSlotAddress = addChecked(initial.poolAddress, (initial.count - 1) * ITEM_STRIDE, 8);
      const lastAgain = bytesFromHex(await api.read(lastSlotAddress, 8, owner, finalCapability), 8);
      if (hexBytes(firstAgain) !== firstIdentity || hexBytes(lastAgain) !== lastIdentity) {
        throw new Error('The item pool changed at a boundary sample; the partial snapshot was discarded.');
      }

      renderRows(output, rows);
      lastSnapshotRows = rows;
      status.dataset.state = 'matched';
      const labeledNames = rows.filter(row => window.localInventoryItemName?.(row.itemIndex)).length;
      status.textContent = `Read ${initial.count} validated pool slots; ${rows.length} contain an item object. ${labeledNames} names came from explicitly BLES-indexed catalogs. Sequential, non-atomic sample; item-pool pointers rechecked.`;
    } catch (error) {
      output.replaceChildren();
      output.hidden = true;
      status.dataset.state = 'error';
      status.textContent = `Live item-pool snapshot refused: ${String(error?.message || error).slice(0, 220)}`;
    } finally {
      busy = false;
      button.disabled = false;
      if (clearButton) clearButton.disabled = false;
    }
  }

  function clearLiveItemPool() {
    lastSnapshotRows = null;
    const output = document.getElementById('live-inventory-pool-output');
    const status = document.getElementById('live-inventory-pool-status');
    if (output) {
      output.replaceChildren();
      output.hidden = true;
    }
    if (status) {
      status.textContent = 'No live item-pool snapshot taken.';
      delete status.dataset.state;
    }
  }

  root.inspectLiveItemPool = inspectLiveItemPool;
  function initialize() {
    document.getElementById('live-inventory-pool-clear')?.addEventListener('click', clearLiveItemPool);
    window.addEventListener('rtm-local-item-catalogs-changed', () => {
      if (lastSnapshotRows) renderRows(document.getElementById('live-inventory-pool-output'), lastSnapshotRows);
    });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
})(window);
