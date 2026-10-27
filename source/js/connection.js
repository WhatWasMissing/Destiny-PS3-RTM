  async function ps3Shutdown() {
    if (window.rtmFramebufferBusy) { toast('Finish the framebuffer capture or restore the saved pause byte first', 'err'); return; }
    if (!ip) return;
    if (!await RtmDialogs.confirm('Shut down the PS3?')) return;
    const generation = connectionGeneration;
    log('Sending shutdown command…', '');
    try {
      await ps3fetch(`http://${ip}/shutdown.ps3mapi`);
      if (generation !== connectionGeneration) return;
      ++connectionGeneration; consoleTransport.cancelAll(); resetVerifiedStates();
      log('Shutdown request acknowledged', '');
      toast('PS3 shutting down…', 'ok');
      // Connection will drop — clean up the UI
      stopHealthCheck();
      stopAutoReconnect();
      ip = '';
      setBadge('', 'OFFLINE');
      document.getElementById('btn-connect').style.display = '';
      document.getElementById('btn-connect').disabled = false;
      document.getElementById('btn-disconnect').style.display = 'none';
      document.getElementById('btn-shutdown').classList.remove('enabled');
      document.getElementById('btn-restart').classList.remove('enabled');
    } catch(e) {
      log(`Shutdown failed: ${e.message}`, 'err');
      toast('Shutdown failed', 'err');
    }
  }

  async function ps3Restart() {
    if (window.rtmFramebufferBusy) { toast('Finish the framebuffer capture or restore the saved pause byte first', 'err'); return; }
    if (!ip || !await RtmDialogs.confirm('Restart the PS3?')) return;
    const target = ip;
    const generation = ++connectionGeneration;
    stopAutoReconnect(); stopHealthCheck(); consoleTransport.cancelAll(); resetVerifiedStates();
    setBadge('connecting','RESTART REQUEST');
    try {
      await consoleTransport.request(`http://${target}/restart.ps3mapi`,5000);
      log('Restart request acknowledged; waiting for console availability','');
    } catch (error) {
      log(`Restart response unavailable; restart is not confirmed: ${error.message}`,'err');
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
    beginSafeAutoReconnect(target);
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
  }

  async function quickReconnectFromBadge() {
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
      await probePS3Reachable(target,5000);
      if (generation !== connectionGeneration) return;
      await finishSafeAutoReconnect(target);
    } catch (error) {
      if (generation !== connectionGeneration) return;
      setBadge('failed','FAILED');
      document.getElementById('btn-connect').style.display = '';
      document.getElementById('btn-connect').disabled = false;
      document.getElementById('btn-disconnect').style.display = 'none';
      document.getElementById('btn-shutdown').classList.remove('enabled');
      document.getElementById('btn-restart').classList.remove('enabled');
      log(`Relink failed: ${error.message}`,'err'); toast('Could not refresh connection','err');
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
      setBadge('connected', 'ONLINE');
      document.getElementById('btn-connect').style.display = 'none';
      document.getElementById('btn-disconnect').style.display = '';
      document.getElementById('btn-shutdown').classList.add('enabled');
      document.getElementById('btn-restart').classList.add('enabled');
      log(`Connected → ${ip}`, 'ok');
      if (typeof automationNotify === 'function') automationNotify('Connected', 'connect', {ip});
      toast('Connected to ' + ip, 'ok');
      // Confirm a successful PS3 link with the webMAN HTTP popup.
      // This is a PS3/XMB notification, separate from the Destiny-internal RTM toast.
      ps3fetch(`http://${ip}/popup.ps3/${encodeURIComponent('Destiny RTM - By MVQ1303')}`)
        .then(() => log('Success popup sent → Destiny RTM - By MVQ1303', 'ok'))
        .catch(() => log('Success popup could not be sent', 'err'));
      // Start 30-second connection health-check pings.
      startHealthCheck();
      // Install player hook — required for gamespeed writes to persist.
      // Mirrors writePlayerHook() from AFKCrucible/Form1.cs (soulsAPI/C#).
      setTimeout(() => { if (generation === connectionGeneration) installPlayerHook().catch(error => log(`Player hook failed: ${error.message}`, 'err')); }, 800);

    } catch (e) {
      if (generation !== connectionGeneration) return;
      const timedOut = /timed out/i.test(e?.message || '');
      setBadge('failed', 'FAILED');
      document.getElementById('btn-connect').disabled = false;
      log(`Connection failed → ${ip} ${timedOut ? '(timed out)' : '(unreachable)'}`, 'err', 'connect');
      toast(timedOut ? 'PS3 not responding' : 'Could not reach PS3', 'err');
      ip = '';
    }
  }

  async function disconnect() {
    if (window.rtmFramebufferBusy) { toast('Restore the saved pause byte before disconnecting', 'err'); return; }
    const generation = ++connectionGeneration;
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
      toast('Disconnected', '');
    }
  }

  // ── Reachability probe / safe Auto Reconnect ─────────────────────────────
  const AUTO_RECONNECT_KEY = 'rtm_auto_reconnect_safe_v1';
  let autoReconnectEnabled = localStorage.getItem(AUTO_RECONNECT_KEY) !== '0';
  let _autoReconnectTimer = null;
  let _autoReconnectActive = false;
  let _autoReconnectAttempts = 0;

  const AUTO_RECONNECT_INTERVAL = 5000;
  const AUTO_RECONNECT_MAX_ATTEMPTS = 36; // 3 minutes

  function applyAutoReconnectSettingUI() {
    document.getElementById('auto-reconnect-on')?.classList.toggle('active', autoReconnectEnabled);
    document.getElementById('auto-reconnect-off')?.classList.toggle('active', !autoReconnectEnabled);
  }

  function setAutoReconnect(enabled) {
    autoReconnectEnabled = !!enabled;
    localStorage.setItem(AUTO_RECONNECT_KEY, autoReconnectEnabled ? '1' : '0');
    applyAutoReconnectSettingUI();

    if (!autoReconnectEnabled) {
      stopAutoReconnect(false);
      toast('Auto reconnect OFF', '');
    } else {
      toast('Auto reconnect ON', 'ok');
    }
  }

  window.setAutoReconnect = setAutoReconnect;
  document.addEventListener('DOMContentLoaded', applyAutoReconnectSettingUI);

  async function probePS3Reachable(targetIp, timeoutMs = 5000) {
    await consoleTransport.request(`http://${targetIp}/index.ps3?_=${Date.now()}`, timeoutMs);
    return true;
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
    ip = targetIp;

    const input = document.getElementById('ip');
    if (input) input.value = targetIp;
    saveIp(targetIp);

    setBadge('connected', 'ONLINE');
    document.getElementById('btn-connect').style.display = 'none';
    document.getElementById('btn-connect').disabled = false;
    document.getElementById('btn-disconnect').style.display = '';
    document.getElementById('btn-shutdown').classList.add('enabled');
    document.getElementById('btn-restart').classList.add('enabled');

    // Deliberately do NOT reinstall the game hook during a quick reconnect.
    // This reconnect path only restores the RTM's HTTP link.
    log(`Auto reconnect restored → ${targetIp}`, 'ok');
    if (typeof automationNotify === 'function') automationNotify('Connection restored', 'restore', {ip:targetIp});
    toast('PS3 connection restored', 'ok');
    startHealthCheck();
  }

  function beginSafeAutoReconnect(targetIp = ip) {
    if (window.rtmFramebufferBusy && String(window.rtmFramebufferRecoveryIp || '') !== String(targetIp || '')) return;
    if (!autoReconnectEnabled || _autoReconnectActive || !targetIp) return;

    const generation = ++connectionGeneration;
    consoleTransport.cancelAll();
    resetVerifiedStates();
    let busy = false;
    _autoReconnectActive = true;
    _autoReconnectAttempts = 0;
    stopHealthCheck();

    setBadge('connecting', 'RECONNECTING…');
    log(`Connection lost — auto reconnect started → ${targetIp}`, 'err');

    const attempt = async () => {
      if (!autoReconnectEnabled || !_autoReconnectActive || busy || generation !== connectionGeneration) return;
      busy = true;

      _autoReconnectAttempts++;
      setBadge('connecting', `RECONNECT ${_autoReconnectAttempts}/${AUTO_RECONNECT_MAX_ATTEMPTS}`);

      try {
        await probePS3Reachable(targetIp, 4000);
        if (!_autoReconnectActive || generation !== connectionGeneration) return;
        await finishSafeAutoReconnect(targetIp);
      } catch (e) {
        if (!_autoReconnectActive || generation !== connectionGeneration) return;
        if (_autoReconnectAttempts >= AUTO_RECONNECT_MAX_ATTEMPTS) {
          stopAutoReconnect(false);
          setBadge('failed', 'RECONNECT FAILED');
          document.getElementById('btn-connect').style.display = '';
          document.getElementById('btn-connect').disabled = false;
          document.getElementById('btn-disconnect').style.display = 'none';
          document.getElementById('btn-shutdown').classList.remove('enabled');
          document.getElementById('btn-restart').classList.remove('enabled');
          log('Auto reconnect timed out after 3 minutes', 'err');
          toast('Auto reconnect timed out', 'err');
        }
      } finally { busy = false; }
    };

    attempt();
    _autoReconnectTimer = setInterval(attempt, AUTO_RECONNECT_INTERVAL);
  }

  // ── Connection health-check (every 30 s) ─────────────────────────────────
  // Uses a read-free HTTP reachability probe. No getmem calls are made here.
  let _healthTimer = null;
  let _healthMissed = 0;
  const HEALTH_INTERVAL = 30000;
  const HEALTH_TIMEOUT = 5000;
  const HEALTH_MAX_MISS = 2;

  function startHealthCheck() {
    const generation = connectionGeneration;
    stopHealthCheck();
    _healthMissed = 0;

    _healthTimer = setInterval(async () => {
      if (!ip) { stopHealthCheck(); return; }
      const targetIp = ip;

      try {
        await probePS3Reachable(targetIp, HEALTH_TIMEOUT);
        if (ip !== targetIp || generation !== connectionGeneration) return;

        if (_healthMissed > 0) {
          _healthMissed = 0;
          setBadge('connected', 'ONLINE');
          log('Connection restored', 'ok');
          if (typeof automationNotify === 'function') automationNotify('Connection restored', 'restore', {ip:targetIp});
          toast('PS3 back online', 'ok');
        }
      } catch (e) {
        if (ip !== targetIp || generation !== connectionGeneration) return;
        _healthMissed++;

        if (_healthMissed >= HEALTH_MAX_MISS) {
          setBadge('failed', 'NO SIGNAL');
          log(`Health-check: PS3 not responding (${_healthMissed} missed probes)`, 'err');

          if (_healthMissed === HEALTH_MAX_MISS) {
            if (autoReconnectEnabled) {
              beginSafeAutoReconnect(targetIp);
            } else {
              toast('PS3 not responding — click status to reconnect', 'err');
            }
          }
        } else {
          log(`Health-check: probe missed (1/${HEALTH_MAX_MISS})`, '');
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

