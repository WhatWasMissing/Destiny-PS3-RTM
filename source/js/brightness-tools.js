(function (root) {
  'use strict';

  const memory = root.RtmMemoryFeatures;
  let brightnessBusy = false;

  function setControlsDisabled(disabled) {
    for (const id of ['brightness-read-btn', 'brightness-preset-bright', 'brightness-preset-normal', 'brightness-preset-inverted']) {
      const button = document.getElementById(id);
      if (button) button.disabled = disabled;
    }
  }

  function setStatus(message, state = '') {
    const element = document.getElementById('brightness-status');
    if (!element) return;
    element.textContent = message;
    element.dataset.state = state;
  }

  async function readBrightnessCandidate() {
    const current = document.getElementById('brightness-current');
    const badge = document.getElementById('status-badge');
    if (!current || !memory) return;
    if (brightnessBusy) return;
    if (!ip || !badge?.classList.contains('connected')) {
      setStatus('Connect to the Destiny process before reading this address.', 'error');
      return;
    }

    brightnessBusy = true;
    const generation = connectionGeneration;
    const targetIp = ip;
    setControlsDisabled(true);
    current.textContent = 'Current word: reading…';
    setStatus('Reading four bytes from the source RTM address…', 'pending');

    try {
      const raw = String(await ps3getmem(memory.BRIGHTNESS_ADDRESS, 4)).replace(/\s/g, '').toUpperCase();
      if (generation !== connectionGeneration || targetIp !== ip) {
        setStatus('Connection changed during the read; result discarded. Read again on the current session.', 'error');
        current.textContent = 'Current word: not read';
        return;
      }
      if (!/^(?:[0-9A-F]{2}){4}$/.test(raw)) throw new Error('Console returned an invalid four-byte value');

      const value = memory.float32FromBE(raw);
      const formatted = Number.isFinite(value) ? value.toFixed(4) : String(value);
      const preset = memory.brightnessPresetForHex(raw);
      current.textContent = `Current word: ${raw} · Float32 ${formatted}`;
      if (preset) {
        setStatus(`Matches the source “${preset.label}” value (${preset.value}). The 01.24 ELF stores Normal (1.0) here; this read does not verify the in-game effect.`, 'warning');
      } else {
        setStatus('Does not match any listed source preset. The 01.24 ELF stores Normal (1.0) at this address by default; no write was attempted.', 'warning');
      }
    } catch (error) {
      if (generation !== connectionGeneration || targetIp !== ip) {
        setStatus('Connection changed during the read; result discarded. Read again on the current session.', 'error');
      } else {
        current.textContent = 'Current word: read failed';
        setStatus(`Read failed: ${error.message}`, 'error');
      }
    } finally {
      brightnessBusy = false;
      setControlsDisabled(false);
    }
  }

  async function applyBrightnessPreset(name) {
    const current = document.getElementById('brightness-current');
    const badge = document.getElementById('status-badge');
    if (!current || !memory) return;
    if (brightnessBusy) return;
    if (!ip || !badge?.classList.contains('connected')) {
      setStatus('Connect to the Destiny process before changing brightness.', 'error');
      return;
    }

    let preset;
    try { preset = memory.brightnessPreset(name); }
    catch (error) { setStatus(`Brightness preset unavailable: ${error.message}`, 'error'); return; }

    brightnessBusy = true;
    const generation = connectionGeneration;
    const targetIp = ip;
    setControlsDisabled(true);
    setStatus(`Writing ${preset.label} (${preset.value}) to 0x006101C0 and verifying readback…`, 'pending');
    try {
      const address = memory.BRIGHTNESS_ADDRESS.toString(16).padStart(8, '0').toUpperCase();
      await verifiedMemoryValue(address, preset.hex);
      if (generation !== connectionGeneration || targetIp !== ip) {
        setStatus('Connection changed during the write; result discarded. Read again on the current session.', 'error');
        current.textContent = 'Current word: not read';
        return;
      }
      current.textContent = `Current word: ${preset.hex} · Float32 ${Number(preset.value).toFixed(4)}`;
      setStatus(`${preset.label} (${preset.value}) write verified. The 01.24 ELF has Normal (1.0) at this address by default; the in-game effect has not been observed.`, 'warning');
    } catch (error) {
      if (generation !== connectionGeneration || targetIp !== ip) {
        setStatus('Connection changed during the write; result discarded. Read again on the current session.', 'error');
      } else {
        current.textContent = 'Current word: write not verified';
        setStatus(`Brightness write failed: ${error.message}`, 'error');
      }
    } finally {
      brightnessBusy = false;
      setControlsDisabled(false);
    }
  }

  root.readBrightnessCandidate = readBrightnessCandidate;
  root.applyBrightnessPreset = applyBrightnessPreset;
})(globalThis);
