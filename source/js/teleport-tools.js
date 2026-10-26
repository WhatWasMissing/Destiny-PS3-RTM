(function () {
  'use strict';
  const undo = [];
  window.tpRemember = function () {
    undo.push(JSON.stringify({ savedTeleports, teleportFolders }));
    if (undo.length > 20) undo.shift();
  };
  window.tpEditSlot = function (index) {
    const position = savedTeleports[index]; if (!position) return;
    const name = prompt('Location name:', position.name); if (name === null) return;
    if (!name.trim() || name.length > 200) { toast('Enter a name of 1â€“200 characters', 'err'); return; }
    const next = { ...position, name: name.trim() };
    for (const axis of ['x','y','z']) {
      const value = prompt(axis.toUpperCase() + ' coordinate:', position[axis]);
      if (value === null) return;
      if (!value.trim() || !Number.isFinite(Number(value))) { toast('Coordinates must be finite numbers', 'err'); return; }
      next[axis] = Number(value);
    }
    tpRemember(); savedTeleports[index] = next; saveTeleportsToStorage(); tpRenderSlots();
  };
  window.tpIsDuplicate = function (position) {
    return savedTeleports.some(existing => ['x','y','z'].every(axis => Number(existing[axis]) === Number(position[axis])));
  };
  function exportPositions() {
    const data = { format: 'destiny-rtm-settings', version: 1, settings: {
      rtm_saved_teleports: JSON.stringify(savedTeleports), rtm_teleport_folders: JSON.stringify(teleportFolders)
    } };
    const url = URL.createObjectURL(new Blob([JSON.stringify(data, null, 2)], { type: 'application/json' }));
    const anchor = document.createElement('a'); anchor.href = url; anchor.download = 'destiny-rtm-teleports.json'; anchor.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  document.addEventListener('DOMContentLoaded', () => setTimeout(() => {
    const list = document.getElementById('tp-saved-list'); if (!list) return;
    const toolbar = document.createElement('div'); toolbar.className = 'connect-row'; toolbar.style.flexWrap = 'wrap';
    function button(label, handler) {
      const element = document.createElement('button'); element.className = 'slider-preset'; element.textContent = label;
      element.onclick = () => { try { handler(); } catch (error) { toast(error.message, 'err'); } }; toolbar.append(element);
    }
    button('Sort by name', () => { tpRemember(); savedTeleports.sort((a,b) => a.name.localeCompare(b.name)); saveTeleportsToStorage(); tpRenderSlots(); });
    button('Undo last change', () => {
      if (!undo.length) { toast('No teleport changes to undo', ''); return; }
      const previous = JSON.parse(undo.pop()); savedTeleports = previous.savedTeleports; teleportFolders = previous.teleportFolders;
      saveTeleportsToStorage(); tpRenderSlots();
    });
    button('Export positions', exportPositions);
    const fileInput = document.createElement('input'); fileInput.type = 'file'; fileInput.accept = '.json'; fileInput.hidden = true;
    button('Import positions', () => fileInput.click());
    fileInput.onchange = async () => {
      try {
        const file = fileInput.files[0]; if (!file) return;
        if (file.size > 5000000) throw new Error('Position file exceeds the 5 MB limit');
        let data = JSON.parse(await file.text());
        if (Array.isArray(data)) data = { format: 'destiny-rtm-settings', version: 1, settings: { rtm_saved_teleports: JSON.stringify(data) } };
        RtmBackups.validate(data);
        if (!data.settings.rtm_saved_teleports) throw new Error('No positions found in this backup');
        const positions = JSON.parse(data.settings.rtm_saved_teleports);
        const folders = JSON.parse(data.settings.rtm_teleport_folders || '[]');
        if (!confirm(`Merge ${positions.length} positions? Duplicate coordinates will be skipped.`)) return;
        tpRemember();
        for (const folder of folders) if (!teleportFolders.some(existing => existing.id === folder.id)) teleportFolders.push(folder);
        let added = 0;
        for (const position of positions) {
          if (tpIsDuplicate(position)) continue;
          savedTeleports.push({ ...position, folderId: teleportFolders.some(folder => folder.id === position.folderId) ? position.folderId : 'unsorted' }); added++;
        }
        saveTeleportsToStorage(); tpRenderSlots(); toast(`Imported ${added} positions`, 'ok');
      } catch (error) { toast(error.message, 'err'); }
      finally { fileInput.value = ''; }
    };
    toolbar.append(fileInput); list.before(toolbar);
  }, 0));
})();
