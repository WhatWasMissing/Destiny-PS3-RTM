// User-triggered adapter for Blackrock's recovered operation-1 spawn executor.
(function (root) {
  'use strict';

  let controller = null;
  let busy = false;
  const MAX_SPAWN_OPTIONS = 1500;

  function element(id) { return document.getElementById(id); }

  function setStatus(message, state = '') {
    const status = element('spawn-executor-status');
    if (!status) return;
    status.textContent = message;
    status.dataset.state = state;
  }

  function setBusy(value) {
    busy = value;
    for (const id of [
      'spawn-executor-install-btn', 'spawn-executor-submit-btn',
      'spawn-executor-cancel-btn', 'spawn-executor-reconcile-btn', 'spawn-executor-recover-btn'
    ]) {
      const button = element(id);
      if (button) button.disabled = value;
    }
  }

  function getController() {
    if (controller) return controller;
    const memory = root.rtmActivitySessionMemory;
    const writeApi = root.rtmSpawnExecutorMemory;
    const packet = root.DESTINY_RTM_SPAWN_PACKET;
    const dataBlock = root.DESTINY_RTM_SPAWN_DATA;
    const images = root.DESTINY_RTM_SPAWN_EXECUTOR_IMAGES;
    const api = root.RtmSpawnExecutorController;
    if (!memory || !writeApi || !packet || !dataBlock || !images || !api ||
        !Array.isArray(root.rtmSpawnPathSignatures) || !Array.isArray(root.rtmSpawnExecutorRegions)) {
      throw new Error('Spawn executor components are not available in this app build');
    }
    controller = api.create({
      memory,
      profileAnchorCount: root.RtmBuildCompatibility.anchorCount,
      writeIfEqual: writeApi.writeIfEqual,
      signatures: root.rtmSpawnPathSignatures,
      regions: root.rtmSpawnExecutorRegions,
      packet,
      dataBlock,
      images,
      storage: root.localStorage,
      onProgress(progress) {
        const active = element('spawn-executor-progress');
        if (active) active.textContent = `Slot 0x${progress.slotBase.toString(16).toUpperCase()} · worker status ${progress.status} · completed ${progress.progress}`;
        const cancel = element('spawn-executor-cancel-btn');
        if (cancel) cancel.disabled = !busy || (progress.status !== 1 && progress.status !== 2);
      }
    });
    return controller;
  }

  function updateJournalState() {
    try {
      const snapshot = getController().state();
      const journal = snapshot.journal;
      const state = journal ? `${journal.state}${journal.phase ? ` · ${journal.phase}` : ''}` : 'not installed';
      const field = element('spawn-executor-journal-state');
      if (field) field.textContent = `Journal: ${state}`;
      const install = element('spawn-executor-install-btn');
      const submit = element('spawn-executor-submit-btn');
      const cancel = element('spawn-executor-cancel-btn');
      const reconcile = element('spawn-executor-reconcile-btn');
      const recover = element('spawn-executor-recover-btn');
      if (install) install.disabled = busy || journal?.state === 'installed';
      if (submit) submit.disabled = busy || journal?.state !== 'installed' || Boolean(journal?.pendingCommand);
      if (cancel && !busy) cancel.disabled = !journal?.pendingCommand && !snapshot.active;
      if (reconcile) reconcile.disabled = busy || !journal?.pendingCommand;
      if (recover) recover.disabled = busy || !journal || journal.state === 'recovered-clean' || Boolean(journal?.pendingCommand);
    } catch (error) {
      const field = element('spawn-executor-journal-state');
      if (field) field.textContent = `Journal unavailable: ${String(error?.message || error)}`;
    }
  }

  function numberValue(id, name, integer = false) {
    const text = element(id)?.value.trim();
    if (!text) throw new Error(`${name} is required`);
    const value = Number(text);
    if (!Number.isFinite(value) || (integer && !Number.isInteger(value))) throw new Error(`${name} must be a ${integer ? 'whole number' : 'finite number'}`);
    return value;
  }

  function readFields() {
    const expectedType = numberValue('spawn-expected-type', 'Expected type', true);
    const tagText = element('spawn-tag-input')?.value.trim().replace(/^0x/i, '');
    if (!tagText || !/^[0-9a-f]{1,8}$/i.test(tagText)) throw new Error('Enter a 32-bit tag as up to eight hexadecimal digits');
    const pose = Array.from({ length: 9 }, (_, index) => numberValue(`spawn-pose-${index}`, `Pose ${index}`));
    return {
      expectedType,
      tag: Number.parseInt(tagText, 16) >>> 0,
      placementMode94: numberValue('spawn-mode-94', 'Mode +0x94', true),
      placementMode98: numberValue('spawn-mode-98', 'Mode +0x98', true),
      count: numberValue('spawn-count', 'Count', true),
      placementModeA0: numberValue('spawn-mode-a0', 'Mode +0xA0', true),
      pose
    };
  }

  function setSpawnPlacementPreset(preset) {
    const placement = element('spawn-mode-94');
    const source = element('spawn-mode-a0');
    if (!placement || !source) return;
    if (preset === 'crosshair') {
      placement.value = '0';
      setStatus('Placement set to Crosshair. The emitted command uses screen center and the selected distance.', 'matched');
      return;
    }
    if (preset === 'player') {
      placement.value = '1';
      source.value = '0';
      setStatus('Placement set to My Position using the verified engine position at sObject +0xB0.', 'matched');
      return;
    }
    setStatus('Unknown placement shortcut.', 'error');
  }

  function refreshSpawnCatalog() {
    const select = element('spawn-catalog-entry');
    const search = element('spawn-catalog-search');
    const status = element('spawn-catalog-status');
    if (!select || !status) return;
    const api = root.rtmLocalSpawnCatalogs;
    const entries = api && typeof api.listCandidates === 'function' ? api.listCandidates() : [];
    const query = (search?.value || '').trim().toLocaleLowerCase();
    const matches = entries.map((entry, index) => ({ entry, index })).filter(({ entry }) =>
      !query || `${entry.tag} ${entry.label} ${entry.source}`.toLocaleLowerCase().includes(query));
    select.replaceChildren();
    const placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = matches.length ? 'Choose an imported tag…' : 'No matching imported tags';
    select.append(placeholder);
    matches.slice(0, MAX_SPAWN_OPTIONS).forEach(({ entry, index }) => {
      const option = document.createElement('option');
      option.value = String(index);
      option.textContent = `${entry.label} · ${entry.tag} · ${entry.source}`;
      select.append(option);
    });
    select.disabled = matches.length === 0;
    const capNote = matches.length > MAX_SPAWN_OPTIONS ? ` · showing first ${MAX_SPAWN_OPTIONS}` : '';
    status.textContent = matches.length
      ? `${matches.length} matching tag${matches.length === 1 ? '' : 's'} from imported catalogs${capNote}. Selecting one copies only its tag.`
      : 'Import a Spawn / hop-on catalog with a tag column, then filter to find a candidate.';
  }

  function chooseSpawnCatalogEntry() {
    const select = element('spawn-catalog-entry');
    const tagInput = element('spawn-tag-input');
    const status = element('spawn-catalog-status');
    if (!select?.value) return;
    const index = Number(select.value);
    const entries = root.rtmLocalSpawnCatalogs?.listCandidates?.() || [];
    if (!Number.isInteger(index) || index < 0 || !entries[index] || !tagInput || !status) return;
    const entry = entries[index];
    tagInput.value = entry.tag;
    const expectedType = element('spawn-expected-type')?.value || 'unset';
    status.textContent = `Copied ${entry.tag} (${entry.label}) from ${entry.source}. Expected runtime class stays ${expectedType}; catalog type is not mapped. Live BLES resolution and enemy class remain unconfirmed.`;
  }

  async function runAction(label, action) {
    if (busy) return;
    setBusy(true);
    try {
      setStatus(`${label}…`, 'pending');
      const result = await action(getController());
      const output = element('spawn-executor-output');
      output.textContent = JSON.stringify(result, null, 2);
      output.hidden = false;
      if (result?.pending) setStatus('Request is still active in Destiny. Wait, or request cancellation before recovery.', 'pending');
      else if (result?.state === 'recovered-resident') setStatus(result.message || 'Hook restored; restart Destiny before another install.', 'warning');
      else if (result?.terminalStatus === 4) setStatus(`${label} ended with worker failure status 4; inspect the raw rows and retry only after recovery if needed.`, 'error');
      else if (result?.terminalStatus === 0) setStatus('The slot is idle and no worker completion was recorded. The pending state was cleared.', 'pending');
      else setStatus(`${label} finished.`, 'matched');
    } catch (error) {
      setStatus(`${label} stopped: ${String(error?.message || error)}`, 'error');
    } finally {
      setBusy(false);
      updateJournalState();
    }
  }

  async function installSpawnExecutor() {
    if (!root.confirm('Install the experimental Blackrock spawn executor on the currently linked BLES01857 01.24 console? This writes PPC code and changes the hook at 0x00354074. A mismatch or unsupported instruction-cache behavior can freeze or crash Destiny. Use a development console and a recoverable session.')) return;
    await runAction('Executor install', executor => executor.install());
  }

  async function submitSpawnRequest() {
    let fields;
    try { fields = readFields(); } catch (error) { setStatus(String(error?.message || error), 'error'); return; }
    if (!root.confirm('Submit this spawn request to Destiny now? The tag, class mapping, placement, and live BLES behavior have not been validated.')) return;
    await runAction('Spawn request', executor => executor.spawn(fields));
  }

  async function cancelSpawnRequest() {
    if (!busy) {
      try {
        if (!getController().state().journal?.pendingCommand) return;
      } catch (_) { return; }
    }
    const button = element('spawn-executor-cancel-btn');
    if (button) button.disabled = true;
    try {
      const result = await getController().cancelCurrent();
      const output = element('spawn-executor-output');
      output.textContent = JSON.stringify(result, null, 2);
      output.hidden = false;
      setStatus('Cancellation requested. The worker must finish before recovery is safe.', 'pending');
      if (button) button.disabled = false;
    } catch (error) {
      setStatus(`Cancel request stopped: ${String(error?.message || error)}`, 'error');
      if (button) button.disabled = false;
    }
  }

  async function recoverSpawnExecutor() {
    await runAction('Executor recovery', executor => executor.recover());
  }

  async function reconcileSpawnRequest() {
    await runAction('Request status check', executor => executor.reconcilePending());
  }

  function initialize() {
    if (!element('spawn-executor-panel')) return;
    element('spawn-catalog-search')?.addEventListener('input', refreshSpawnCatalog);
    element('spawn-catalog-entry')?.addEventListener('change', chooseSpawnCatalogEntry);
    document.addEventListener('keydown', event => {
      if (!event.ctrlKey || event.metaKey || event.altKey || event.shiftKey || event.key !== 'Enter' || event.isComposing) return;
      const panel = element('spawn-executor-panel');
      const submit = element('spawn-executor-submit-btn');
      if (!panel?.contains(event.target) || !submit || submit.disabled) return;
      if (event.target?.closest?.('pre,[contenteditable="true"]')) return;
      event.preventDefault();
      submitSpawnRequest();
    });
    root.addEventListener('rtm-local-item-catalogs-changed', refreshSpawnCatalog);
    refreshSpawnCatalog();
    updateJournalState();
  }

  root.installSpawnExecutor = installSpawnExecutor;
  root.setSpawnPlacementPreset = setSpawnPlacementPreset;
  root.submitSpawnRequest = submitSpawnRequest;
  root.cancelSpawnRequest = cancelSpawnRequest;
  root.reconcileSpawnRequest = reconcileSpawnRequest;
  root.recoverSpawnExecutor = recoverSpawnExecutor;
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
})(window);
