// Compare two explicitly selected BLES 01.24 character payloads by slot and item index only.
(function () {
  'use strict';

  let lastDiff = null;
  const byId = id => document.getElementById(id);

  function describeItem(row) {
    if (!row) return '—';
    const metadata = window.localInventoryItemMetadata?.(row.itemIndex);
    const name = metadata?.name || window.localInventoryItemName?.(row.itemIndex);
    return name ? `${row.itemIndex} · ${name}` : String(row.itemIndex);
  }

  function render() {
    const output = byId('offline-inventory-diff-rows');
    output.replaceChildren();
    output.hidden = true;
    if (!lastDiff) return;
    output.hidden = false;

    if (!lastDiff.changes.length) {
      const empty = document.createElement('p');
      empty.className = 'offline-inventory-empty';
      empty.textContent = 'No slot or item-index changes were found.';
      output.append(empty);
      return;
    }

    const table = document.createElement('table');
    table.className = 'offline-inventory-table';
    const head = document.createElement('thead');
    const header = document.createElement('tr');
    for (const label of ['Slot', 'Change', 'Before item', 'After item']) {
      const cell = document.createElement('th');
      cell.scope = 'col';
      cell.textContent = label;
      header.append(cell);
    }
    head.append(header);

    const body = document.createElement('tbody');
    for (const change of lastDiff.changes) {
      const row = document.createElement('tr');
      const slot = document.createElement('td');
      const kind = document.createElement('td');
      const before = document.createElement('td');
      const after = document.createElement('td');
      slot.className = 'inventory-slot-cell';
      slot.textContent = String(change.slot);
      kind.textContent = change.change;
      before.textContent = describeItem(change.before);
      after.textContent = describeItem(change.after);
      row.append(slot, kind, before, after);
      body.append(row);
    }
    table.append(head, body);
    output.append(table);
  }

  function setStatus(message, state) {
    const status = byId('offline-inventory-diff-status');
    status.textContent = message;
    if (state) status.dataset.state = state;
    else delete status.dataset.state;
  }

  async function compareFiles() {
    const beforeInput = byId('offline-inventory-before-file');
    const afterInput = byId('offline-inventory-after-file');
    const button = byId('offline-inventory-compare');
    const beforeFile = beforeInput.files && beforeInput.files[0];
    const afterFile = afterInput.files && afterInput.files[0];
    const decoder = window.rtmOfflineInventorySnapshot;
    lastDiff = null;
    render();

    if (!beforeFile || !afterFile) {
      setStatus('Choose both before and after character files.', 'error');
      return;
    }
    if (!decoder?.decodeCharacterBuffer || !decoder?.compareCharacterRows) {
      setStatus('The BLES character snapshot decoder is unavailable.', 'error');
      return;
    }
    for (const file of [beforeFile, afterFile]) {
      if (file.size !== decoder.characterFileBytes) {
        setStatus(`Rejected a file with ${file.size.toLocaleString()} bytes; expected exactly ${decoder.characterFileBytes.toLocaleString()}.`, 'error');
        return;
      }
    }

    button.disabled = true;
    beforeInput.disabled = true;
    afterInput.disabled = true;
    setStatus('Reading both files in this page…', 'pending');
    try {
      const [beforeBuffer, afterBuffer] = await Promise.all([beforeFile.arrayBuffer(), afterFile.arrayBuffer()]);
      const beforeRows = decoder.decodeCharacterBuffer(beforeBuffer);
      const afterRows = decoder.decodeCharacterBuffer(afterBuffer);
      lastDiff = decoder.compareCharacterRows(beforeRows, afterRows);
      render();

      const added = lastDiff.changes.filter(row => row.change === 'Added').length;
      const removed = lastDiff.changes.filter(row => row.change === 'Removed').length;
      const changed = lastDiff.changes.filter(row => row.change === 'Changed').length;
      setStatus(`Compared by slot and item index: ${added} added, ${removed} removed, ${changed} changed, ${lastDiff.unchangedCount} unchanged. Different instances with the same item index are not distinguishable.`, 'ok');
    } catch (error) {
      setStatus(`Could not compare these payloads: ${String(error?.message || error).slice(0, 180)}`, 'error');
    } finally {
      button.disabled = false;
      beforeInput.disabled = false;
      afterInput.disabled = false;
      beforeInput.value = '';
      afterInput.value = '';
    }
  }

  function clearComparison() {
    lastDiff = null;
    byId('offline-inventory-before-file').value = '';
    byId('offline-inventory-after-file').value = '';
    render();
    setStatus('No comparison loaded.', '');
  }

  function initialize() {
    if (!byId('offline-inventory-comparison')) return;
    byId('offline-inventory-compare').addEventListener('click', compareFiles);
    byId('offline-inventory-clear').addEventListener('click', clearComparison);
    window.addEventListener('rtm-local-item-catalogs-changed', render);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
})();
