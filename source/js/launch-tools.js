(function (root) {
  'use strict';

  const STORAGE_KEY = 'rtm_launch_iso_path';
  const DEFAULT_PATH = '/dev_hdd0/PS3ISO/Destiny.iso';
  const PATH_PATTERN = /^\/dev_hdd0\/PS3ISO\/[A-Za-z0-9][A-Za-z0-9 _().-]{0,124}\.iso$/i;

  function validateISOPath(value) {
    const path = String(value ?? '');
    if (path.length > 160 || path.includes('..') || !PATH_PATTERN.test(path)) {
      throw new Error('Use one .iso file directly under /dev_hdd0/PS3ISO/');
    }
    return path;
  }

  function setStatus(message, state = '') {
    const element = root.document?.getElementById('rtm-launch-status');
    if (!element) return;
    element.textContent = message;
    if (state) element.dataset.state = state;
    else delete element.dataset.state;
  }

  function delay(milliseconds) {
    return new Promise(resolve => root.setTimeout(resolve, milliseconds));
  }

  async function launchDestiny() {
    const input = root.document?.getElementById('rtm-launch-iso-path');
    const button = root.document?.getElementById('btn-launch-destiny');
    const badge = root.document?.getElementById('status-badge');
    if (!input || !button) return;
    if (button.disabled) return;
    if (!ip || !badge?.classList.contains('connected')) {
      setStatus('Link the PS3 before sending launch requests.', 'error');
      return;
    }

    let isoPath;
    try {
      isoPath = validateISOPath(input.value);
    } catch (error) {
      setStatus(error.message, 'error');
      return;
    }

    try { root.localStorage.setItem(STORAGE_KEY, isoPath); } catch (_) {}
    const target = ip;
    const generation = connectionGeneration;
    let stage = 'mount ISO';
    button.disabled = true;
    input.disabled = true;
    try {
      setStatus(`Sending mount request to ${target}…`);
      await ps3fetch(`http://${target}/mount.ps3${isoPath}&mode=silent`);
      if (generation !== connectionGeneration) throw new Error('Console connection changed; sequence stopped.');

      setStatus('Mount request acknowledged. Waiting 3 seconds before returning to the game list…', 'ok');
      await delay(3000);
      if (generation !== connectionGeneration) throw new Error('Console connection changed; sequence stopped.');

      stage = 'return to the game list';
      setStatus('Sending game-list request…');
      await ps3fetch(`http://${target}/wmnav.ps3/game`);
      if (generation !== connectionGeneration) throw new Error('Console connection changed; sequence stopped.');

      setStatus('Game-list request acknowledged. Waiting 1 second before starting the mounted game…', 'ok');
      await delay(1000);
      if (generation !== connectionGeneration) throw new Error('Console connection changed; sequence stopped.');

      stage = 'start mounted game';
      setStatus('Sending start request…');
      await ps3fetch(`http://${target}/play.ps3`);
      if (generation !== connectionGeneration) throw new Error('Console connection changed; sequence stopped.');

      setStatus('All three requests received HTTP acknowledgement. Destiny start is not confirmed.', 'ok');
      log('Launch sequence HTTP-acknowledged; Destiny start is not confirmed.', 'ok');
    } catch (error) {
      const message = String(error?.message || error);
      setStatus(`${stage} stopped: ${message}`, 'error');
      log(`Launch sequence stopped during ${stage}.`, 'err');
      toast('Launch sequence stopped', 'err');
    } finally {
      button.disabled = false;
      input.disabled = false;
    }
  }

  root.RtmLaunchTools = { validateISOPath, launchDestiny, defaultPath: DEFAULT_PATH };
  root.launchDestiny = launchDestiny;
  if (typeof module !== 'undefined') module.exports = root.RtmLaunchTools;

  function initializeLaunchControls() {
    const input = root.document.getElementById('rtm-launch-iso-path');
    if (input) {
      try {
        const saved = root.localStorage.getItem(STORAGE_KEY);
        if (saved) input.value = validateISOPath(saved);
      } catch (_) {
        input.value = DEFAULT_PATH;
      }
      input.addEventListener('change', () => {
        try {
          const path = validateISOPath(input.value);
          root.localStorage.setItem(STORAGE_KEY, path);
          setStatus('ISO path saved. Game start cannot be confirmed by the launch sequence.');
        } catch (error) {
          setStatus(error.message, 'error');
        }
      });
    }
  }

  if (root.document) {
    if (root.document.readyState === 'loading') root.document.addEventListener('DOMContentLoaded', initializeLaunchControls, { once: true });
    else initializeLaunchControls();
  }
})(globalThis);
