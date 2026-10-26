(function (root) {
  'use strict';

  const memory = root.RtmMemoryFeatures;

  function setStatus(message, state = '') {
    const element = document.getElementById('brightness-status');
    if (!element) return;
    element.textContent = message;
    element.dataset.state = state;
  }

  async function readBrightnessCandidate() {
    const current = document.getElementById('brightness-current');
    const button = document.getElementById('brightness-read-btn');
    const badge = document.getElementById('status-badge');
    if (!current || !memory) return;
    if (!ip || !badge?.classList.contains('connected')) {
      setStatus('Connect to the Destiny process before reading this address.', 'error');
      return;
    }

    const generation = connectionGeneration;
    const targetIp = ip;
    if (button) button.disabled = true;
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
        setStatus(`Matches the source “${preset.label}” value (${preset.value}). The 01.24 ELF stores Normal (1.0) at this candidate address, but a live read does not prove the field's meaning or in-game effect. Probe only.`, 'warning');
      } else {
        setStatus('Does not match any listed source preset. The 01.24 ELF confirms this candidate address holds Normal (1.0) by default; no write was attempted.', 'warning');
      }
    } catch (error) {
      if (generation !== connectionGeneration || targetIp !== ip) {
        setStatus('Connection changed during the read; result discarded. Read again on the current session.', 'error');
      } else {
        current.textContent = 'Current word: read failed';
        setStatus(`Read failed: ${error.message}`, 'error');
      }
    } finally {
      if (generation === connectionGeneration && targetIp === ip && button) button.disabled = false;
    }
  }

  root.readBrightnessCandidate = readBrightnessCandidate;
})(globalThis);
