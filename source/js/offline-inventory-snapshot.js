// SPDX-License-Identifier: GPL-2.0-only
// BLES 01.24 account and character wire-layout decoder, isolated under GPL-2.0-only.
// Layout facts are based on the 01.24 type schema in p3n1cillin/d1reimagined-source.
// This module handles explicitly selected offline payload files only.
(function () {
  'use strict';

  const QZ_HEADER_BYTES = 0x10;
  const ACCOUNT_TYPE_HASH = 0xB5DF2907;
  const ACCOUNT_BODY_BYTES = 0x7050;
  const ACCOUNT_FILE_BYTES = QZ_HEADER_BYTES + ACCOUNT_BODY_BYTES;
  const CHARACTER_TYPE_HASH = 0xEB4B8439;
  const CHARACTER_BODY_BYTES = 0x6DB8;
  const CHARACTER_FILE_BYTES = QZ_HEADER_BYTES + CHARACTER_BODY_BYTES;
  const CHARACTER_INVENTORY_OFFSET = 0x30;
  const INVENTORY_ROWS_OFFSET = 0x08;
  const CHARACTER_SLOT_COUNT = 320;
  const VAULT_CANDIDATE_OFFSET = 0x4C0;
  const VAULT_ROWS_OFFSET = 0x08;
  const VAULT_CANDIDATE_SLOT_COUNT = 326;
  const ITEM_ROW_BYTES = 0x20;
  const ITEM_INDEX_OFFSET = 0;
  const INSTANCE_ID_OFFSET = 0x08;
  const EQUIPPED_SLOT_LABELS = new Map([
    [40, 'Subclass'],
    [43, 'Helmet'],
    [53, 'Gauntlets'],
    [63, 'Chest'],
    [73, 'Legs'],
    [83, 'Class item'],
    [93, 'Weapon slot 6'],
    [103, 'Weapon slot 7'],
    [113, 'Weapon slot 8'],
    [123, 'Equipped slot 9'],
    [143, 'Equipped slot 10'],
    [153, 'Equipped slot 11'],
    [210, 'Equipped slot 12'],
    [220, 'Equipped slot 13'],
    [230, 'Equipped slot 14'],
    [240, 'Equipped slot 15'],
    [268, 'Equipped slot 16']
  ]);
  let activeLoads = 0;
  let lastCharacterRows = null;
  let lastAccountRows = null;

  const byId = id => document.getElementById(id);

  function isOccupied(view, rowOffset) {
    // Read only the transient zero/nonzero predicate; never retain or render the ID.
    return view.getUint32(rowOffset + INSTANCE_ID_OFFSET, false) !== 0 ||
      view.getUint32(rowOffset + INSTANCE_ID_OFFSET + 4, false) !== 0;
  }

  function validateWirePayload(buffer, expectedTypeHash, expectedBodyBytes) {
    const view = new DataView(buffer);
    if (view.getUint32(0, false) !== expectedTypeHash) throw new Error('The QZ header type hash does not match this profile payload.');
    if (view.getUint32(12, false) !== expectedBodyBytes) throw new Error('The QZ header body length does not match BLES 01.24.');
    // The QZ SOID at +4..+0xB is intentionally never read or retained.
  }

  function decodeRows(buffer, baseOffset, rowsOffset, slotCount) {
    const view = new DataView(buffer);
    const rows = [];
    for (let slot = 0; slot < slotCount; slot++) {
      const rowOffset = baseOffset + rowsOffset + slot * ITEM_ROW_BYTES;
      if (!isOccupied(view, rowOffset)) continue;
      rows.push({ slot, equippedLabel: EQUIPPED_SLOT_LABELS.get(slot) || '', itemIndex: view.getInt16(rowOffset + ITEM_INDEX_OFFSET, false) });
    }
    return rows;
  }

  function decodeCharacterBuffer(buffer) {
    if (!buffer || typeof buffer.byteLength !== 'number' || buffer.byteLength !== CHARACTER_FILE_BYTES) {
      throw new Error(`Expected exactly ${CHARACTER_FILE_BYTES.toLocaleString()} bytes for a BLES 01.24 character payload.`);
    }
    validateWirePayload(buffer, CHARACTER_TYPE_HASH, CHARACTER_BODY_BYTES);
    return decodeRows(buffer, QZ_HEADER_BYTES + CHARACTER_INVENTORY_OFFSET, INVENTORY_ROWS_OFFSET, CHARACTER_SLOT_COUNT);
  }

  function compareCharacterRows(beforeRows, afterRows) {
    const beforeBySlot = new Map(beforeRows.map(row => [row.slot, row]));
    const afterBySlot = new Map(afterRows.map(row => [row.slot, row]));
    const slots = [...new Set([...beforeBySlot.keys(), ...afterBySlot.keys()])].sort((a, b) => a - b);
    const changes = [];
    let unchangedCount = 0;

    for (const slot of slots) {
      const before = beforeBySlot.get(slot) || null;
      const after = afterBySlot.get(slot) || null;
      if (!before) changes.push({ slot, change: 'Added', before: null, after });
      else if (!after) changes.push({ slot, change: 'Removed', before, after: null });
      else if (before.itemIndex !== after.itemIndex) changes.push({ slot, change: 'Changed', before, after });
      else unchangedCount++;
    }

    return { changes, unchangedCount };
  }

  function clearRows(target) {
    target.replaceChildren();
    target.hidden = true;
  }

  function renderRows(target, rows, emptyMessage, showEquippedPositions = false) {
    clearRows(target);
    target.hidden = false;
    if (!rows.length) {
      const empty = document.createElement('p');
      empty.className = 'offline-inventory-empty';
      empty.textContent = emptyMessage;
      target.append(empty);
      return;
    }

    const table = document.createElement('table');
    table.className = 'offline-inventory-table';
    const head = document.createElement('thead');
    const headRow = document.createElement('tr');
    const columnLabels = ['Slot'];
    if (showEquippedPositions) columnLabels.push('Equipped position');
    columnLabels.push('Item index', 'Catalog name', 'Catalog bucket', 'Catalog type');
    for (const label of columnLabels) {
      const cell = document.createElement('th');
      cell.scope = 'col';
      cell.textContent = label;
      headRow.append(cell);
    }
    head.append(headRow);
    const body = document.createElement('tbody');
    for (const row of rows) {
      const tr = document.createElement('tr');
      const slotCell = document.createElement('td');
      const indexCell = document.createElement('td');
      const nameCell = document.createElement('td');
      const bucketCell = document.createElement('td');
      const typeCell = document.createElement('td');
      const metadata = window.localInventoryItemMetadata?.(row.itemIndex);
      slotCell.className = 'inventory-slot-cell';
      indexCell.className = 'inventory-item-index-cell';
      nameCell.className = 'inventory-item-name-cell';
      slotCell.textContent = String(row.slot);
      indexCell.textContent = String(row.itemIndex);
      nameCell.textContent = metadata?.name || window.localInventoryItemName?.(row.itemIndex) || '—';
      bucketCell.className = 'inventory-item-bucket-cell';
      typeCell.className = 'inventory-item-type-cell';
      bucketCell.textContent = metadata?.bucket || '—';
      typeCell.textContent = metadata?.type || '—';
      tr.append(slotCell);
      if (showEquippedPositions) {
        const equippedCell = document.createElement('td');
        equippedCell.className = 'inventory-equipped-cell';
        equippedCell.textContent = row.equippedLabel || '—';
        tr.append(equippedCell);
      }
      tr.append(indexCell, nameCell, bucketCell, typeCell);
      body.append(tr);
    }
    table.append(head, body);
    target.append(table);
  }

  function setStatus(id, message, state) {
    const status = byId(id);
    status.textContent = message;
    if (state) status.dataset.state = state;
    else delete status.dataset.state;
  }

  async function loadPayload(kind) {
    const isCharacter = kind === 'character';
    const input = byId(isCharacter ? 'offline-character-payload' : 'offline-account-payload');
    const button = byId(isCharacter ? 'offline-character-load' : 'offline-account-load');
    const statusId = isCharacter ? 'offline-character-status' : 'offline-account-status';
    const output = byId(isCharacter ? 'offline-character-rows' : 'offline-vault-candidate-rows');
    const expectedBytes = isCharacter ? CHARACTER_FILE_BYTES : ACCOUNT_FILE_BYTES;
    const expectedBodyBytes = isCharacter ? CHARACTER_BODY_BYTES : ACCOUNT_BODY_BYTES;
    const expectedTypeHash = isCharacter ? CHARACTER_TYPE_HASH : ACCOUNT_TYPE_HASH;
    const file = input.files && input.files[0];

    if (isCharacter) lastCharacterRows = null;
    else lastAccountRows = null;
    clearRows(output);
    if (!file) {
      setStatus(statusId, `Choose a BLES 01.24 ${isCharacter ? 'character' : 'account'} wire payload file.`, 'error');
      return;
    }
    if (file.size !== expectedBytes) {
      input.value = '';
      setStatus(statusId, `Rejected: expected exactly ${expectedBytes.toLocaleString()} bytes; received ${file.size.toLocaleString()}.`, 'error');
      return;
    }

    button.disabled = true;
    input.disabled = true;
    activeLoads++;
    byId('offline-inventory-clear').disabled = true;
    setStatus(statusId, 'Reading the selected file in this page…', 'pending');
    try {
      const buffer = await file.arrayBuffer();
      if (buffer.byteLength !== expectedBytes) throw new Error('The selected file changed size while it was being read.');
      validateWirePayload(buffer, expectedTypeHash, expectedBodyBytes);
      const rows = isCharacter
        ? decodeRows(buffer, QZ_HEADER_BYTES + CHARACTER_INVENTORY_OFFSET, INVENTORY_ROWS_OFFSET, CHARACTER_SLOT_COUNT)
        : decodeRows(buffer, QZ_HEADER_BYTES + VAULT_CANDIDATE_OFFSET, VAULT_ROWS_OFFSET, VAULT_CANDIDATE_SLOT_COUNT);
      if (isCharacter) lastCharacterRows = rows;
      else lastAccountRows = rows;

      renderRows(output, rows, isCharacter ? 'No populated character inventory slots in this payload.' : 'No nonzero item-instance fields were found in this candidate block.', isCharacter);
      if (isCharacter) {
        setStatus(statusId, `Loaded a ${expectedBytes.toLocaleString()}-byte BLES 01.24 character QZ payload · ${rows.length} populated slots.`, 'ok');
      } else {
        setStatus(statusId, `Loaded a ${expectedBytes.toLocaleString()}-byte BLES 01.24 account QZ payload · ${rows.length} candidate rows. Vault layout and row semantics are unverified.`, 'warning');
      }
    } catch (error) {
      setStatus(statusId, `Could not decode this payload: ${String(error?.message || error).slice(0, 180)}`, 'error');
    } finally {
      button.disabled = false;
      input.disabled = false;
      input.value = '';
      activeLoads--;
      byId('offline-inventory-clear').disabled = activeLoads > 0;
    }
  }

  function clearViewer() {
    lastCharacterRows = null;
    lastAccountRows = null;
    byId('offline-character-payload').value = '';
    byId('offline-account-payload').value = '';
    clearRows(byId('offline-character-rows'));
    clearRows(byId('offline-vault-candidate-rows'));
    setStatus('offline-character-status', 'No character payload loaded.', '');
    setStatus('offline-account-status', 'No account payload loaded.', '');
  }

  function initialize() {
    if (!byId('offline-inventory-snapshot')) return;
    byId('offline-character-load').addEventListener('click', () => loadPayload('character'));
    byId('offline-account-load').addEventListener('click', () => loadPayload('account'));
    byId('offline-inventory-clear').addEventListener('click', clearViewer);
    window.addEventListener('rtm-local-item-catalogs-changed', () => {
      if (lastCharacterRows) renderRows(byId('offline-character-rows'), lastCharacterRows, 'No populated character inventory slots in this payload.', true);
      if (lastAccountRows) renderRows(byId('offline-vault-candidate-rows'), lastAccountRows, 'No nonzero item-instance fields were found in this candidate block.');
    });
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
  window.rtmOfflineInventorySnapshot = Object.freeze({ characterFileBytes: CHARACTER_FILE_BYTES, decodeCharacterBuffer, compareCharacterRows });
})();
