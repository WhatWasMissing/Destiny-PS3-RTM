let ps3mapiHealthProbeSupported = true;
const CONNECTION_EVENT_LIMIT = 100;
const connectionEvents = [];

function recordConnectionEvent(event) {
  connectionEvents.push({ time: new Date().toISOString(), event });
  if (connectionEvents.length > CONNECTION_EVENT_LIMIT) connectionEvents.splice(0, connectionEvents.length - CONNECTION_EVENT_LIMIT);
}

window.rtmConnectionDiagnostics = () => connectionEvents.map(event => ({ ...event }));

  async function ps3Shutdown() {
    if (window.rtmFramebufferBusy) { toast('Finish the framebuffer capture or restore the saved pause byte first', 'err'); return; }
    if (!ip) return;
    if (!await RtmDialogs.confirm('Shut down the PS3?')) return;
    const target = ip;
    const generation = ++connectionGeneration;
    recordConnectionEvent('shutdown-requested');
    stopHealthCheck();
    stopAutoReconnect();
    consoleTransport.cancelAll();
    resetVerifiedStates();
    setBadge('connecting', 'SHUTDOWN REQUEST');
    log('Sending shutdown command…', '');
    let acknowledged = false;
    try {
      await consoleTransport.request(`http://${target}/shutdown.ps3`,5000);
      if (generation !== connectionGeneration) return;
      acknowledged = true;
      recordConnectionEvent('shutdown-acknowledged');
      log('Shutdown request acknowledged', '');
    } catch(e) {
      if (generation !== connectionGeneration) return;
      if (isDefiniteCommandRejection(e)) {
        recordConnectionEvent('shutdown-rejected');
        setBadge('connected', ps3mapiHealthProbeSupported ? 'ONLINE' : 'WEBMAN ONLINE');
        log(`Shutdown request was rejected; console remains linked: ${e.message}`, 'err');
        toast('Shutdown request was rejected', 'err');
        if (ps3mapiHealthProbeSupported) startHealthCheck();
        return;
      }
      log(`Shutdown response unavailable; console shutdown is not confirmed: ${e.message}`, 'err');
      recordConnectionEvent('shutdown-unconfirmed');
    }
    if (generation !== connectionGeneration) return;
    ip = '';
    setBadge('', 'OFFLINE');
    document.getElementById('btn-connect').style.display = '';
    document.getElementById('btn-connect').disabled = false;
    document.getElementById('btn-disconnect').style.display = 'none';
    document.getElementById('btn-shutdown').classList.remove('enabled');
    document.getElementById('btn-restart').classList.remove('enabled');
    toast(acknowledged ? 'PS3 shutdown requested' : 'Shutdown status unknown; reconnect to check', acknowledged ? 'ok' : 'err');
  }

  async function ps3Restart() {
    if (window.rtmFramebufferBusy) { toast('Finish the framebuffer capture or restore the saved pause byte first', 'err'); return; }
    if (!ip || !await RtmDialogs.confirm('Restart the PS3?')) return;
    const target = ip;
    const generation = ++connectionGeneration;
    recordConnectionEvent('restart-requested');
    stopAutoReconnect(); stopHealthCheck(); consoleTransport.cancelAll(); resetVerifiedStates();
    setBadge('connecting','RESTART REQUEST');
    try {
      await consoleTransport.request(`http://${target}/restart.ps3`,5000);
      if (generation !== connectionGeneration) return;
      log('Restart request acknowledged; waiting for console availability','');
      recordConnectionEvent('restart-acknowledged');
    } catch (error) {
      if (generation !== connectionGeneration) return;
      if (isDefiniteCommandRejection(error)) {
        recordConnectionEvent('restart-rejected');
        setBadge('connected', ps3mapiHealthProbeSupported ? 'ONLINE' : 'WEBMAN ONLINE');
        log(`Restart request was rejected; console remains linked: ${error.message}`, 'err');
        toast('Restart request was rejected', 'err');
        if (ps3mapiHealthProbeSupported) startHealthCheck();
        return;
      }
      log(`Restart response unavailable; restart is not confirmed: ${error.message}`,'err');
      recordConnectionEvent('restart-unconfirmed');
    }
    if (generation !== connectionGeneration) return;
    ip = '';
    document.getElementById('btn-connect').style.display = '';
    document.getElementById('btn-connect').disabled = false;
    document.getElementById('btn-disconnect').style.display = 'none';
    document.getElementById('btn-shutdown').classList.remove('enabled');
    document.getElementById('btn-restart').classList.remove('enabled');
    await sleep(15000);
    if (generation !== connectionGeneration) return;
    recordConnectionEvent('restart-relink-started');
    beginSafeAutoReconnect(target, true);
  }

  async function doubleBeep() {
    await beep(1);
    await new Promise(r => setTimeout(r, 300));
    await beep(1);
  }

  async function tripleBeep() {
    await beep(2);
    await new Promise(r => setTimeout(r, 250));
    await beep(2);
    await new Promise(r => setTimeout(r, 250));
    await beep(2);
  }

  function setBadge(state, label) {
    const b = document.getElementById('status-badge');
    b.className = 'status-badge ' + state;
    b.textContent = label;
    const canRelink = state !== 'connected' && state !== 'connecting';
    b.classList.toggle('is-reconnectable', canRelink);
    b.setAttribute('aria-disabled', canRelink ? 'false' : 'true');
    b.tabIndex = canRelink ? 0 : -1;
    b.title = state === 'connected'
      ? (ps3mapiHealthProbeSupported
        ? 'The PS3MAPI HTTP version command responded. Destiny process and build are checked separately by memory tools.'
        : 'webMAN responded, but its PS3MAPI HTTP version command is unsupported. Memory tools check their own requests and build profile.')
      : canRelink ? 'Click to relink to the current or saved PS3 IP' : '';
  }

  function isDefiniteCommandRejection(error) {
    return /Console returned HTTP (?:400|401|403|404|405|501)\b/i.test(String(error?.message || ''));
  }

  async function quickReconnectFromBadge() {
    const badge = document.getElementById('status-badge');
    if (badge?.classList.contains('connected') || badge?.classList.contains('connecting')) return;
    recordConnectionEvent('manual-relink-started');
    const input = document.getElementById('ip');
    let target = String(ip || input?.value || '').trim();
    if (!target) { try { target = localStorage.getItem(SAVED_IP_KEY) || ''; } catch (_) {} }
    if (window.rtmFramebufferBusy && String(window.rtmFramebufferRecoveryIp || '') !== target) {
      toast('Restore the saved pause byte before changing consoles', 'err');
      return;
    }
    if (!/^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$/.test(target) || target.split('.').some(part => Number(part)>255)) { toast('Enter a valid PS3 IPv4 address', 'err'); return; }
    const generation = ++connectionGeneration;
    stopAutoReconnect(); stopHealthCheck(); consoleTransport.cancelAll(); resetVerifiedStates();
    ip = target; if (input) input.value = target;
    setBadge('connecting','RELINKING...');
    try {
      const mapiProbeSupported = await probePs3mapiAvailable(target,8000);
      if (generation !== connectionGeneration) return;
      ps3mapiHealthProbeSupported = mapiProbeSupported;
      await finishSafeAutoReconnect(target);
      recordConnectionEvent('manual-relink-restored');
    } catch (error) {
      if (generation !== connectionGeneration) return;
      setBadge('failed','FAILED');
      document.getElementById('btn-connect').style.display = '';
      document.getElementById('btn-connect').disabled = false;
      document.getElementById('btn-disconnect').style.display = 'none';
      document.getElementById('btn-shutdown').classList.remove('enabled');
      document.getElementById('btn-restart').classList.remove('enabled');
      log(`Relink failed: ${error.message}`,'err'); toast('Could not refresh connection','err');
      recordConnectionEvent('manual-relink-failed');
      ip = '';
    }
  }

  window.quickReconnectFromBadge = quickReconnectFromBadge;

  let connectionGeneration = 0;
  async function connect() {
    const recoveryTarget = String(document.getElementById('ip')?.value || '').trim();
    if (window.rtmFramebufferBusy && String(window.rtmFramebufferRecoveryIp || '') !== recoveryTarget) {
      toast('Reconnect to the console with the saved pause state first', 'err');
      return;
    }
    const generation = ++connectionGeneration;
    consoleTransport.cancelAll();
    resetVerifiedStates();
    stopHealthCheck();
    stopAutoReconnect();
    let val = document.getElementById('ip').value.trim();
    if (!val) { toast('Enter a PS3 IP address', 'err'); return; }
    if (!/^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$/.test(val) || val.split('.').some(part => Number(part) > 255)) { toast('Enter a valid IPv4 address', 'err'); return; }
    val = val.split('.').map(Number).join('.');
    document.getElementById('ip').value = val;
    ip = val;
    saveIp(val);
    recordConnectionEvent('connect-started');

    setBadge('connecting', 'LINKING...');
    document.getElementById('btn-connect').disabled = true;
    log(`Attempting connection → ${ip}`, '');

    try {
      // mode: no-cors means we can't read the response, but the request
      // reaches the PS3. If it aborts (timeout) the PS3 isn't reachable.
      await ps3fetch(`http://${ip}/beep.ps3?1`);
      await new Promise(r => setTimeout(r, 400));
      if (generation !== connectionGeneration) return;
      await doubleBeep();
      if (generation !== connectionGeneration) return;
      const mapiProbeSupported = await probePs3mapiAvailable(ip, 8000);
      if (generation !== connectionGeneration) return;
      ps3mapiHealthProbeSupported = mapiProbeSupported;
      setBadge('connected', ps3mapiHealthProbeSupported ? 'ONLINE' : 'WEBMAN ONLINE');
      document.getElementById('btn-connect').style.display = 'none';
      document.getElementById('btn-disconnect').style.display = '';
      document.getElementById('btn-shutdown').classList.add('enabled');
      document.getElementById('btn-restart').classList.add('enabled');
      log(ps3mapiHealthProbeSupported
        ? `PS3MAPI server ready → ${ip}`
        : `webMAN responded → ${ip}; PS3MAPI HTTP version probing is unsupported`, ps3mapiHealthProbeSupported ? 'ok' : '');
      if (typeof automationNotify === 'function') automationNotify('Connected', 'connect', {ip});
      recordConnectionEvent('connect-restored');
      toast(ps3mapiHealthProbeSupported
        ? 'Connected to PS3MAPI at ' + ip
        : 'webMAN is reachable; PS3MAPI status is unverified', ps3mapiHealthProbeSupported ? 'ok' : '');
      // Confirm a successful PS3 link with the webMAN HTTP popup.
      // This is a PS3/XMB notification, separate from the Destiny-internal RTM toast.
      ps3fetch(`http://${ip}/popup.ps3/${encodeURIComponent('Destiny RTM - By MVQ1303')}`)
        .then(() => log('Success popup sent → Destiny RTM - By MVQ1303', 'ok'))
        .catch(() => log('Success popup could not be sent', 'err'));
      // Start 30-second connection health-check pings.
      if (ps3mapiHealthProbeSupported) startHealthCheck();
      // Install player hook — required for gamespeed writes to persist.
      // Mirrors writePlayerHook() from AFKCrucible/Form1.cs (soulsAPI/C#).
      setTimeout(() => { if (generation === connectionGeneration) installPlayerHook().catch(error => log(`Player hook failed: ${error.message}`, 'err')); }, 800);

    } catch (e) {
      if (generation !== connectionGeneration) return;
      const timedOut = /timed out/i.test(e?.message || '');
      setBadge('failed', 'FAILED');
      document.getElementById('btn-connect').disabled = false;
      log(`Connection failed → ${ip} ${timedOut ? '(timed out)' : '(unreachable)'}`, 'err', 'connect');
      toast(timedOut ? 'PS3MAPI not responding' : 'Could not reach PS3MAPI', 'err');
      recordConnectionEvent('connect-failed');
      ip = '';
    }
  }

  async function disconnect() {
    if (window.rtmFramebufferBusy) { toast('Restore the saved pause byte before disconnecting', 'err'); return; }
    const generation = ++connectionGeneration;
    recordConnectionEvent('disconnect-requested');
    resetVerifiedStates();
    stopAutoReconnect();
    stopHealthCheck();
    consoleTransport.cancelAll();
    try {
      if (ip) await uninstallPlayerHook();
    } catch (error) {
      if (generation === connectionGeneration) log(`Disconnected; player hook cleanup could not be confirmed: ${error.message}`, 'err');
    } finally {
      if (generation !== connectionGeneration) return;
      consoleTransport.cancelAll();
      if (typeof automationNotify === 'function') automationNotify('Disconnected', 'connect', {ip});
      ip = '';
      setBadge('', 'OFFLINE');
      document.getElementById('btn-connect').style.display = '';
      document.getElementById('btn-connect').disabled = false;
      document.getElementById('btn-disconnect').style.display = 'none';
      document.getElementById('btn-shutdown').classList.remove('enabled');
      document.getElementById('btn-restart').classList.remove('enabled');
      log('Disconnected', '');
      recordConnectionEvent('disconnect-finished');
      toast('Disconnected', '');
    }
  }

  // ── Reachability probe / safe Auto Reconnect ─────────────────────────────
  const AUTO_RECONNECT_KEY = 'rtm_auto_reconnect_safe_v1';
  let autoReconnectEnabled = localStorage.getItem(AUTO_RECONNECT_KEY) === '1';
  recordConnectionEvent(autoReconnectEnabled ? 'auto-reconnect-pref-enabled' : 'auto-reconnect-pref-disabled');
  let _autoReconnectTimer = null;
  let _autoReconnectActive = false;
  let _autoReconnectAttempts = 0;

  const AUTO_RECONNECT_INTERVAL = 15000;
  const AUTO_RECONNECT_PROBE_TIMEOUT = 8000;
  const AUTO_RECONNECT_MAX_ATTEMPTS = 12; // 3 minutes

  function applyAutoReconnectSettingUI() {
    document.getElementById('auto-reconnect-on')?.classList.toggle('active', autoReconnectEnabled);
    document.getElementById('auto-reconnect-off')?.classList.toggle('active', !autoReconnectEnabled);
  }

  function setAutoReconnect(enabled) {
    autoReconnectEnabled = !!enabled;
    recordConnectionEvent(autoReconnectEnabled ? 'auto-reconnect-enabled' : 'auto-reconnect-disabled');
    localStorage.setItem(AUTO_RECONNECT_KEY, autoReconnectEnabled ? '1' : '0');
    applyAutoReconnectSettingUI();

    if (!autoReconnectEnabled) {
      const wasRelinking = _autoReconnectActive;
      stopAutoReconnect(false);
      if (wasRelinking) {
        setBadge('failed', 'NO SIGNAL');
        document.getElementById('btn-connect').style.display = '';
        document.getElementById('btn-connect').disabled = false;
        document.getElementById('btn-disconnect').style.display = 'none';
        document.getElementById('btn-shutdown').classList.remove('enabled');
        document.getElementById('btn-restart').classList.remove('enabled');
      }
      toast('Auto reconnect OFF', '');
    } else {
      toast('Auto reconnect ON', 'ok');
    }
  }

  window.setAutoReconnect = setAutoReconnect;
  document.addEventListener('DOMContentLoaded', applyAutoReconnectSettingUI);

  async function probePs3mapiAvailable(targetIp, timeoutMs = 8000, requestOptions = {}) {
    let response;
    try {
      response = await consoleTransport.request(`http://${targetIp}/ps3mapi.ps3?SERVER%20GETVERSION`, timeoutMs, requestOptions);
    } catch (error) {
      if (/Console returned HTTP (?:400|401|403|404|405|501|502)\b/i.test(String(error?.message || ''))) return false;
      throw error;
    }
    let payload;
    try { payload = await response.json(); }
    catch (_) { return false; }
    const version = payload?.response;
    const validNumber = typeof version === 'number' && Number.isSafeInteger(version) && version > 0;
    const validHex = typeof version === 'string' && /^0x[0-9a-f]+$/i.test(version);
    const numericVersion = validNumber ? version : validHex ? Number.parseInt(version.slice(2), 16) : 0;
    if (Number.isFinite(numericVersion) && numericVersion > 0) return true;
    const status = typeof payload?.status === 'string' ? payload.status : '';
    if ([400, 401, 403, 404, 405, 501, 502].includes(payload?.code) || /not implemented|unknown command|unsupported/i.test(status)) return false;
    throw new Error(`PS3MAPI HTTP version response is unavailable${status ? ` (${status})` : ''}`);
  }

  function stopAutoReconnect(resetAttempts = true) {
    if (_autoReconnectTimer !== null) {
      clearInterval(_autoReconnectTimer);
      _autoReconnectTimer = null;
    }
    _autoReconnectActive = false;
    if (resetAttempts) _autoReconnectAttempts = 0;
  }

  async function finishSafeAutoReconnect(targetIp) {
    stopAutoReconnect();
    recordConnectionEvent('relink-restored');
    ip = targetIp;

    const input = document.getElementById('ip');
    if (input) input.value = targetIp;
    saveIp(targetIp);

    setBadge('connected', ps3mapiHealthProbeSupported ? 'ONLINE' : 'WEBMAN ONLINE');
    document.getElementById('btn-connect').style.display = 'none';
    document.getElementById('btn-connect').disabled = false;
    document.getElementById('btn-disconnect').style.display = '';
    document.getElementById('btn-shutdown').classList.add('enabled');
    document.getElementById('btn-restart').classList.add('enabled');

    // Deliberately do NOT reinstall the game hook during a quick reconnect.
    // This reconnect path verifies the PS3MAPI server without reinstalling game hooks.
    log(ps3mapiHealthProbeSupported
      ? `PS3MAPI server restored → ${targetIp}`
      : `webMAN responded → ${targetIp}; PS3MAPI HTTP version probing is unsupported`, ps3mapiHealthProbeSupported ? 'ok' : '');
    if (typeof automationNotify === 'function') automationNotify('Connection restored', 'restore', {ip:targetIp});
    toast(ps3mapiHealthProbeSupported ? 'PS3MAPI connection restored' : 'webMAN is reachable; PS3MAPI status is unverified', ps3mapiHealthProbeSupported ? 'ok' : '');
    if (ps3mapiHealthProbeSupported) startHealthCheck();
  }

  function beginSafeAutoReconnect(targetIp = ip, force = false) {
    if (window.rtmFramebufferBusy && String(window.rtmFramebufferRecoveryIp || '') !== String(targetIp || '')) return;
    if ((!autoReconnectEnabled && !force) || _autoReconnectActive || !targetIp) return;

    const generation = ++connectionGeneration;
    consoleTransport.cancelAll();
    resetVerifiedStates();
    let busy = false;
    let lastProbeError = '';
    _autoReconnectActive = true;
    _autoReconnectAttempts = 0;
    recordConnectionEvent(force ? 'restart-relink-loop-started' : 'auto-relink-loop-started');
    stopHealthCheck();

    setBadge('connecting', 'RECONNECTING…');
    log(`PS3MAPI connection lost — auto reconnect started → ${targetIp}`, 'err');

    const attempt = async () => {
      if ((!autoReconnectEnabled && !force) || !_autoReconnectActive || busy || generation !== connectionGeneration) return;
      busy = true;

      _autoReconnectAttempts++;
      recordConnectionEvent('auto-relink-attempt');
      setBadge('connecting', `RECONNECT ${_autoReconnectAttempts}/${AUTO_RECONNECT_MAX_ATTEMPTS}`);

      try {
        const mapiProbeSupported = await probePs3mapiAvailable(targetIp, AUTO_RECONNECT_PROBE_TIMEOUT);
        if (!_autoReconnectActive || generation !== connectionGeneration) return;
        ps3mapiHealthProbeSupported = mapiProbeSupported;
        await finishSafeAutoReconnect(targetIp);
      } catch (e) {
        if (!_autoReconnectActive || generation !== connectionGeneration) return;
        lastProbeError = String(e?.message || e);
        if (_autoReconnectAttempts === 1) log(`PS3MAPI relink probe failed: ${lastProbeError}`, '');
        if (_autoReconnectAttempts >= AUTO_RECONNECT_MAX_ATTEMPTS) {
          recordConnectionEvent('auto-relink-failed');
          stopAutoReconnect(false);
          setBadge('failed', 'RECONNECT FAILED');
          document.getElementById('btn-connect').style.display = '';
          document.getElementById('btn-connect').disabled = false;
          document.getElementById('btn-disconnect').style.display = 'none';
          document.getElementById('btn-shutdown').classList.remove('enabled');
          document.getElementById('btn-restart').classList.remove('enabled');
          log(`PS3MAPI relink timed out after 3 minutes: ${lastProbeError}`, 'err');
          toast('PS3MAPI relink timed out; see the connection log', 'err');
        }
      } finally { busy = false; }
    };

    attempt();
    _autoReconnectTimer = setInterval(attempt, AUTO_RECONNECT_INTERVAL);
  }

  // ── Connection health-check (every 30 s) ─────────────────────────────────
  // Uses the read-only PS3MAPI server-version command; it reads no Destiny memory.
  let _healthTimer = null;
  let _healthMissed = 0;
  const HEALTH_INTERVAL = 30000;
  const HEALTH_TIMEOUT = 8000;
  const HEALTH_MAX_MISS = 2;
  const HEALTH_QUIET_WINDOW = 5000;

  function startHealthCheck() {
    const generation = connectionGeneration;
    stopHealthCheck();
    _healthMissed = 0;

    _healthTimer = setInterval(async () => {
      if (!ip) { stopHealthCheck(); return; }
      const targetIp = ip;

      // Busy RTM operations share the same PS3MAPI transport. Defer a health
      // poll during or immediately after their requests so one slow response
      // cannot make an otherwise-live session look disconnected.
      if (consoleTransport.shouldDeferBackgroundProbe(HEALTH_QUIET_WINDOW)) return;

      try {
        const mapiProbeSupported = await probePs3mapiAvailable(targetIp, HEALTH_TIMEOUT, { background: true });
        if (ip !== targetIp || generation !== connectionGeneration) return;
        ps3mapiHealthProbeSupported = mapiProbeSupported;

        if (!ps3mapiHealthProbeSupported) {
          recordConnectionEvent('health-probe-unsupported');
          stopHealthCheck();
          setBadge('connected', 'WEBMAN ONLINE');
          log('PS3MAPI HTTP version probe is unsupported; health relinking is disabled to avoid a false offline state', '');
          return;
        }

        if (_healthMissed > 0) {
          _healthMissed = 0;
          setBadge('connected', 'ONLINE');
          log('PS3MAPI health check responded again', 'ok');
          if (typeof automationNotify === 'function') automationNotify('Connection restored', 'restore', {ip:targetIp});
          toast('PS3MAPI connection is responsive again', 'ok');
        }
      } catch (e) {
        if (ip !== targetIp || generation !== connectionGeneration) return;
        _healthMissed++;
        recordConnectionEvent(`health-probe-missed-${_healthMissed}`);

        if (_healthMissed >= HEALTH_MAX_MISS) {
          setBadge('failed', 'NO SIGNAL');
          log(`Health-check: PS3MAPI version query failed (${_healthMissed} missed probes): ${String(e?.message || e)}`, 'err');

          if (_healthMissed === HEALTH_MAX_MISS) {
            if (autoReconnectEnabled) {
              recordConnectionEvent('health-relink-started');
              beginSafeAutoReconnect(targetIp);
            } else {
              toast('PS3MAPI not responding — click status to relink', 'err');
            }
          }
        } else {
          log(`Health-check: PS3MAPI version query missed (1/${HEALTH_MAX_MISS}): ${String(e?.message || e)}`, '');
        }
      }
    }, HEALTH_INTERVAL);
  }

  function stopHealthCheck() {
    if (_healthTimer !== null) {
      clearInterval(_healthTimer);
      _healthTimer = null;
    }
    _healthMissed = 0;
  }

