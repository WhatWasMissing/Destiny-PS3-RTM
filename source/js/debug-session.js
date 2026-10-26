
  // ── Session-only debug warning bypass ───────────────────────────────────
  const DEBUG_MODE_SESSION_KEY = 'destinyRtmDebugWarningBypassV1';
  const DEBUG_MODE_PIN = '1982';

  function isDebugModeEnabled() {
    try {
      return sessionStorage.getItem(DEBUG_MODE_SESSION_KEY) === '1';
    } catch (e) {
      return false;
    }
  }

  function setDebugModeEnabled(enabled) {
    try {
      if (enabled) sessionStorage.setItem(DEBUG_MODE_SESSION_KEY, '1');
      else sessionStorage.removeItem(DEBUG_MODE_SESSION_KEY);
    } catch (e) {}
    updateDebugModeUI();
  }

  function updateDebugModeUI() {
    const active = isDebugModeEnabled();
    const badge = document.getElementById('debug-mode-badge');
    const status = document.getElementById('debug-login-status');
    const input = document.getElementById('debug-pin-input');
    const btn = document.getElementById('debug-pin-btn');

    if (badge) badge.style.display = active ? 'block' : 'none';
    if (input) {
      input.disabled = active;
      if (active) input.value = '';
    }
    if (btn) {
      btn.disabled = active;
      btn.textContent = active ? 'Enabled' : 'Enable';
      btn.style.opacity = active ? '0.45' : '';
    }
    if (active && status) {
      status.textContent = 'Debug mode enabled for this session.';
      status.className = 'debug-login-status ok';
    }
  }

  function enableDebugModeFromPin() {
    const input = document.getElementById('debug-pin-input');
    const status = document.getElementById('debug-login-status');
    if (!input) return;

    if (input.value.trim() !== DEBUG_MODE_PIN) {
      if (status) {
        status.textContent = 'Incorrect debug PIN.';
        status.className = 'debug-login-status err';
      }
      input.value = '';
      input.focus();
      return;
    }

    setDebugModeEnabled(true);

    // Unlock warning-gated controls immediately without permanently marking
    // either guide as read.
    if (typeof setServerCheatsLocked === 'function') {
      serverSafetyRequired = false;
      setServerCheatsLocked(false);
      const overlay = document.getElementById('server-safety-overlay');
      if (overlay) overlay.classList.remove('show');
    }
    if (typeof setActivityLoaderLocked === 'function') {
      activityGuideRequired = false;
      setActivityLoaderLocked(false);
      const overlay = document.getElementById('activity-guide-overlay');
      if (overlay) overlay.classList.remove('show');
    }
  }

  window.isDebugModeEnabled = isDebugModeEnabled;
  window.enableDebugModeFromPin = enableDebugModeFromPin;

  document.addEventListener('DOMContentLoaded', updateDebugModeUI);
