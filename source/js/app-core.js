
  // Auto-detect PS3 IP when served from the PS3 web server
  const pageHost = String(location.hostname || '').toLowerCase();
  const isDesktopLocalHost = pageHost === 'localhost' || pageHost === '127.0.0.1' || pageHost === '::1';
  let ip = (!isDesktopLocalHost && pageHost !== '') ? location.hostname : '';

  // Destiny game process used by PS3MAPI memory operations.
  // Kept in one place so Ammo Editor / health checks / setmem use the same PID.
  const GAME_PID = "0x1010200";

  // ── Saved IP address ──────────────────────────────────────────────────────
  const SAVED_IP_KEY = 'rtm_saved_ip';

  function loadSavedIp() {
    let saved = '';
    try { saved = localStorage.getItem(SAVED_IP_KEY) || ''; } catch (e) { saved = ''; }
    const input = document.getElementById('ip');
    // Don't clobber an IP already auto-detected from the hostname.
    if (input && !ip && saved) input.value = saved;
  }

  function saveIp(val) {
    try { localStorage.setItem(SAVED_IP_KEY, val); } catch (e) {}
  }

  function forgetSavedIp() {
    try { localStorage.removeItem(SAVED_IP_KEY); } catch (e) {}
    const input = document.getElementById('ip');
    if (input) input.value = '';
    toast('Saved IP cleared', '');
  }

  document.addEventListener('DOMContentLoaded', loadSavedIp);

  // ── Global write delay (used by macros and batch operations) ─────────────
  const WRITE_DELAY = 150; // ms between sequential memory writes

  function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }




  // ── Reset all active cheats ───────────────────────────────────────────────
  async function resetAllCheats() {
    log('Resetting all active cheats…', '');
    const CHEAT_DEFS = [
      { id: 'tog-god-mode',        addr: '00444CA0', offVal: 'C0210358', label: 'Invincibility',         size: 4 },
      { id: 'tog-ohk',             addr: '43E626',   offVal: '3F80',     label: 'One-Hit Enemies',     size: 2 },
      { id: 'tog-ammo',            addr: '00310F94', offVal: '3F800000', label: 'Unlimited Ammo',    size: 4 },
      { id: 'tog-power',           addr: '02C36FC',  offVal: 'C3C30000', label: 'Infinite Power',  size: 4 },
      { id: 'tog-bullets',         addr: '51E7E0',   offVal: '3F800000', label: 'Freeze Projectiles',     size: 4 },
      { id: 'tog-instant-respawn', addr: '0071C348', offVal: '93BF0030', label: 'Instant Respawn',  size: 4 },
      { id: 'tog-rapid-fire',      addr: '307590',   offVal: '3F800000', label: 'Faster Fire Rate',       size: 4 },
      { id: 'tog-super-speed',     addr: '1A96C0',   offVal: '00',       label: 'Super Speed',      size: 1 },
      { id: 'tog-variable-buff',   addr: '5E614',    offVal: '3F800000', label: 'Power Boost',    size: 4 },
      { id: 'tog-unl-sparrow',     addr: '50A700',   offVal: 'D023002C', label: 'Unlimited Sparrow Boost',size: 4 },
    ];
    const FLOAT_DEFS = [
      { id: 'tog-no-collision',  addr: '14FC690', offFloat: 1, label: 'Walk Through Objects'  },
      { id: 'tog-no-gravity',    addr: '143AD08', offFloat: 1, label: 'Zero Gravity'    },
      { id: 'tog-no-floors',     addr: '1689470', offFloat: 1, label: 'Fall Through Floors'     },
      { id: 'tog-static-actors', addr: '2EDAC4',  offFloat: 1, label: 'Freeze Characters' },
    ];
    const MULTI_DEFS = [
      { id: 'tog-notarget',      label: 'Enemy Ignore',           pairs: [['18681C','3F800000'],['1891E4','3F800000']] },
      { id: 'tog-no-recoil',     label: 'Steady Aim',           pairs: [['5056DC','3F800000'],['505D24','3F800000'],['30DC44','3F800000']] },
      { id: 'tog-warpriest',     label: 'Warpriest Immunity',    pairs: [['444CA4','C0434DB0'],['1897420','3F800000']] },
      { id: 'tog-unl-abilities', label: 'Unlimited Abilities', pairs: [['2C1DB4','D03B019C'],['CCB9E8','3063FFFF'],['2C36FC','C3C30000']] },
    ];
    const BYTES_DEFS = [
      { id: 'tog-no-pushbacks', addr: '1389760', offHex: '3F8000003F8000003F800000', label: 'No Knockback' },
    ];
    for (const d of CHEAT_DEFS) {
      const el = document.getElementById(d.id);
      if (!el || !el.classList.contains('active-cheat')) continue;
      const sizeParam = d.size !== 4 ? `&size=${d.size}` : '';
      try { await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${d.addr}&val=${d.offVal}${sizeParam}`); } catch(e) {}
      await henDelay();
      cheatState[d.label] = false;
      el.classList.remove('active-cheat');
    }
    for (const d of FLOAT_DEFS) {
      const el = document.getElementById(d.id);
      if (!el || !el.classList.contains('active-cheat')) continue;
      try { await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${d.addr}&val=${floatToBEHex(d.offFloat)}`); } catch(e) {}
      await henDelay();
      cheatState[d.label] = false;
      el.classList.remove('active-cheat');
    }
    for (const d of MULTI_DEFS) {
      const el = document.getElementById(d.id);
      if (!el || !el.classList.contains('active-cheat')) continue;
      for (const [addr, val] of d.pairs) {
        try { await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${val}`); } catch(e) {}
        await henDelay();
      }
      cheatState[d.label] = false;
      el.classList.remove('active-cheat');
    }
    for (const d of BYTES_DEFS) {
      const el = document.getElementById(d.id);
      if (!el || !el.classList.contains('active-cheat')) continue;
      const chunks = d.offHex.match(/.{8}/g) || [];
      const base = parseInt(d.addr, 16);
      for (let i = 0; i < chunks.length; i++) {
        const addr = (base + i * 4).toString(16).toUpperCase();
        try { await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${chunks[i]}`); } catch(e) {}
        await henDelay();
      }
      cheatState[d.label] = false;
      el.classList.remove('active-cheat');
    }
    // Reset Deathsinger's Power if active
    if (cheatState["Deathsinger's Power"]) {
      try { await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=1C7B58&val=${dsDetectedOff || '38C60001'}`); } catch(e) {}
      await henDelay();
      cheatState["Deathsinger's Power"] = false;
      const dsEl = document.getElementById('tog-deathsinger');
      if (dsEl) dsEl.classList.remove('active-cheat');
    }
    // Clear server cheats visual state (PS3 memory will be reset by reboot anyway)
    ['srv-void','srv-ropk','srv-off'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.classList.remove('active-cheat');
    });
    srvMode = 'off';
    const ropkCtrl = document.getElementById('ropk-controls');
    if (ropkCtrl) ropkCtrl.style.display = 'none';
    log('All cheats reset ', 'ok');
  }

  function ts() {
    return new Date().toTimeString().slice(0,8);
  }

  function log(msg, type = '') {
    const el = document.getElementById('log');
    const entry = document.createElement('div');
    entry.className = 'log-entry ' + type;
    entry.innerHTML = `<span class="ts">${ts()}</span>${msg}`;
    el.appendChild(entry);
    el.scrollTop = el.scrollHeight;

    const dock = document.getElementById('activity-log-dock-body');
    if (dock) {
      const empty = dock.querySelector('.dock-log-empty');
      if (empty) empty.remove();
      const d = document.createElement('div');
      d.className = 'dock-log-entry ' + type;
      d.innerHTML = `<span class="ts">${ts()}</span>${msg}`;
      dock.appendChild(d);
      dock.scrollTop = dock.scrollHeight;
    }

    if (type === 'err' && typeof automationNotify === 'function') {
    }
  }

  function toast(msg, type = '') {
    const t = document.getElementById('toast');
    t.textContent = msg;
    t.className = 'show ' + type;
    clearTimeout(t._tid);
    t._tid = setTimeout(() => t.className = '', 2200);
  }

  // XHR-based sender — works when the HTML file is opened directly from
  // phone storage (file://) with no local server needed. PS3MAPI does not
  // return CORS headers so we cannot read the response body for WRITE ops,
  // but the PS3 still receives and acts on every request.
  // Write a value to PS3 memory, scoped to the game process.
  // All setmem calls should go through this to ensure proc=${GAME_PID} is included.
  function ps3setmem(addr, val, timeoutMs = 4000) {
    const addrHex = typeof addr === 'number'
      ? addr.toString(16).toUpperCase().padStart(8, '0')
      : addr.toString().toUpperCase().replace(/^0X/i, '').padStart(8, '0');
    const url = `http://${ip}/setmem.ps3mapi?proc=${GAME_PID}&addr=${addrHex}&val=${val}`;
    console.log(`Set memory at ${addrHex} to ${val}`);
    return ps3fetch(url, timeoutMs);
  }

  // NOTE: For READ ops (getmem), use ps3fetchRead() which throws on empty body.
  const consoleTransport = RtmTransport.createTransport({ processId: GAME_PID, fetch: RtmHost.fetch });
  async function ps3fetch(url, timeoutMs = 8000) {
    return consoleTransport.request(url, timeoutMs);
  }

  // For getmem — must be able to READ the response. Rejects if body is empty
  // (indicates CORS block or network error). Must serve the HTML from the PS3
  // web server (http://ps3ip/) for this to work; file:// origin blocks reads.
  async function ps3fetchRead(url, timeoutMs = 8000) {
    const response = await consoleTransport.request(url, timeoutMs);
    const body = await response.text();
    if (!body.trim()) throw new Error('Console returned an empty memory response');
    return { responseText: body };
  }

  function beep(type) {
    return ps3fetch(`http://${ip}/beep.ps3?${type}`).catch(() => {});
  }

  async function ps3Shutdown() {
    if (!ip) return;
    if (!confirm('Shut down the PS3?')) return;
    log('Sending shutdown command…', '');
    try {
      await ps3fetch(`http://${ip}/shutdown.ps3mapi`);
      log('Shutdown command sent', 'ok');
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
    if (!ip) return;
    if (!confirm('Restart the PS3?')) return;
    const targetIp = ip;
    log('Sending restart command…', '');
    try {
      await resetAllCheats();
    } catch(e) {}
    try {
      await ps3fetch(`http://${targetIp}/restart.ps3mapi`);
    } catch(e) {
      // The PS3 cuts the connection as it restarts — a network error here is
      // expected and not a failure. Only bail if ip was never set.
    }
    log('Restart command sent — waiting for PS3 to come back…', 'ok');
    toast('Restarting… will reconnect automatically', 'ok');

    // Clear connection state while the PS3 is down
    stopHealthCheck();
    ip = '';
    setBadge('connecting', 'REBOOTING…');
    document.getElementById('btn-connect').style.display = 'none';
    document.getElementById('btn-disconnect').style.display = 'none';
    document.getElementById('btn-shutdown').classList.remove('enabled');
    document.getElementById('btn-restart').classList.remove('enabled');

    // Poll every 5 s for up to 3 minutes. The PS3 typically takes 40-80 s
    // to fully boot and have PS3MAPI listening again.
    const POLL_INTERVAL = 5000;
    const MAX_ATTEMPTS  = 36; // 3 min
    let attempts = 0;

    // Give the PS3 a head-start before we start pinging so we don't
    // immediately get a stale success from the dying connection.
    await new Promise(r => setTimeout(r, 15000));

    const poll = setInterval(async () => {
      attempts++;
      log(`Reconnect attempt ${attempts}/${MAX_ATTEMPTS} → ${targetIp}`, '');
      setBadge('connecting', `REBOOTING… ${attempts}`);
      try {
        await ps3fetch(`http://${targetIp}/beep.ps3?1`, 4000);
        // PS3 responded — reconnect
        clearInterval(poll);
        ip = targetIp;
        await new Promise(r => setTimeout(r, 400));
        await doubleBeep();
        setTimeout(() => enableRPC(), 500);
        setBadge('connected', 'ONLINE');
        document.getElementById('btn-connect').style.display = 'none';
        document.getElementById('btn-disconnect').style.display = '';
        document.getElementById('btn-shutdown').classList.add('enabled');
        document.getElementById('btn-restart').classList.add('enabled');
        log(`Reconnected → ${targetIp}`, 'ok');
        toast('Reconnected after restart', 'ok');
        startHealthCheck();

      } catch(e) {
        if (attempts >= MAX_ATTEMPTS) {
          clearInterval(poll);
          setBadge('failed', 'FAILED');
          document.getElementById('btn-connect').style.display = '';
          document.getElementById('btn-connect').disabled = false;
          log('Auto-reconnect timed out — reconnect manually', 'err');
          toast('Reconnect timed out', 'err');
        }
        // else: still booting, keep polling
      }
    }, POLL_INTERVAL);
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
    stopAutoReconnect(false);
    const badge = document.getElementById('status-badge');
    if (badge?.classList.contains('connecting')) return;

    const input = document.getElementById('ip');
    let target = String(ip || input?.value || '').trim();

    if (!target) {
      try { target = String(localStorage.getItem(SAVED_IP_KEY) || '').trim(); } catch (e) {}
    }

    if (!target) {
      toast('No saved PS3 IP — enter one in Connection first', 'err');
      document.getElementById('ip')?.focus();
      return;
    }

    // Manual quick relink only. This does not reinstall RPC/player hooks
    // and does not perform any memory writes.
    stopHealthCheck();
    ip = target;
    if (input) input.value = target;
    saveIp(target);

    setBadge('connecting', 'RELINKING...');
    log(`Quick reconnect → ${target}`, '');

    try {
      await ps3fetch(`http://${target}/beep.ps3?1`);

      setBadge('connected', 'ONLINE');
      document.getElementById('btn-connect').style.display = 'none';
      document.getElementById('btn-connect').disabled = false;
      document.getElementById('btn-disconnect').style.display = '';
      document.getElementById('btn-shutdown').classList.add('enabled');
      document.getElementById('btn-restart').classList.add('enabled');

      startHealthCheck();
      log(`Quick reconnect successful → ${target}`, 'ok');
      toast('Connection refreshed', 'ok');
    } catch (e) {
      setBadge('failed', 'FAILED');
      document.getElementById('btn-connect').style.display = '';
      document.getElementById('btn-connect').disabled = false;
      document.getElementById('btn-disconnect').style.display = 'none';
      document.getElementById('btn-shutdown').classList.remove('enabled');
      document.getElementById('btn-restart').classList.remove('enabled');

      log(`Quick reconnect failed → ${target}`, 'err');
      toast(e?.name === 'AbortError' ? 'PS3 not responding' : 'Could not refresh connection', 'err');
      ip = '';
    }
  }

  window.quickReconnectFromBadge = quickReconnectFromBadge;

  let connectionGeneration = 0;
  async function connect() {
    const generation = ++connectionGeneration;
    consoleTransport.cancelAll();
    stopHealthCheck();
    stopAutoReconnect();
    const val = document.getElementById('ip').value.trim();
    if (!val) { toast('Enter a PS3 IP address', 'err'); return; }
    if (!/^(?:[0-9]{1,3}\.){3}[0-9]{1,3}$/.test(val) || val.split('.').some(part => Number(part) > 255)) { toast('Enter a valid IPv4 address', 'err'); return; }
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
      // Enable RPC once at connect time via iframe (avoids download popup).
      // Must be active before any Call() operations.
      setTimeout(() => { if (generation === connectionGeneration) enableRPC(); }, 500);
      setBadge('connected', 'ONLINE');
      document.getElementById('btn-connect').style.display = 'none';
      document.getElementById('btn-disconnect').style.display = '';
      document.getElementById('btn-shutdown').classList.add('enabled');
      document.getElementById('btn-restart').classList.add('enabled');
      log(`Connected → ${ip}`, 'ok');
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
      log(`Connection failed → ${ip} ${timedOut ? '(timed out)' : '(unreachable)'}`, 'err');
      toast(timedOut ? 'PS3 not responding' : 'Could not reach PS3', 'err');
      ip = '';
    }
  }

  async function disconnect() {
    ++connectionGeneration;
    stopAutoReconnect();
    stopHealthCheck();
    consoleTransport.cancelAll();
    try {
      if (ip) await uninstallPlayerHook();
    } catch (error) {
      log(`Disconnected; player hook cleanup could not be confirmed: ${error.message}`, 'err');
    } finally {
      consoleTransport.cancelAll();
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

    // Deliberately do NOT call enableRPC() or installPlayerHook() here.
    // This reconnect path only restores the RTM's HTTP link.
    log(`Auto reconnect restored → ${targetIp}`, 'ok');
    toast('PS3 connection restored', 'ok');
    startHealthCheck();
  }

  function beginSafeAutoReconnect(targetIp = ip) {
    if (!autoReconnectEnabled || _autoReconnectActive || !targetIp) return;

    _autoReconnectActive = true;
    _autoReconnectAttempts = 0;
    stopHealthCheck();

    setBadge('connecting', 'RECONNECTING…');
    log(`Connection lost — auto reconnect started → ${targetIp}`, 'err');

    const attempt = async () => {
      if (!autoReconnectEnabled || !_autoReconnectActive) return;

      _autoReconnectAttempts++;
      setBadge('connecting', `RECONNECT ${_autoReconnectAttempts}/${AUTO_RECONNECT_MAX_ATTEMPTS}`);

      try {
        await probePS3Reachable(targetIp, 4000);
        await finishSafeAutoReconnect(targetIp);
      } catch (e) {
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
      }
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
    stopHealthCheck();
    _healthMissed = 0;

    _healthTimer = setInterval(async () => {
      if (!ip) { stopHealthCheck(); return; }
      const targetIp = ip;

      try {
        await probePS3Reachable(targetIp, HEALTH_TIMEOUT);
        if (ip !== targetIp) return;

        if (_healthMissed > 0) {
          _healthMissed = 0;
          setBadge('connected', 'ONLINE');
          log('Connection restored', 'ok');
          toast('PS3 back online', 'ok');
        }
      } catch (e) {
        if (ip !== targetIp) return;
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

  // Convert a JS string to big-endian UTF-16 hex — required for WriteUtf16String
  // PS3 is big-endian so each UTF-16 code unit needs its bytes swapped
  function toUtf16BEHex(str) {
    let hex = '';
    for (let i = 0; i < str.length; i++) {
      const code = str.charCodeAt(i);
      const hi = (code >> 8) & 0xFF;
      const lo = code & 0xFF;
      hex += hi.toString(16).padStart(2,'0') + lo.toString(16).padStart(2,'0');
    }
    // Null terminate and pad to 8-byte boundary for safe 4-byte writes
    hex += '0000';
    while (hex.length % 8 !== 0) hex += '00';
    return hex;
  }

  // Write a UTF-16 BE string to a PS3 memory address in 4-byte chunks
  async function writeUtf16(baseAddr, str) {
    const hex = toUtf16BEHex(str);
    const chunks = hex.match(/.{8}/g);
    for (let i = 0; i < chunks.length; i++) {
      const addr = (parseInt(baseAddr, 16) + i * 4).toString(16).toUpperCase();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${chunks[i]}`);
    }
  }

  // Call a PS3 function via PS3MAPI.
  // addr is a HEX string (e.g. '750540').
  // args must be DECIMAL uint strings — PS3MAPI call.ps3mapi params are decimal, not hex.
  // Use hexToDecParam() to convert hex addresses to the required decimal format.
  function hexToDecParam(hexStr) {
    return parseInt(hexStr.replace(/^0x/i, ''), 16).toString(10);
  }
  function ps3call(addrHex, ...decimalArgs) {
    // IMPORTANT: webMAN MOD's documented HTTP PS3MAPI command list exposes
    // MEMORY GET/SET, PS3 NOTIFY, PAGEALLOCATE, THREAD CREATE, etc., but does
    // not document a generic /call.ps3mapi endpoint. The original soulsAPI
    // Extension.Call() is therefore not proven to be directly reproducible
    // from browser HTTP. Keep this isolated as an experimental backend.
    let url = `http://${ip}/call.ps3mapi?addr=${addrHex}`;
    decimalArgs.forEach((a, i) => { url += `&p${i+1}=${encodeURIComponent(a)}`; });
    return ps3fetch(url);
  }

  // Original Destiny RTM toast sequence recovered from Form1.cs:
  // EnableRPC(4882496)
  // Call(7669056, 268632064)
  // WriteUtf16String(268632576, title)
  // WriteUtf16String(268633088, body)
  // Call(7636756, 28508392, 28507992, 268632064, 7, 0)
  //
  // This is retained as an EXPERIMENTAL browser backend. The source proves
  // the soulsAPI sequence, but webMAN's public HTTP documentation does not
  // prove a generic call.ps3mapi route. We therefore never claim success
  // merely because a no-cors HTTP request was dispatched.
  // ── Shared PS3MAPI memory-read helpers ──────────────────────────────────
  // Teleport depends on these helpers to resolve the live player pointer at
  // 0x0190EBB0. They were unintentionally removed with the Player Size code.
  function extractMemoryHexFromPS3Page(page, addrHex, len = 4) {
    page = String(page || '');
    addrHex = String(addrHex || '').replace(/^0x/i, '').toUpperCase().padStart(8, '0');
    const wantedChars = Math.max(1, Number(len) || 4) * 2;

    // webMAN/PS3MAPI commonly exposes raw bytes in a hidden output textarea.
    const textarea = page.match(/<textarea[^>]*id=["']output["'][^>]*>([0-9A-Fa-f\s]*)<\/textarea>/i);
    if (textarea) {
      const raw = textarea[1].replace(/[^0-9A-Fa-f]/g, '').toUpperCase();
      if (raw.length >= wantedChars) return raw.slice(0, wantedChars);
    }

    // Fallback: parse the human-readable dump beginning at the requested row.
    // Example: 0190EBB0  12 34 56 78 ...
    const escaped = addrHex.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    const row = page.match(new RegExp(`${escaped}\\s+((?:[0-9A-Fa-f]{2}\\s+){1,32})`, 'i'));
    if (row) {
      const raw = row[1].replace(/[^0-9A-Fa-f]/g, '').toUpperCase();
      if (raw.length >= Math.min(wantedChars, 8)) return raw.slice(0, wantedChars);
    }

    // Last fallback for standard 4-byte PS3MAPI memory rows.
    const four = page.match(
      new RegExp(`${escaped}\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})\\s+([0-9A-Fa-f]{2})`, 'i')
    );
    if (four) return (four[1] + four[2] + four[3] + four[4]).toUpperCase();

    return null;
  }

  async function ps3getmem(addr, len = 4) {
    if (!ip) throw new Error('Not connected');

    const numericAddr = Number(addr) >>> 0;
    if (!Number.isFinite(numericAddr) || numericAddr <= 0) {
      throw new Error(`Invalid memory address: ${addr}`);
    }

    const addrHex = numericAddr.toString(16).padStart(8, '0').toUpperCase();
    const proc = (typeof GAME_PID !== 'undefined' && GAME_PID) ? GAME_PID : '0x1010200';
    const url = `http://${ip}/getmem.ps3mapi?proc=${proc}&addr=${addrHex}&len=${Number(len) || 4}&_=${Date.now()}`;

    let page = '';
    let firstError = null;

    // Older working Teleport builds used fetch(), so keep that as the primary
    // transport. If it fails, try the RTM's readable-XHR path as a fallback.
    try {
      const response = await consoleTransport.request(url, 8000);
      page = await response.text();
    } catch (e) {
      firstError = e;
    }

    if (!page) {
      try {
        const xhr = await ps3fetchRead(url, 8000);
        page = xhr.responseText || '';
      } catch (e) {
        const msg = e?.message || firstError?.message || String(e);
        throw new Error(`getmem failed at 0x${addrHex}: ${msg}`);
      }
    }

    const hex = extractMemoryHexFromPS3Page(page, addrHex, len);
    if (!hex) {
      throw new Error(`Could not parse getmem response at 0x${addrHex}`);
    }

    return hex;
  }

  async function ps3readU32(addr) {
    const hex = await ps3getmem(addr, 4);
    if (!hex || hex.length < 8) throw new Error(`Short read at 0x${(Number(addr) >>> 0).toString(16).toUpperCase()}`);
    return parseInt(hex.slice(0, 8), 16) >>> 0;
  }

  async function ps3readString(addr, maxBytes = 64) {
    const hex = await ps3getmem(addr, maxBytes);
    let result = '';
    for (let i = 0; i + 1 < hex.length && result.length < maxBytes; i += 2) {
      const b = parseInt(hex.slice(i, i + 2), 16);
      if (!Number.isFinite(b) || b === 0) break;
      result += String.fromCharCode(b);
    }
    return result;
  }

  // ── Teleport (gemini-code.js version) ───────────────────────────────────
  // Ported directly from gemini-code-1785661879784.js, adapted to reuse this
  // file's ip / ps3fetch / toast / log helpers, and wired to the existing
  // button hooks (tpReadCoords, tpGo, tpSaveCurrent, tpAboveYou, readPlayerPos,
  // tpLoadSlot/tpDeleteSlot/tpRenderSlots) so the HTML doesn't need edits.
  //
  // NOTE vs. the previous implementation: this version does NOT write the
  // 0x1B8 "activation" float, and re-resolves the 0x190EBB0 pointer on every
  // call instead of caching it. If teleports silently fail to commit in-game,
  // that missing activation write is almost certainly why — say the word and
  // I'll add it back in.

  function floatToBEHex(val) {
    const arr = new ArrayBuffer(4);
    const dv = new DataView(arr);
    dv.setFloat32(0, val, false); // false = big endian
    return dv.getUint32(0, false).toString(16).padStart(8, '0').toUpperCase();
  }

  function beHexToFloat(hexStr) {
    const intVal = parseInt(hexStr, 16);
    const arr = new ArrayBuffer(4);
    const dv = new DataView(arr);
    dv.setUint32(0, intVal, false);
    return dv.getFloat32(0, false);
  }

  // Memory read helper — hits getmem.ps3mapi directly (mirrors gemini-code.js).
  // Delegates to ps3getmem, which already handles the real PS3MAPI response
  // format correctly: getmem.ps3mapi returns an HTML page with the address
  // followed by 4 space-separated hex byte pairs, NOT raw hex text. An
  // earlier version of this function did `text.trim()` on the raw HTML and
  // treated the whole blob as hex, which always failed to parse (that's why
  // Read said "invalid pointer" and Teleport silently no-op'd on garbage
  // addresses without erroring).
  // Robust memory read helper. PS3MAPI can occasionally return an empty
  // or transiently invalid response while the game/HEN is busy. Retry the
  // read a few times before reporting failure.
  async function readMemHex(addr, size = 4, retries = 4, delayMs = 100) {
    const numericAddr = parseInt(addr, 16);

    if (!Number.isFinite(numericAddr) || numericAddr <= 0) {
      log("Read mem error: invalid address " + addr, "err");
      return null;
    }

    for (let attempt = 1; attempt <= retries; attempt++) {
      try {
        log(`Reading ${addr} (attempt ${attempt}/${retries})`, "");

        const result = await ps3getmem(numericAddr, size);

        // A valid pointer read must contain a non-empty hex value.
        if (result && /^[0-9a-fA-F]{8}$/.test(String(result).trim())) {
          return String(result).trim().toUpperCase();
        }

        log(`Pointer read returned invalid/empty data (attempt ${attempt}/${retries})`, "err");
      } catch (e) {
        console.error("Read mem error:", e);
        log(`Read mem error (attempt ${attempt}/${retries}): ${e.message}`, "err");
      }

      if (attempt < retries) {
        await sleep(delayMs);
      }
    }

    log("Read mem error: all pointer read attempts failed for " + addr, "err");
    return null;
  }

  // Restored so the pre-existing readPlayerPos() (below, used by the
  // "Ptr Pos" button) keeps working — it wasn't part of gemini-code.js but
  // is still referenced elsewhere in this file.
  async function ps3readFloat(addr) {
    const hex = await ps3getmem(addr, 4);
    const intValue = parseInt(hex, 16);
    const buffer = new ArrayBuffer(4);
    const view = new DataView(buffer);
    view.setUint32(0, intValue, false);
    return view.getFloat32(0, false);
  }

  const TELEPORTS_KEY = 'rtm_saved_teleports';
  const TELEPORT_FOLDERS_KEY = 'rtm_teleport_folders';
  let savedTeleports = [];
  let teleportFolders = [];
  try { savedTeleports = JSON.parse(localStorage.getItem(TELEPORTS_KEY) || '[]'); } catch (e) { savedTeleports = []; }
  try { teleportFolders = JSON.parse(localStorage.getItem(TELEPORT_FOLDERS_KEY) || '[]'); } catch (e) { teleportFolders = []; }
  if (!Array.isArray(teleportFolders)) teleportFolders = [];
  if (!teleportFolders.length) teleportFolders = [{ id:'unsorted', name:'Unsorted', system:true, open:true }];
  if (!teleportFolders.some(f => f.id === 'unsorted')) teleportFolders.unshift({ id:'unsorted', name:'Unsorted', system:true, open:true });
  savedTeleports = Array.isArray(savedTeleports) ? savedTeleports.map(p => ({...p, folderId:p.folderId || 'unsorted'})) : [];

  function saveTeleportsToStorage() {
    try { localStorage.setItem(TELEPORTS_KEY, JSON.stringify(savedTeleports)); } catch (e) {}
    try { localStorage.setItem(TELEPORT_FOLDERS_KEY, JSON.stringify(teleportFolders)); } catch (e) {}
  }

  function tpEscape(v) {
    return String(v ?? '').replace(/[&<>\"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',"'":'&#39;'}[c]));
  }
  function tpNewFolder() {
    const name = prompt('Folder name:');
    if (!name || !name.trim()) return;
    teleportFolders.push({ id:'folder_' + Date.now().toString(36), name:name.trim(), open:true });
    saveTeleportsToStorage(); tpRenderSlots();
  }
  function tpRenameFolder(id) {
    const folder = teleportFolders.find(f => f.id === id);
    if (!folder || folder.system) return;
    const name = prompt('Rename folder:', folder.name);
    if (!name || !name.trim()) return;
    folder.name = name.trim(); saveTeleportsToStorage(); tpRenderSlots();
  }
  function tpDeleteFolder(id) {
    const folder = teleportFolders.find(f => f.id === id);
    if (!folder || folder.system) return;
    if (!confirm(`Delete folder \"${folder.name}\"? Locations will be moved to Unsorted.`)) return;
    tpRemember();
    savedTeleports.forEach(p => { if (p.folderId === id) p.folderId = 'unsorted'; });
    teleportFolders = teleportFolders.filter(f => f.id !== id);
    saveTeleportsToStorage(); tpRenderSlots();
  }
  function tpToggleFolder(id) {
    const folder = teleportFolders.find(f => f.id === id); if (!folder) return;
    folder.open = !folder.open; saveTeleportsToStorage(); tpRenderSlots();
  }
  function tpMoveSlot(idx) {
    const pos = savedTeleports[idx]; if (!pos) return;
    const options = teleportFolders.map((f,i) => `${i+1}. ${f.name}`).join('\n');
    const answer = prompt(`Move \"${pos.name}\" to folder:\n${options}\n\nEnter folder number:`);
    const n = Number(answer);
    if (!Number.isInteger(n) || n < 1 || n > teleportFolders.length) return;
    pos.folderId = teleportFolders[n-1].id; saveTeleportsToStorage(); tpRenderSlots();
  }

  // Note: this file already has a separate readPlayerPos() (used by the
  // "Ptr Pos" button, which only toasts the coords). tpReadCoords below has
  // its own body — same logic as gemini-code.js's readPlayerPos() — so it
  // doesn't collide with that existing function and correctly populates the
  // tp-x/tp-y/tp-z fields for the "Read" button.
  async function tpReadCoords() {
    if (!ip) { toast("Not connected", "err"); return; }
    try {
      // 1. Read base pointer from 0x190EBB0
      const ptrHex = await readMemHex("190EBB0", 4);
      if (!ptrHex) {
        toast("Failed to read player pointer", "err");
        log("Teleport Read: 0x0190EBB0 returned no readable pointer. Check Activity Log for getmem errors.", "err");
        return;
      }

      const baseAddr = parseInt(ptrHex, 16);
      if (!baseAddr) { toast("Invalid pointer address", "err"); return; }

      // 2. Read X, Y, Z offsets (+0x140, +0x144, +0x148)
      const xHex = await readMemHex((baseAddr + 0x140).toString(16), 4);
      const yHex = await readMemHex((baseAddr + 0x144).toString(16), 4);
      const zHex = await readMemHex((baseAddr + 0x148).toString(16), 4);

      if (xHex && yHex && zHex) {
        document.getElementById('tp-x').value = beHexToFloat(xHex).toFixed(3);
        document.getElementById('tp-y').value = beHexToFloat(yHex).toFixed(3);
        document.getElementById('tp-z').value = beHexToFloat(zHex).toFixed(3);
        toast("Position read successfully", "ok");
        log("Read player coordinates via pointer", "ok");
      }
    } catch (e) {
      toast("Error reading position", "err");
      log("Read pos error: " + e.message, "err");
    }
  }

  function toggleDirectMode() {
    const toggle = document.getElementById('directAddressToggle');
    const container = document.getElementById('directAddressContainer');
    if (!toggle || !container) return;

    container.classList.toggle('visible', toggle.checked);

    if (toggle.checked) {
      toast('Direct memory test mode enabled', '');
      log('Direct memory address test mode enabled', '');
    } else {
      toast('Pointer teleport mode restored', 'ok');
      log('Pointer-based teleport mode restored', 'ok');
    }
  }

  async function executeTeleport() {
    if (!ip) {
      toast("Not connected", "err");
      return;
    }

    const directToggle = document.getElementById('directAddressToggle');
    if (!directToggle || !directToggle.checked) {
      await tpGo();
      return;
    }

    const rawAddr = document.getElementById('directXAddr').value.trim();
    const addressText = rawAddr.replace(/^0x/i, '');

    if (!/^[0-9a-fA-F]{1,8}$/.test(addressText)) {
      toast("Enter a valid X address", "err");
      log("Direct teleport failed: invalid X address", "err");
      return;
    }

    const x = parseFloat(document.getElementById('tp-x').value);
    const y = parseFloat(document.getElementById('tp-y').value);
    const z = parseFloat(document.getElementById('tp-z').value);

    if (isNaN(x) || isNaN(y) || isNaN(z)) {
      toast("Enter valid coordinates", "err");
      return;
    }

    try {
      const xAddr = parseInt(addressText, 16);
      const yAddr = xAddr + 0x4;
      const zAddr = xAddr + 0x8;

      const combinedHex =
        floatToBEHex(x) +
        floatToBEHex(y) +
        floatToBEHex(z);

      // X, Y and Z are contiguous 4-byte floats. Write all 12 bytes at X.
      await ps3fetch(
        `http://${ip}/setmem.ps3mapi?addr=${xAddr.toString(16).padStart(8, '0').toUpperCase()}&val=${combinedHex}`
      );

      toast("Direct teleport successful!", "ok");
      log(
        `Direct teleport → X:0x${xAddr.toString(16).toUpperCase()} ` +
        `Y:0x${yAddr.toString(16).toUpperCase()} ` +
        `Z:0x${zAddr.toString(16).toUpperCase()} ` +
        `(${x}, ${y}, ${z})`,
        "ok"
      );
    } catch (e) {
      toast("Direct teleport failed", "err");
      log("Direct teleport error: " + e.message, "err");
    }
  }

  async function tpGo() {
    if (!ip) { toast("Not connected", "err"); return; }
    const x = parseFloat(document.getElementById('tp-x').value);
    const y = parseFloat(document.getElementById('tp-y').value);
    const z = parseFloat(document.getElementById('tp-z').value);

    if (isNaN(x) || isNaN(y) || isNaN(z)) {
      toast("Enter valid coordinates", "err");
      return;
    }

    try {
      // Resolve the player pointer with automatic retries. This prevents a
      // single transient PS3MAPI read failure from cancelling the teleport.
      const ptrHex = await readMemHex("190EBB0", 4, 4, 100);
      if (!ptrHex) {
        toast("Failed to resolve player pointer after 4 attempts", "err");
        log("Teleport cancelled: 0x0190EBB0 could not be read. The shared PS3MAPI getmem helper retried 4 times.", "err");
        return;
      }

      const baseAddr = parseInt(ptrHex, 16);
      if (!Number.isFinite(baseAddr) || baseAddr <= 0) {
        toast("Invalid player pointer", "err");
        log(`Teleport cancelled: invalid pointer returned: ${ptrHex}`, "err");
        return;
      }

      log(`Player pointer resolved: 0x${baseAddr.toString(16).toUpperCase()}`, "ok");

      const xHex = floatToBEHex(x);
      const yHex = floatToBEHex(y);
      const zHex = floatToBEHex(z);

      // Single combined write: X/Y/Z are contiguous in memory (0x140-0x14B,
      // 12 bytes total), so one setmem call with a 24-char hex payload does
      // the same job as three separate round-trips — faster, from faster.js.
      // Address still zero-padded/uppercased (that's what actually broke
      // teleport last time); write still goes through ps3fetch so proc=
      // is attached automatically.
      const xAddr = (baseAddr + 0x140).toString(16).padStart(8, '0').toUpperCase();
      const combinedHex = xHex + yHex + zHex;
      let writeSucceeded = false;
      let lastWriteError = null;

      // A single write is still used for X/Y/Z. If the PS3MAPI request itself
      // fails, retry the same write rather than forcing the player to click
      // Teleport again.
      for (let attempt = 1; attempt <= 2; attempt++) {
        try {
          await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${xAddr}&val=${combinedHex}`);
          writeSucceeded = true;
          break;
        } catch (e) {
          lastWriteError = e;
          log(`Teleport write failed (attempt ${attempt}/2): ${e.message}`, "err");
          if (attempt < 2) await sleep(100);
        }
      }

      if (!writeSucceeded) {
        throw lastWriteError || new Error("Teleport memory write failed");
      }

      toast("Teleported successfully!", "ok");
      log(`Teleported to X:${x} Y:${y} Z:${z}`, "ok");
    } catch (e) {
      toast("Teleport failed", "err");
      log("Teleport error: " + e.message, "err");
    }
  }

  function tpSaveCurrent() {
    const x = document.getElementById('tp-x').value;
    const y = document.getElementById('tp-y').value;
    const z = document.getElementById('tp-z').value;

    if (![x,y,z].every(value => value.trim() !== '' && Number.isFinite(Number(value)))) {
      toast("Read coordinates first", "err");
      return;
    }

    const slotName = prompt("Enter a name for this saved location:", `Position ${savedTeleports.length + 1}`);
    if (!slotName) return;
    const folderOptions = teleportFolders.map((f,i) => `${i+1}. ${f.name}`).join('\n');
    const folderAnswer = prompt(`Save \"${slotName}\" in folder:\n${folderOptions}\n\nEnter folder number (default 1):`, '1');
    const folderNumber = Number(folderAnswer || 1);
    const folder = Number.isInteger(folderNumber) && folderNumber >= 1 && folderNumber <= teleportFolders.length ? teleportFolders[folderNumber - 1] : teleportFolders[0];

    if (tpIsDuplicate({x,y,z}) && !confirm('These coordinates are already saved. Save another copy?')) return;
    tpRemember();
    savedTeleports.push({ name: slotName.trim().slice(0,200), x, y, z, folderId: folder.id });
    saveTeleportsToStorage();
    tpRenderSlots();
    toast("Position saved!", "ok");
  }

  async function tpAboveYou() {
    if (!ip) { toast("Not connected", "err"); return; }
    const zInput = document.getElementById('tp-z');
    let currentZ = parseFloat(zInput.value);
    if (isNaN(currentZ)) currentZ = 0;

    zInput.value = (currentZ + 5.0).toFixed(3); // Add 5 units above
    await tpGo();
  }

  function tpRenderSlots() {
    const listEl = document.getElementById('tp-saved-list');
    if (!listEl) return;
    const folders = teleportFolders;
    listEl.innerHTML = folders.map(folder => {
      const entries = savedTeleports.map((p,idx) => ({p,idx})).filter(({p}) => (p.folderId || 'unsorted') === folder.id);
      const children = folder.open ? (entries.length ? entries.map(({p,idx}) => `
        <div class="tp-slot">
          <div class="tp-slot-info"><div class="tp-slot-name">${tpEscape(p.name)}</div><div class="tp-slot-coords">X: ${tpEscape(p.x)} | Y: ${tpEscape(p.y)} | Z: ${tpEscape(p.z)}</div></div>
          <div class="tp-slot-actions"><button class="tp-load-btn" type="button" onclick="tpLoadSlot(${idx})">Go</button><button class="tp-move-btn" type="button" onclick="tpEditSlot(${idx})">Edit</button><button class="tp-move-btn" type="button" onclick="tpMoveSlot(${idx})">Move</button><button class="tp-delete-btn" type="button" onclick="tpDeleteSlot(${idx})">×</button></div>
        </div>`).join('') : '<div class="tp-empty">No saved positions</div>') : '';
      return `<div class="tp-folder"><div class="tp-folder-header"><button type="button" class="tp-folder-toggle" onclick="tpToggleFolder('${folder.id}')">${folder.open ? '▾' : '▸'}</button><span class="tp-folder-icon"></span><span class="tp-folder-name">${tpEscape(folder.name)}</span><span class="tp-folder-count">${entries.length}</span>${folder.system ? '' : `<button type="button" class="tp-folder-menu" onclick="tpRenameFolder('${folder.id}')">Rename</button><button type="button" class="tp-folder-menu danger" onclick="tpDeleteFolder('${folder.id}')">Delete</button>`}</div><div class="tp-folder-body">${children}</div></div>`;
    }).join('') + `<button type="button" class="tp-new-folder-btn" onclick="tpNewFolder()">＋ New Folder</button>`;
  }

  async function tpLoadSlot(idx) {
    const pos = savedTeleports[idx];
    if (!pos) return;
    document.getElementById('tp-x').value = pos.x;
    document.getElementById('tp-y').value = pos.y;
    document.getElementById('tp-z').value = pos.z;
    await tpGo();
  }

  function tpDeleteSlot(idx) {
    tpRemember();
    savedTeleports.splice(idx, 1);
    saveTeleportsToStorage();
    tpRenderSlots();
    toast("Position removed", "ok");
  }

  // Render any teleports restored from localStorage. The tp-saved-list
  // element is defined later in the DOM, so wait until it exists instead
  // of calling this immediately (which was throwing and aborting the rest
  // of this script — including all the Settings modal code below it).
  document.addEventListener('DOMContentLoaded', tpRenderSlots);

  // ── Fly / Speed Multiplier ────────────────────────────────────────────────
  // Same base pointer chain as Teleport (0x190EBB0), offset +0x14C (332
  // decimal) — the float immediately following Z (+0x148), contiguous with
  // the position struct. Reuses readMemHex/ps3getmem so it gets the same
  // HTML-response parsing and proc-scoping fixes as the rest of the tool.
  const SPEED_OFFSET = 0x14C; // 332 decimal

  async function readSpeed() {
    if (!ip) { toast("Not connected", "err"); return; }
    try {
      const ptrHex = await readMemHex("190EBB0", 4);
      if (!ptrHex) { toast("Failed to read player pointer", "err"); return; }

      const baseAddr = parseInt(ptrHex, 16);
      if (!baseAddr) { toast("Invalid pointer address", "err"); return; }

      const speedHex = await readMemHex((baseAddr + SPEED_OFFSET).toString(16), 4);
      if (!speedHex) { toast("Failed to read speed", "err"); return; }

      const speedFloat = beHexToFloat(speedHex);
      document.getElementById('speed-multiplier').value = speedFloat.toFixed(2);
      log(`Speed read → x${speedFloat.toFixed(2)}`, 'ok');
      toast("Speed read successfully", "ok");
    } catch (e) {
      toast("Error reading speed", "err");
      log("Read speed error: " + e.message, "err");
    }
  }

  async function writeSpeed() {
    if (!ip) { toast("Not connected", "err"); return; }

    const speedVal = parseFloat(document.getElementById('speed-multiplier').value);
    if (isNaN(speedVal)) { toast("Enter a valid number", "err"); return; }

    try {
      const ptrHex = await readMemHex("190EBB0", 4);
      if (!ptrHex) { toast("Failed to read player pointer", "err"); return; }

      const baseAddr = parseInt(ptrHex, 16);
      if (!baseAddr) { toast("Invalid pointer address", "err"); return; }

      const speedAddr = (baseAddr + SPEED_OFFSET).toString(16).toUpperCase();
      const hexSpeed = floatToBEHex(speedVal);

      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${speedAddr}&val=${hexSpeed}`);
      log(`Speed set → x${speedVal} (${hexSpeed})`, 'ok');
      toast("Speed applied!", "ok");
    } catch (e) {
      toast("Failed to apply speed", "err");
      log("Write speed error: " + e.message, "err");
    }
  }

  async function resetSpeed() {
    document.getElementById('speed-multiplier').value = '1.00';
    await writeSpeed();
  }

  // ── Character Utilities (Character Utilities) ───────────────
  const CHAR_NAME_ADDR       = 0x32486DC4;

  const CONSUMABLE_BASE      = 0x600EAE60;
  const CONSUMABLE_VALUE_OFF = 0x10;
  const CONSUMABLE_STRIDE    = 0x20;
  const CONSUMABLE_COUNT     = 20;

  const REP_ITEM_ADDR        = 0x600EAE80;
  const REP_VALUE_ADDR       = 0x600EAE92;

  let splitRepOriginal = null;

  function intToHexBE(val) {
    const buf = new ArrayBuffer(4);
    const view = new DataView(buf);
    view.setInt32(0, val, false);
    return view.getUint32(0, false).toString(16).padStart(8, '0').toUpperCase();
  }

  function uintToHexBE(val) {
    return (Number(val) >>> 0).toString(16).padStart(8, '0').toUpperCase();
  }

  function uint16ToHexBE(val) {
    return (Number(val) & 0xFFFF).toString(16).padStart(4, '0').toUpperCase();
  }

  function byteToHex(val) {
    return (Number(val) & 0xFF).toString(16).padStart(2, '0').toUpperCase();
  }

  function hexBEToInt(hexStr) {
    const hex = String(hexStr || '').replace(/[^0-9a-f]/gi, '').slice(0, 8);
    if (hex.length < 8) return 0;
    const num = parseInt(hex, 16) >>> 0;
    return num > 0x7FFFFFFF ? num - 0x100000000 : num;
  }

  async function readInt32At(addr) {
    const hex = await ps3getmem(addr, 4);
    if (!hex || hex.length < 8) throw new Error('Read failed');
    return hexBEToInt(hex);
  }

  async function readByteAt(addr) {
    const hex = await ps3getmem(addr, 1);
    if (!hex || hex.length < 2) throw new Error('Read failed');
    return parseInt(hex.slice(0, 2), 16);
  }

  async function writeHexAt(addr, hex) {
    const addrHex = Number(addr).toString(16).padStart(8, '0').toUpperCase();
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addrHex}&val=${hex}`);
    await henDelay();
  }

  async function writeInt32At(addr, value) {
    await writeHexAt(addr, intToHexBE(value));
  }

  async function readCharacterNameAtAddress(addr, maxBytes = 32) {
    const addrHex = Number(addr).toString(16).padStart(8, '0').toUpperCase();
    const url = `http://${ip}/getmem.ps3mapi?proc=0x1010200&addr=${addrHex}&len=${maxBytes}&_=${Date.now()}`;
    const xhr = await ps3fetchRead(url, 8000);
    const page = xhr.responseText || '';

    // webMAN / PS3MAPI commonly puts the raw bytes in the hidden output textarea.
    const textareaMatch = page.match(/<textarea[^>]*id=["']output["'][^>]*>([0-9A-Fa-f]*)<\/textarea>/i);
    let hex = textareaMatch ? textareaMatch[1] : '';

    // Fallback: collect bytes from the human-readable memory dump rows.
    if (!hex) {
      const bytePairs = [];
      const byteRegex = /\b([0-9A-Fa-f]{2})\b/g;
      const relevant = page.slice(Math.max(0, page.indexOf(addrHex)));
      let m;
      while ((m = byteRegex.exec(relevant)) && bytePairs.length < maxBytes) {
        bytePairs.push(m[1]);
      }
      hex = bytePairs.join('');
    }

    if (!hex || hex.length < 2) {
      throw new Error('No readable character-name data returned by PS3MAPI');
    }

    let result = '';
    for (let i = 0; i + 1 < hex.length && result.length < maxBytes; i += 2) {
      const b = parseInt(hex.slice(i, i + 2), 16);
      if (!Number.isFinite(b) || b === 0) break;
      result += String.fromCharCode(b);
    }

    return result;
  }

  async function readCharacterInfo() {
    if (!ip) { toast('Not connected', 'err'); return; }

    const btn = document.getElementById('char-read-btn');
    if (btn) btn.disabled = true;

    try {
      const name = await readCharacterNameAtAddress(CHAR_NAME_ADDR, 32);
      const input = document.getElementById('char-name');
      if (input) input.value = name || '';

      log(`Character name read — ${name || 'Unknown'}`, 'ok');
      toast(name ? 'Character name read' : 'Character name is empty', name ? 'ok' : '');
    } catch (e) {
      const msg = e?.message || String(e);
      log(`Character name read failed: ${msg}`, 'err');
      toast('Character name read failed', 'err');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function setCharacterInfo() {
    if (!ip) { toast('Not connected', 'err'); return; }

    const name = (document.getElementById('char-name')?.value || '').trim();
    if (!name) {
      toast('Enter a character name', 'err');
      return;
    }

    if (!confirm('Set the entered character name on the connected console?')) return;

    const btn = document.getElementById('char-set-btn');
    if (btn) btn.disabled = true;

    try {
      await writeAscii(CHAR_NAME_ADDR, name.slice(0, 31));
      log(`Character name updated — ${name.slice(0, 31)}`, 'ok');
      toast('Character name updated', 'ok');
    } catch (e) {
      log(`Character name write failed: ${e.message}`, 'err');
      toast('Character name write failed', 'err');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function sendInGameToast() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const title = document.getElementById('toast-title').value.trim() || 'Destiny RTM';
    const body  = document.getElementById('toast-body').value.trim()  || '';
    try {
      // Call(7669056 /*0x750540*/, 268632064 /*0x10030000*/) — params are decimal
      await ps3call('750540', '268632064');
      await writeUtf16('10030200', title);
      await writeUtf16('10030400', body);
      // Call(7636756 /*0x748714*/, 28508392 /*0x1B300E8*/, 28507992 /*0x1B2FF58*/, 268632064 /*0x10030000*/, 7, 0)
      await ps3call('748714', '28508392', '28507992', '268632064', '7', '0');
      log(`Original RTM toast sequence dispatched: "${title}"`, 'ok');
      toast('RTM toast request dispatched', 'ok');
    } catch(e) {
      log('Original RTM toast request failed: ' + (e?.message || e), 'err');
      toast('RTM toast request failed', 'err');
    }
  }

  // Documented webMAN/PS3MAPI fallback. This is a system/XMB notification,
  // not the Destiny-internal toast produced by soulsAPI's Extension.Call().
  async function sendPS3SystemToastFromRTM() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const body = document.getElementById('toast-body')?.value.trim() || 'Hello from Destiny RTM';
    try {
      await ps3fetch(`http://${ip}/notify.ps3mapi?msg=${encodeURIComponent(body)}`);
      log(`PS3 system notification dispatched: "${body}"`, 'ok');
      toast('PS3 system notification sent ', 'ok');
    } catch (e) {
      log('PS3 system notification failed: ' + (e?.message || e), 'err');
      toast('PS3 notification failed', 'err');
    }
  }

  // PS3 system notification — uses PS3MAPI's built-in notify endpoint
  // Shows as an XMB-style popup bubble with the PS3 system icon
  function sendPS3Notification() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const msg = document.getElementById('notif-msg').value.trim();
    if (!msg) { toast('Enter a message', 'err'); return; }
    const encoded = encodeURIComponent(msg);
    ps3fetch(`http://${ip}/notify.ps3mapi?msg=${encoded}`)
      .then(() => { log(`PS3 notification: "${msg}"`, 'ok'); toast('Notification sent ', 'ok'); })
      .catch(() => { log('Notification failed', 'err'); toast('Notification failed', 'err'); });
  }

  function notifyPS3(msg) {
    if (!ip) return;
    const encoded = encodeURIComponent(msg);
    ps3fetch(`http://${ip}/notify.ps3mapi?msg=${encoded}`).catch(() => {});
  }

  // Browser-side best-effort RPC initialization corresponding to the source's
  // EnableRPC(4882496). The exact soulsAPI implementation is inside the DLL;
  // this HTTP route is therefore treated as experimental rather than proven.
  async function enableRPC() {
    if (!ip) return;
    try {
      await ps3fetch(`http://${ip}/enablerpc.ps3mapi?addr=4A8040`);
      log('RPC enable request accepted; function-call capability still requires verification', '');
    } catch (error) {
      log(`RPC enable request failed: ${error.message}`, 'err');
    }
  }

  // ── Player hook install / uninstall ───────────────────────────────────────
  // Mirrors writePlayerHook() / unHookPlayer() from AFKCrucible/Form1.cs.
  // The hook patches a branch at 0x1C4110 to redirect execution into a small
  // PPC stub at 0x10D8EC0. Without the hook active, writes to the gamespeed
  // address (0x30606E4) are overwritten by the game every frame.
  //
  // Step 1 — write the 116-byte PPC stub (same bytes as OFF_HEX used by
  //           Server Cheats; that constant already holds the base hook).
  // Step 2 — write hook data values into the hook's data region.
  // Step 3 — write the branch patch at 0x1C4110 → 0x48F14DB0 to activate.
  //
  // On disconnect the branch is restored to the original instruction (0x30640140).

  // Lock flag: true while installPlayerHook is in progress.
  // Prevents srvSetMode from touching 0x10D8EC0 mid-write.
  let hookInstalling = false;

  async function installPlayerHook() {
    if (!ip) return;
    hookInstalling = true;
    try {
      // Step 1: write the 116-byte PPC stub to 0x10D8EC0.
      // Branch is NOT yet active, so no need to deactivate first.
      // henDelay() guards against HEN's slower write handler being overwhelmed.
      const stubHex = '3C6001906063EBB0F9C3FFD8F9E3FFD0DA03FFE0DA23FFE87DE0002681C30014C2040140C22E0000FC10884040820028C2040144C22E0004FC10884040820018C2040148C22E0008FC10884040820008908300007DEFF120E9C3FFD8E9E3FFD0CA03FFE0CA23FFE8306401403C80010A4B0EB1E4';
      const stubChunks = stubHex.match(/.{8}/g);
      for (let i = 0; i < stubChunks.length; i++) {
        const addr = (0x10D8EC0 + i * 4).toString(16).toUpperCase();
        await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${stubChunks[i]}`);
        await henDelay();
      }
      // Zero the 152-byte tail region at 0x10D8F34
      for (let i = 0; i < 38; i++) {
        const addr = (0x10D8F34 + i * 4).toString(16).toUpperCase();
        await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=00000000`);
        await henDelay();
      }

      // Step 2: write hook data values
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBC4&val=306E8D58`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBAC&val=3F99999A`); await henDelay(); // 1.2f
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBD0&val=C499DA3F`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBD4&val=C41B299C`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBD8&val=4230BCFE`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBE0&val=C49979E6`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBE4&val=C42005F0`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBE8&val=41F563DA`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBF0&val=C499A918`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBF4&val=C425A282`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBF8&val=420AF6F6`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EBFC&val=00000001`); await henDelay();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=190EC00&val=00000002`); await henDelay();

      // Step 3: activate — branch patch at 0x1C4110 → jumps to stub.
      // Stub is fully written before this, so the CPU will never hit a partial stub.
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=1C4110&val=48F14DB0`);

      log('Player hook installed — gamespeed active', 'ok');
    } catch(e) {
      log('Player hook install failed', 'err');
    } finally {
      hookInstalling = false;
    }
  }

  async function uninstallPlayerHook() {
    if (!ip) return;
    try {
      // Restore original instruction at the branch patch site
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=1C4110&val=30640140`);
      log('Player hook removed', 'ok');
    } catch(e) {}
  }

  const toggleState = {};

  // size: 4 (default, UInt32), 2 (UInt16), or 1 (byte) — must match what the
  // solution uses or adjacent memory gets corrupted (e.g. immunity bug)
  async function fire(addr, val, label, restoreVal, size = 4) {
    if (!ip) { toast('Not connected', 'err'); return; }
    const key = addr + '_' + label;
    const isToggle = restoreVal !== undefined;
    const isOn = isToggle ? !toggleState[key] : true;
    const writeVal = (isToggle && toggleState[key]) ? restoreVal : val;
    const stateLabel = isToggle ? (isOn ? ' ON' : ' OFF') : '';
    const sizeParam = size !== 4 ? `&size=${size}` : '';

    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${writeVal}${sizeParam}`);
      if (isToggle) toggleState[key] = isOn;
      log(`${label}${stateLabel} → addr:${addr} val:${writeVal} [${size}b]`, 'ok');
      toast(label + stateLabel + ' ', 'ok');
    } catch(e) {
      log(`${label} failed → ${addr}`, 'err');
      toast(label + ' failed', 'err');
    }
  }

  // Sends multiple address/value pairs strictly sequentially (one at a time)
  // EnableRPC is called first to stabilise PS3MAPI before the write sequence
  // fireMulti: onPairs = [[addr,val],...], offPairs = [[addr,val],...] OR legacy restoreVal string
  async function fireMulti(onPairs, offPairsOrRestore, label, size = 4) {
    if (!ip) { toast('Not connected', 'err'); return; }
    // Legacy call: fireMulti(pairs, label, restoreVal) — single restore value applied to all
    if (typeof offPairsOrRestore === 'string' || offPairsOrRestore === undefined) {
      const pairs      = onPairs;
      const lbl        = offPairsOrRestore ?? label;
      const restoreVal = label;  // 3rd arg is restoreVal in legacy form
      const key = lbl + '_multi';
      const isToggle = restoreVal !== undefined;
      const isOn = isToggle ? !toggleState[key] : true;
      const writeVal = (isToggle && toggleState[key]) ? restoreVal : null;
      const stateLabel = isToggle ? (isOn ? ' ON' : ' OFF') : '';
      try {
        for (const [addr, val] of pairs) {
          const v = writeVal !== null ? writeVal : val;
          const sizeParam = size !== 4 ? `&size=${size}` : '';
          await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${v}${sizeParam}`);
        }
        if (isToggle) toggleState[key] = isOn;
        log(`${lbl}${stateLabel} → ${pairs.length} writes sent`, 'ok');
        toast(lbl + stateLabel + ' ', 'ok');
      } catch(e) {
        log(`${lbl} failed`, 'err');
        toast(lbl + ' failed', 'err');
      }
      return;
    }
    // New call: fireMulti(onPairs, offPairs, label, size?)
    const offPairs = offPairsOrRestore;
    const lbl = label;
    const key = lbl + '_multi2';
    const isOn = !toggleState[key];
    const activePairs = isOn ? onPairs : offPairs;
    const stateLabel = isOn ? ' ON' : ' OFF';
    try {
      for (const [addr, val] of activePairs) {
        const sizeParam = size !== 4 ? `&size=${size}` : '';
        await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${val}${sizeParam}`);
      }
      toggleState[key] = isOn;
      log(`${lbl}${stateLabel} → ${activePairs.length} writes sent`, 'ok');
      toast(lbl + stateLabel + ' ', 'ok');
    } catch(e) {
      log(`${lbl} failed`, 'err');
      toast(lbl + ' failed', 'err');
    }
  }

  // Convenience wrapper for multi-write with a non-4-byte size
  async function fireMultiSize(onPairs, offPairs, label, size) {
    return fireMulti(onPairs, offPairs, label, size);
  }

  let writeSize = 4;
  let advancedMode = false;

  function toggleWriteMode() {
    advancedMode = !advancedMode;
    const toggle = document.getElementById('write-mode-toggle');
    const label  = document.getElementById('write-mode-label');
    const sizeEl = document.getElementById('size-selector');
    if (advancedMode) {
      toggle.classList.add('advanced');
      label.textContent = 'ADVANCED';
      sizeEl.style.display = '';
    } else {
      toggle.classList.remove('advanced');
      label.textContent = '4 BYTE';
      sizeEl.style.display = 'none';
      writeSize = 4;
      document.querySelectorAll('.size-btn').forEach((b,i) => b.classList.toggle('active', i===0));
    }
  }

  function setSize(el, sz) {
    document.querySelectorAll('.size-btn').forEach(b => b.classList.remove('active'));
    el.classList.add('active');
    writeSize = sz;
  }

  function sendCustom() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const addr = document.getElementById('c-addr').value.trim().replace(/^0x/i,'');
    const val  = document.getElementById('c-val').value.trim().replace(/^0x/i,'');
    if (!addr || !val) { toast('Enter address & value', 'err'); return; }
    const paddedVal = advancedMode ? val : val.padStart(8,'0');
    const url = (advancedMode && writeSize !== 4)
      ? `http://${ip}/setmem.ps3mapi?addr=${addr}&val=${paddedVal}&size=${writeSize}`
      : `http://${ip}/setmem.ps3mapi?addr=${addr}&val=${paddedVal}`;
    ps3fetch(url)
      .then(() => { log(`Custom → addr:${addr} val:${paddedVal} [${writeSize}b]`, 'ok'); toast('Sent ', 'ok'); })
      .catch(() => { log(`Custom failed → ${addr}`, 'err'); toast('Send failed', 'err'); });
  }
  // If served from the PS3 web server, auto-fill the IP and connect
  window.addEventListener("DOMContentLoaded", () => {
    if (ip) {
      document.getElementById("ip").value = ip;
      document.getElementById("ip").disabled = true;
      connect();
    }
  });

  // ── Shared slider helper ──────────────────────────────────────────────────
  function setSlider(sliderId, valId, val, suffix, displayFn) {
    const el = document.getElementById(sliderId);
    el.value = val;
    const display = displayFn ? displayFn(val) : (val + suffix);
    document.getElementById(valId).textContent = display;
  }

  // ── Gamespeed ─────────────────────────────────────────────────────────────
  // C#: checkBox20/trackBar1 → WriteFloat(811100260, value) = 0x30586864

  async function applyGamespeed() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const speed = parseFloat(document.getElementById('gs-input').value);
    if (isNaN(speed) || speed <= 0) { toast('Enter a valid speed', 'err'); return; }
    const hex = floatToHex(speed);
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=30586864&val=${hex}`);
      log(`Gamespeed → x${speed} (${hex})`, 'ok');
      toast(`Gamespeed x${speed} `, 'ok');
    } catch(e) { log('Gamespeed write failed', 'err'); toast('Gamespeed failed', 'err'); }
  }

  async function resetGamespeed() {
    if (!ip) { toast('Not connected', 'err'); return; }
    document.getElementById('gs-input').value = '1';
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=30586864&val=3F800000`);
      log('Gamespeed reset → x1', 'ok');
      toast('Gamespeed reset ', 'ok');
    } catch(e) { log('Gamespeed reset failed', 'err'); toast('Gamespeed reset failed', 'err'); }
  }

  // ── FOV helpers ───────────────────────────────────────────────────────
  const FOV_ADDR = 0x0070906C;

  function updateFOV(value) {
    const n = Number(value);
    if (!Number.isFinite(n)) return;

    const val = document.getElementById('fov-val');
    if (val) val.textContent = n.toFixed(2);

    const custom = document.getElementById('fov-custom');
    if (custom && document.activeElement !== custom) custom.value = String(n);
  }

  async function writeFOVValue(fov) {
    if (!ip) throw new Error('Not connected');

    const n = Number(fov);
    if (!Number.isFinite(n)) throw new Error('Invalid FOV value');

    const hex = floatToHex(n);
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${FOV_ADDR.toString(16).toUpperCase()}&val=${hex}`);
    return hex;
  }

  async function applyFOV() {
    if (!ip) { toast('Not connected', 'err'); return; }

    const slider = document.getElementById('fov-slider');
    const custom = document.getElementById('fov-custom');

    let value = Number(slider?.value ?? 2);
    if (custom && String(custom.value).trim() !== '') {
      const customValue = Number(custom.value);
      if (Number.isFinite(customValue)) value = customValue;
    }

    const btn = document.getElementById('fov-apply-btn');
    if (btn) btn.disabled = true;

    try {
      const hex = await writeFOVValue(value);
      const val = document.getElementById('fov-val');
      if (val) val.textContent = value.toFixed(2);
      log(`FOV ${value} → 0070906C = ${hex}`, 'ok');
      toast(`FOV applied: ${value}`, 'ok');
    } catch (e) {
      log(`FOV failed: ${e.message || e}`, 'err');
      toast('FOV write failed', 'err');
    } finally {
      if (btn) btn.disabled = false;
    }
  }

  async function resetFOV() {
    const slider = document.getElementById('fov-slider');
    const custom = document.getElementById('fov-custom');
    if (slider) slider.value = '2';
    if (custom) custom.value = '2';
    updateFOV(2);

    if (!ip) { toast('Not connected', 'err'); return; }

    try {
      const hex = await writeFOVValue(2.0);
      log(`FOV reset → 0070906C = ${hex}`, 'ok');
      toast('FOV reset to 2.00', 'ok');
    } catch (e) {
      log(`FOV reset failed: ${e.message || e}`, 'err');
      toast('FOV reset failed', 'err');
    }
  }

  // ── FOV helpers ───────────────────────────────────────────────────────────
  // Formula from C#: fov = 2.0 + trackBar1.Value / 10.0  (trackBar range 0–10)
  // Addr: 0x70906C  — WriteFloat
  function fovDisplay(val) {
    return (2.0 + parseFloat(val) / 10.0).toFixed(1);
  }

  function floatToHex(f) {
    const buf = new ArrayBuffer(4);
    new DataView(buf).setFloat32(0, f, false); // big-endian
    return Array.from(new Uint8Array(buf)).map(b => b.toString(16).padStart(2,'0')).join('').toUpperCase();
  }

  async function writeFOVValue(fov) {
    if (!ip) { toast('Not connected', 'err'); return false; }
    if (!Number.isFinite(fov)) {
      toast('Enter a valid FOV number', 'err');
      return false;
    }

    const hex = floatToHex(fov);
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=70906C&val=${hex}`);
      log(`FOV set → ${fov} (${hex})`, 'ok');
      toast(`FOV ${fov} `, 'ok');
      return true;
    } catch(e) {
      log('FOV write failed', 'err');
      toast('FOV failed', 'err');
      return false;
    }
  }

  async function applyFOV() {
    const raw = parseFloat(document.getElementById('fov-slider').value);
    const fov = 2.0 + raw / 10.0;
    const ok = await writeFOVValue(fov);
    if (ok) {
      const custom = document.getElementById('fov-custom');
      if (custom) custom.value = String(fov);
    }
  }

  async function applyCustomFOV() {
    const input = document.getElementById('fov-custom');
    if (!input) return;

    const text = input.value.trim();
    if (!text) {
      toast('Enter an FOV number', 'err');
      input.focus();
      return;
    }

    const fov = Number(text);
    if (!Number.isFinite(fov)) {
      toast('Enter a valid FOV number', 'err');
      input.focus();
      return;
    }

    const ok = await writeFOVValue(fov);
    if (!ok) return;

    // Keep the normal slider in sync when the custom value is inside its 2.0–3.0 range.
    if (fov >= 2.0 && fov <= 3.0) {
      const slider = document.getElementById('fov-slider');
      const display = document.getElementById('fov-val');
      const raw = Math.round((fov - 2.0) * 10);
      if (slider) slider.value = String(raw);
      if (display) display.textContent = fov.toFixed(1);
    } else {
      const display = document.getElementById('fov-val');
      if (display) display.textContent = 'Custom';
    }
  }

  async function resetFOV() {
    if (!ip) { toast('Not connected', 'err'); return; }
    document.getElementById('fov-slider').value = 0;
    document.getElementById('fov-val').textContent = '2.0';
    const custom = document.getElementById('fov-custom');
    if (custom) custom.value = '2.0';
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=70906C&val=40000000`);
      log('FOV reset → 2.0 (40000000)', 'ok');
      toast('FOV reset ', 'ok');
    } catch(e) {
      log('FOV reset failed', 'err');
      toast('FOV reset failed', 'err');
    }
  }

  // ── Player Name ───────────────────────────────────────────────────────────
  // C#: ReadString / WriteString at ReadUInt32(0x1897C48) + 0xC4
  // Name is stored as ASCII on PS3 (not UTF-16).

  async function resolveNameAddr() {
    const ptr = await ps3readU32(0x01897C48);
    if (!ptr || ptr === 0) throw new Error('Name pointer is null — are you in-game?');
    return (ptr + 0xC4) >>> 0;
  }

  // ASCII string writer (mirrors writeUtf16 above, but 1 byte/char — the
  // name field is plain ASCII on PS3, confirmed by the C# ReadString call).
  function toAsciiHex(str) {
    let hex = '';
    for (let i = 0; i < str.length; i++) {
      hex += (str.charCodeAt(i) & 0xFF).toString(16).padStart(2, '0');
    }
    hex += '00'; // null terminator
    while (hex.length % 8 !== 0) hex += '00'; // pad to 4-byte boundary
    return hex;
  }

  async function writeAscii(baseAddr, str) {
    const hex = toAsciiHex(str);
    const chunks = hex.match(/.{8}/g);
    for (let i = 0; i < chunks.length; i++) {
      const addr = (baseAddr + i * 4).toString(16).toUpperCase();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${chunks[i]}`);
    }
  }

  // This is a self-rename only — it reads/writes whatever name you type into
  // the box below. It does not pull names from other players in your lobby.
  async function readPlayerName() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const input = document.getElementById('visuals-name');
    try {
      const addr = await resolveNameAddr();
      const name = await ps3readString(addr, 17); // 16 chars + null terminator
      input.value = name;
      log(`Name: read "${name}"`, 'ok');
      toast('Name read ', 'ok');
    } catch (e) {
      log(`Name read failed: ${e.message}`, 'err');
      toast('Name read failed', 'err');
    }
  }

  async function writePlayerName() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const input = document.getElementById('visuals-name');
    const name = input.value.slice(0, 16); // matches the C# TextBox MaxLength
    if (!name) { toast('Type a name first', 'err'); return; }
    try {
      const addr = await resolveNameAddr();
      await writeAscii(addr, name);
      log(`Name: set to "${name}"`, 'ok');
      toast('Name set ', 'ok');
    } catch (e) {
      log(`Name write failed: ${e.message}`, 'err');
      toast('Name write failed', 'err');
    }
  }

  // ── Anti-Matchmaking ─────────────────────────────────────────────────────
  // Enable:  0x00913338 → 3BA0005E  (prevents the game from finding/joining lobbies)
  // Disable: 0x00913338 → A3BF00DA  (restores normal matchmaking)
  // IMPORTANT: only activate from Orbit/Tower — patching mid-search corrupts
  // the matchmaking state machine and crashes.
  let antiMMState = false;
  let antiMMBusy  = false;
  async function togAntiMatchmaking(btn) {
    if (!ip)        { updateCheatStatus('tog-anti-mm', 'failed'); toast('Not connected', 'err'); return; }
    if (antiMMBusy) { toast('Please wait…', 'err'); return; }

    const willEnable = !antiMMState;
    if (willEnable && !confirm('Only activate Anti-Matchmaking from Orbit or the Tower.\n\nActivating during matchmaking WILL crash the game.\n\nContinue?')) return;

    antiMMBusy = true;
    updateCheatStatus('tog-anti-mm', 'applying');
    const val = willEnable ? '3BA0005E' : 'A3BF00DA';
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=00913338&val=${val}`);
      antiMMState = willEnable;
      btn.classList.toggle('active-cheat', antiMMState);
      updateCheatStatus('tog-anti-mm', antiMMState ? 'on' : 'off');
      const stateWord = antiMMState ? 'ON' : 'OFF';
      log(`Anti-Matchmaking ${stateWord} → 00913338 = ${val}`, 'ok');
      toast(`Anti-Matchmaking ${stateWord}`, 'ok');
    } catch(e) {
      updateCheatStatus('tog-anti-mm', 'failed');
      log(`Anti-Matchmaking failed: ${e.message}`, 'err');
      toast('Write failed', 'err');
    } finally {
      antiMMBusy = false;
    }
  }

  // ── Settings / section visibility ───────────────────────────────────────
  const TOOL_SETUP_STORAGE_KEY = 'destinyRtmToolSetupV1';
  const SECTION_STORAGE_PREFIX = 'destinyRtmSectionVisible_';

  const SECTIONS = [
    { id: 'sec-connection',          label: 'Connection',      setup: false, lockedOn: true },    { id: 'sec-main-cheats',         label: 'Main Cheats',     setup: true,  description: 'Core player and gameplay cheats.' },
    { id: 'sec-quick-access',        label: 'Quick Access',      setup: true,  description: 'Pinned cheats and macros.' },
    { id: 'sec-server-cheats',       label: 'Server Cheats',   setup: true,  description: 'Server-side and session-related options.' },    { id: 'sec-character-utils',     label: 'Character Utilities', setup: true, description: 'Read and edit the connected character name.' },    { id: 'sec-gamespeed',           label: 'Gamespeed',       setup: true,  description: 'Game-speed controls and presets.' },
    { id: 'sec-damage-tuning',       label: 'Damage Tuning',   setup: true,  description: 'Custom damage-value controls and presets.' },
    { id: 'sec-fov',                 label: 'FOV',             setup: true,  description: 'Field-of-view controls.' },
    { id: 'sec-custom-offset',       label: 'Custom Offset',   setup: true,  description: 'Manual memory address/value tools.', recommended: false },
    { id: 'sec-useless',             label: 'Visuals',         setup: true,  description: 'Visual and display-related options.' },    { id: 'activity-loader-panel',   label: 'Activity Loader', setup: true,  description: 'Load missions, strikes, raids and destinations.' }
  ];

  const secVisible = {};

  // One-time migration: make sure Character Utilities is visible after upgrading
  // from older builds where a saved section preference may have hidden it.
  const CHAR_UTILS_VISIBILITY_MIGRATION_KEY = 'destinyRtmCharacterUtilsVisibleV1';
  try {
    if (localStorage.getItem(CHAR_UTILS_VISIBILITY_MIGRATION_KEY) !== '1') {
      localStorage.setItem(SECTION_STORAGE_PREFIX + 'sec-character-utils', '1');
      localStorage.setItem(CHAR_UTILS_VISIBILITY_MIGRATION_KEY, '1');
    }
  } catch (e) {}

  SECTIONS.forEach(s => {
    if (s.lockedOn) {
      secVisible[s.id] = true;
      return;
    }
    let saved = null;
    try { saved = localStorage.getItem(SECTION_STORAGE_PREFIX + s.id); } catch (e) {}
    // Migrate old per-session visibility if present.
    if (saved === null) {
      try {
        const legacy = sessionStorage.getItem('secvis_' + s.id);
        if (legacy !== null) saved = legacy;
      } catch (e) {}
    }
    secVisible[s.id] = saved === null ? true : saved === '1';
  });

  function persistSectionVisibility(id, visible) {
    try { localStorage.setItem(SECTION_STORAGE_PREFIX + id, visible ? '1' : '0'); } catch (e) {}
  }

  function isToolSetupComplete() {
    try { return localStorage.getItem(TOOL_SETUP_STORAGE_KEY) === '1'; }
    catch (e) { return false; }
  }

  function applySectionVisibility() {
    SECTIONS.forEach(s => {
      const el = document.getElementById(s.id);
      if (el) el.style.display = (s.lockedOn || secVisible[s.id]) ? '' : 'none';
    });
    if (typeof applyRTMTab === 'function') applyRTMTab();
  }

  function toggleSection(id) {
    const el = document.getElementById(id);
    if (el) el.classList.toggle('collapsed');
  }


  function renderToolSetup() {
    const grid = document.getElementById('tool-setup-grid');
    if (!grid) return;
    grid.innerHTML = '';
    SECTIONS.filter(s => s.setup).forEach(s => {
      const row = document.createElement('label');
      row.className = 'tool-setup-option';
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.dataset.sectionId = s.id;
      cb.checked = s.recommended !== false;
      const text = document.createElement('span');
      const title = document.createElement('strong');
      title.textContent = s.label;
      const help = document.createElement('small');
      help.textContent = s.description || '';
      text.append(title, help);
      row.append(cb, text);
      grid.appendChild(row);
    });
  }

  function updateToolSetupSaveState() {
    const saveBtn = document.getElementById('tool-setup-save-btn');
    if (!saveBtn) return;
    const henChoice = document.querySelector('input[name="tool-setup-hen"]:checked');
    saveBtn.disabled = !henChoice;
  }

  function showToolSetup() {
    const overlay = document.getElementById('tool-setup-overlay');
    if (!overlay || isToolSetupComplete()) return;
    renderToolSetup();
    updateToolSetupSaveState();
    overlay.classList.add('show');
  }

  function toolSetupRecommended() {
    document.querySelectorAll('#tool-setup-grid input[data-section-id]').forEach(cb => {
      const s = SECTIONS.find(x => x.id === cb.dataset.sectionId);
      cb.checked = s ? s.recommended !== false : true;
    });
  }

  function toolSetupAll() {
    document.querySelectorAll('#tool-setup-grid input[data-section-id]').forEach(cb => cb.checked = true);
  }

  function saveToolSetup() {
    const henChoice = document.querySelector('input[name="tool-setup-hen"]:checked');
    if (!henChoice) {
      if (typeof toast === 'function') toast('Choose whether you are using PS3HEN before continuing.', 'err');
      return;
    }

    const useHen = henChoice.value === '1';
    try { localStorage.setItem('henMode', useHen ? '1' : '0'); } catch (e) {}
    try { sessionStorage.setItem('henMode', useHen ? '1' : '0'); } catch (e) {}
    if (typeof henMode !== 'undefined') henMode = useHen;
    if (typeof applyHenToggleUI === 'function') applyHenToggleUI();

    document.querySelectorAll('#tool-setup-grid input[data-section-id]').forEach(cb => {
      secVisible[cb.dataset.sectionId] = cb.checked;
      persistSectionVisibility(cb.dataset.sectionId, cb.checked);
    });
    try { localStorage.setItem(TOOL_SETUP_STORAGE_KEY, '1'); } catch (e) {}
    applySectionVisibility();

    const overlay = document.getElementById('tool-setup-overlay');
    if (overlay) overlay.classList.remove('show');

    const serverCheatsEnabled = !!secVisible['sec-server-cheats'];
    const loaderEnabled = !!secVisible['activity-loader-panel'];
    const debugBypass = typeof isDebugModeEnabled === 'function' && isDebugModeEnabled();

    if (!debugBypass && serverCheatsEnabled && typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      setTimeout(openServerSafetyGuide, 120);
    } else if (!debugBypass && loaderEnabled && typeof hasReadActivityGuide === 'function' && !hasReadActivityGuide()) {
      activityGuideRequired = true;
      setActivityLoaderLocked(true);
      setTimeout(openActivityGuide, 120);
    }

    if (typeof toast === 'function') toast(`Setup saved. HEN mode ${useHen ? 'enabled' : 'disabled'}.`, 'ok');
  }

  window.showToolSetup = showToolSetup;
  window.toolSetupRecommended = toolSetupRecommended;
  window.toolSetupAll = toolSetupAll;
  window.saveToolSetup = saveToolSetup;

  document.addEventListener('change', function(event) {
    if (event.target && event.target.matches('input[name="tool-setup-hen"]')) {
      updateToolSetupSaveState();
    }
  });

  function openSettings() {
    const overlay = document.getElementById('settings-overlay');
    if (!overlay) return;
    const picker = document.getElementById('settings-accent');
    const current = getComputedStyle(document.documentElement).getPropertyValue('--accent').trim();
    if (picker && /^#[0-9a-fA-F]{6}$/.test(current)) picker.value = current;
    renderSettingsSections();
    overlay.style.display = 'flex';
    overlay.setAttribute('aria-hidden', 'false');
    document.body.classList.add('settings-open');
    setTimeout(() => document.getElementById('settings-modal')?.focus(), 0);
  }

  function closeSettings() {
    const overlay = document.getElementById('settings-overlay');
    if (!overlay) return;
    overlay.style.display = 'none';
    overlay.setAttribute('aria-hidden', 'true');
    document.body.classList.remove('settings-open');
  }

  function closeSettingsOutside(e) {
    if (e.target === document.getElementById('settings-overlay')) closeSettings();
  }

  // Explicitly expose Settings controls so the gear button works even if another
  // initialization handler elsewhere in the RTM throws an unrelated runtime error.
  window.openSettings = openSettings;
  window.closeSettings = closeSettings;

  function renderSettingsSections() {
    const container = document.getElementById('settings-section-toggles');
    if (!container) return;
    container.innerHTML = '';
    SECTIONS.filter(s => !s.lockedOn).forEach(s => {
      const row = document.createElement('label');
      row.className = 'settings-toggle';
      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.checked = !!secVisible[s.id];
      cb.onchange = () => {
        secVisible[s.id] = cb.checked;
        persistSectionVisibility(s.id, cb.checked);
        applySectionVisibility();
        if (s.id === 'activity-loader-panel' && cb.checked && typeof activityGuideRequired !== 'undefined' && !hasReadActivityGuide()) {
          activityGuideRequired = true;
          setActivityLoaderLocked(true);
          setTimeout(openActivityGuide, 80);
        }
        if (s.id === 'sec-server-cheats' && cb.checked && typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
          serverSafetyRequired = true;
          setServerCheatsLocked(true);
          setTimeout(openServerSafetyGuide, 80);
        }
      };
      const text = document.createElement('span');
      text.textContent = s.label;
      row.append(cb, text);
      container.appendChild(row);
    });
  }

  function settingsShowAll() {
    SECTIONS.filter(s => !s.lockedOn).forEach(s => {
      secVisible[s.id] = true;
      persistSectionVisibility(s.id, true);
    });
    applySectionVisibility();
    renderSettingsSections();
  }

  function settingsReset() {
    SECTIONS.filter(s => !s.lockedOn).forEach(s => {
      secVisible[s.id] = true;
      try { localStorage.removeItem(SECTION_STORAGE_PREFIX + s.id); } catch (e) {}
    });
    applySectionVisibility();
    renderSettingsSections();
    const picker = document.getElementById('settings-accent');
    if (typeof setAccent === 'function') setAccent('#4fa3e0');
    if (picker) picker.value = '#4fa3e0';
    toast('Settings reset', 'ok');
  }

  document.addEventListener('DOMContentLoaded', () => {
    applySectionVisibility();
    const gear = document.getElementById('gear-btn');
    if (gear) gear.addEventListener('click', openSettings);
    const overlay = document.getElementById('settings-overlay');
    if (overlay) overlay.addEventListener('click', closeSettingsOutside);
    const picker = document.getElementById('settings-accent');
    if (picker) picker.addEventListener('input', e => {
      if (typeof setAccent === 'function') setAccent(e.target.value);
    });
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && document.getElementById('settings-overlay')?.getAttribute('aria-hidden') === 'false') closeSettings();
    });
  });


  // ── Dockable activity log ─────────────────────────────────────────────
  function toggleActivityDock() {
    const dock=document.getElementById('activity-log-dock');
    if(dock) dock.classList.toggle('open');
  }

  function clearActivityDock() {
    const dock=document.getElementById('activity-log-dock-body');
    if(dock) dock.innerHTML='<div class="dock-log-empty">Log cleared.</div>';
  }

  // ── Activity ───────────────────────────────────────────────────────
  // Mirrors C# LoadActivity(ushort index): writes index as UInt16 to 4 addresses.
  // comboBox2 items are 1-indexed: item at index 0 = activity 1, etc.
  const ACT_ADDRS = [0x30656E6E, 0x306570A4, 0x30686914, 0x336CBC46];

  const ALL_ACTIVITIES = [
    "1 Tower","2 Iron Banner (Tower)","3 Queen's Wrath (Tower)","4 Festival of the Lost (Tower)",
    "5 SRL","6 The Dawning (Tower)","7 Crimson Days (Tower)","8 Patrol Cosmodrome (Honeydew Error)",
    "9 Patrol Moon (Honeydew Error)","10 Patrol Venus (Honeydew Error)","11 Patrol Mars (Honeydew Error)",
    "12 Vault of Glass (Level 26)","13 Vault of Glass (Level 30) (Heroic)","14 Vault of Glass (Level 31) (Epic)",
    "15 Crota's End (Level 30)","16 Crota's End (Level 33) (Heroic)",
    "17 Weekly NF (Level 32) (Epic, Nightfall, Arc, Juggler, Brawler)","18 Weekly NF (Level 32) (Epic, Angry, Lightswitch, Brawler, Nightfall)",
    "19 Weekly NF (Level 32) (Epic, Arc, Solar, Small Arms, Nightfall)","20 Weekly NF (Level 32) (Epic, Arc, Nightfall, Void, Solar, Grounded)",
    "21 Weekly NF (Level 32) (Epic, Nightfall, Angry, Juggler, Void)","22 Weekly NF (Level 32) (Epic, Nightfall, Arc, Brawler, Solar)",
    "23 Weekly NF (Level 32) (Epic, Nightfall, Juggler, Void, Solar)","24 Weekly NF (Level 32) (Epic, Nightfall, Arc, Specialist, Lightswitch)",
    "25 Weekly NF (Level 32) (Epic, Nightfall, Angry, Brawler, Grounded)","26 Weekly NF (Level 32) (Epic, Nightfall, Void, Solar, Arc)",
    "27 PoE (Level 28)","28 The Forever Eater (Level 32) (PoE)","29 The Forever Eater (Level 34) (PoE)",
    "30 Broken Legion (Level 32) (PoE)","31 Broken Legion (Level 34) (PoE)","32 Machine Wrath (Level 32) (PoE)",
    "33 Machine Wrath (Level 34) (PoE)","34 Cult of The Worm (Level 32)","35 Cult of The Worm (Level 34)",
    "36 Urrox's Grudge (Level 32) (PoE)","37 Urrox's Grudge (Level 34)","38 Skolas's Revenge (Level 35) (PoE)",
    "39 Vestian Outpost","40 The Lighthouse","41 Weekly Heroic Strike (Level 26) (Heroic, Juggler)",
    "42 Weekly Heroic Strike (Level 30) (Heroic, Juggler)","43 Weekly Heroic Strike (Level 32) (Heroic, Juggler)",
    "44 Weekly Heroic Strike (Level 26) (Heroic, Specialist)","45 Weekly Heroic Strike (Level 30) (Heroic, Specialist)",
    "46 Weekly Heroic Strike (Level 32) (Heroic, Specialist)","47 Weekly Heroic Strike (Level 26) (Heroic, Brawler)",
    "48 Weekly Heroic Strike (Level 30) (Heroic, Brawler)","49 Weekly Heroic Strike (Level 32) (Heroic, Brawler)",
    "50 Weekly Heroic Strike (Level 26) (Heroic, Small Arms)","51 Weekly Heroic Strike (Level 30) (Heroic, Small Arms)",
    "52 Weekly Heroic Strike (Level 32) (Heroic, Small Arms)","53 Weekly Heroic Strike (Level 26) (Heroic, Grounded)",
    "54 Weekly Heroic Strike (Level 30) (Heroic, Grounded)","55 Weekly Heroic Strike (Level 32) (Heroic, Grounded)",
    "56 Weekly Heroic Strike (Level 26) (Heroic, Angry)","57 Weekly Heroic Strike (Level 30) (Heroic, Angry)",
    "58 Weekly Heroic Strike (Level 32) (Heroic, Angry)","59 Weekly Heroic Strike (Level 26) (Heroic, Juggler)",
    "60 Weekly Heroic Strike (Level 30) (Heroic, Juggler)","61 Weekly Heroic Strike (Level 32) (Heroic, Juggler)",
    "62 Weekly Heroic Strike (Level 26) (Heroic, Solar)","63 Weekly Heroic Strike (Level 30) (Heroic, Solar)",
    "64 Weekly Heroic Strike (Level 32) (Heroic, Solar)","65 Weekly Heroic Strike (Level 26) (Heroic, Grounded)",
    "66 Weekly Heroic Strike (Level 30) (Heroic, Grounded)","67 Weekly Heroic Strike (Level 32) (Heroic, Grounded)",
    "68 Weekly Heroic Strike (Level 26) (Heroic, Void)","69 Weekly Heroic Strike (Level 30) (Heroic, Void)",
    "70 Weekly Heroic Strike (Level 32) (Heroic, Void)","71 Vanguard Nightfall (Light 320)","72 Fallen SABER (Light 320)",
    "73 The Sunless Cell (Light 320)","74 Shield Brother (Light 320)","75 Dust Palace (Light 320)",
    "76 Cerberus Vae III (Light 320)","77 The Undying Mind (Light 320)","78 The Devil's Lair (Light 320)",
    "79 The Will of Crota (Light 320)","80 Dust Palace (Light 320)","81 Cerberus Vae III (Light 320)",
    "82 The Undying Mind (Light 320)","83 The Summoning Pits (Light 320)","84 The Shadow Thief (Light 320)",
    "85 The Nexus (Light 320)","86 Winter's Run (Light 320)","87 Blighted Chalice (Light 320)",
    "88 Winter's Run (Light 320)","89 Echo Chamber (Light 320)","90 Cosmodrome Patrol",
    "91 Dreadnaught Patrol","92 Mars Patrol","93 Moon Patrol","94 Venus Patrol",
    "95 Clash (In-Game)","96 Salvage (In-Game)","97 Skirmish (In-Game)","98 Rumble (In-Game)",
    "99 Control (In-Game)","100 Rift (In-Game)","101 Trials of Osiris (Light 290)","102 Iron Banner (Level 40)",
    "103 Iron Banner (Level 40)","104 Iron Banner (Level 40)","105 Inferno Clash","106 Inferno Control",
    "107 Mayhem Clash (In-Game)","108 Zone Control (In-Game)","109 Elimination (In-Game)",
    "110 Arsenal","111 Doubles (In-Game)","112 Crimson Doubles","113 Inferno Rumble",
    "114 Inferno Skirmish","115 Inferno Salvage","116 Inferno Doubles","117 Mayhem Rumble (In-Game)",
    "118 Inferno 3v3","119 Inferno 6v6 (In-Game)","120 Freelance 3v3","121 Freelance 6v6",
    "122 Classic 6v6 (In-Game)","123 Classic 3v3 (In-Game)","124 Classic FFA (In-Game)","125 SRL",
    "126 SRL (Solo Que)","127 Rumble (Honeydew Error)","128 Rumble (The Anomaly)","129 Rumble (Firebase Delphi)",
    "130 Rumble (Twilight Gap)","131 Rumble (Exodus Blue)","132 Rumble (Rusted Lands)","133 Rumble (Shores of Time)",
    "134 Rumble (Bastion)","135 Rumble (First Light)","136 Rumble (Blind Watch)","137 Rumble (Asylum)",
    "138 Rumble (The Burning Shrine)","139 Rumble (The Cauldron)","140 Rumble (Skyshock)","141 Rumble (Pantheon)",
    "142 Rumble (Thieves Den)","143 Rumble (The Timekeeper)","144 Rumble (Black Shield)","145 Rumble (Widow's Court)",
    "146 Rumble (Sector 618)","147 Rumble (The Drifter)","148 Rumble (Crossroads)","149 Rumble (Memento)",
    "150 Rumble (Bannerfall)","151 Rumble (The Dungeons)","152 Rumble (Frontier)","153 Rumble (Vertigo)",
    "154 Rumble (Cathedral of Dusk)","155 Rumble (Campus Martius)","156 Rumble (Infinite Descent)",
    "157 King's Fall (290 Light)","158 King's Fall (310 Light)","159 The Lighthouse",
    "160 Destiny Credits (Cinematic)","161 A Guardian Rises (Level 1) (Cinematic)","162 A Guardian Rises (Level 1)",
    "163 Restoration (Level 2)","164 The Speaker (Level 2) (Cinematic)","165 The Dark Within (Level 3)",
    "166 The Dark Within (Light 240) (Heroic)","167 The Warmind (Level 4)","168 The Warmind (Light 240) (Heroic)",
    "169 The Last Array (Level 6)","170 The Last Array (Light 240) (Heroic)","171 The Dark Beyond (Level 7)",
    "172 The Dark Beyond (Light 240) (Heroic)","173 The World's Grave (Level 8)","174 The World's Grave (Light 240) (Heroic)",
    "175 The Sword of Crota (Level 8)","176 The Sword of Crota (Light 240) (Heroic)","177 Chamber of Night (Level 9)",
    "178 Chamber of Night (Light 240) (Heroic)","179 Shrine of Oryx (Level 9)","180 Shrine of Oryx (Light 240) (Heroic)",
    "181 A Stranger's Call (Level 10)","182 A Stranger's Call (Light 240) (Heroic)","183 The Awoken (Level 10) (Cinematic)",
    "184 Ishtar Collective (Level 11)","185 Ishtar Collective (Light 240) (Heroic)","186 The Archive (Level 12)",
    "187 The Archive (Light 240) (Heroic)","188 Scourge of Winter (Level 14)","189 Scourge of Winter (Light 240) (Heroic)",
    "190 Eye of a Gate Lord (Level 12)","191 Eye of a Gate Lord (Light 240) (Heroic)","192 A Key Awaits (Level 13) (Cinematic)",
    "193 Exclusion Zone (Level 15)","194 Exclusion Zone (Light 240) (Heroic)","195 The Buried City (Level 17)",
    "196 The Buried City (Light 240) (Heroic)","197 The Garden's Spire (Level 16)","198 The Garden's Spire (Light 240) (Heroic)",
    "199 A Rising Tide (Level 17)","200 A Rising Tide (Light 240) (Heroic)","201 The Black Garden (Level 18)",
    "202 The Black Garden (Light 240) (Heroic)","203 A Hero's Return (Level 18) (Cinematic)","204 Fist of Crota (Level 10)",
    "205 Fist of Crota (Light 240) (Heroic)","206 Siege of The Warmind (Level 12)","207 Siege of The Warmind (Light 240) (Heroic)",
    "208 The Wakening (Level 14)","209 The Wakening (Light 240) (Heroic)","210 Ritual of Sacrifice (Level 26)",
    "211 A Kell Rising (Level 16)","212 A Kell Rising (Light 240) (Heroic)","213 The Ruling House (Level 20)",
    "214 The Ruling House (Light 240) (Heroic)","215 Queen's Ransom (Level 24)","216 Queen's Ransom (Light 240) (Heroic)",
    "217 The Silent Fang (Level 18)","218 The Silent Fang (Light 240) (Heroic)","219 The Archon-Slayer (Level 28)",
    "220 Wolves Gambit (Level 22)","221 Wolves Gambit (Light 240) (Heroic)","222 Tenebrous Tunnels (Light 250)",
    "223 Shadows at Twilight (Light 270)","224 Entropy's Pinnacle (Level 40)","225 Entropy's Pinnacle (Light 240) (Heroic)",
    "226 Hangar Stakes (Level 25) (Cinematic)","227 Lost to Light (Level 31)","228 Oryx Triumphant (Level 20) (Cinematic)",
    "229 The Taken King: Prelude (Level 1) (Cinematic)","230 [No Name] (Level 1)","231 The Coming War (Level 25)",
    "232 The Coming War (Level 25)","233 The Coming War (Light 240) (Heroic)","234 Dread Patrol (Level 35)",
    "235 Lost to Light (Level 31)","236 Lost to Light (Light 240) (Heroic)","237 Summoner's Circle (Light 270)",
    "238 Cayde's Stash (Level 26)","239 Cayde's Stash (Light 240) (Heroic)","240 Shadow Call (Light 270)",
    "241 The Prime's Path (Level 40)","242 The Prime's Path (Light 240) (Heroic)","243 Knock Knock (Level 40)",
    "244 A Ranger Found (Level 25)","245 Bound by Fire (Level 25)","246 Bottle the Arc (Level 25)",
    "247 Fear's Embrace (Light 260)","248 Fear's Embrace (Light 260)","249 Fear's Embrace (Light 270)",
    "250 Blood in The Garden (Light 290)","251 Enemy of My Enemy (Level 30)","252 Enemy of My Enemy (Light 240) (Heroic)",
    "253 A New Den (Level 39)","254 A New Den (Light 240) (Heroic)","255 A Spark in Shadow (Level 25)",
    "256 Paradox (Level 38)","257 Paradox (Light 240) (Heroic)","258 Blighted Coven (Level 40)",
    "259 Blighted Coven (Light 240) (Heroic)","260 The First Firewall (Light 260)","261 A March of Fire (Level 25)",
    "262 A Ranger Lost (Level 25)","263 Kings of Decay (Level 40)","264 Kings of Decay (Light 240) (Heroic)",
    "265 Last Rites (Level 33)","266 Last Rites (Light 240) (Heroic)","267 Outbound Signal (Level 38)",
    "268 Outbound Signal (Level 38)","269 Outbound Signal (Light 240) (Heroic)","270 Regicide (Level 34)",
    "271 Regicide (Light 240) (Heroic)","272 The Dreadnaught (Level 28)","273 The Dreadnaught (Light 240) (Heroic)",
    "274 The Promethean Code (Level 32)","275 The Promethean Code (Light 240) (Heroic)","276 The Promethean Code (Light 240)",
    "277 Vanguard Legacy (Level 20)","278 Vanguard Strikes (Level 36)","279 The Will of Crota (Level 36)",
    "280 Fallen SABER (Level 36)","281 The Sunless Cell (Level 36)","282 Shield Brothers (Level 36)",
    "283 Echo Chamber (Level 36)","284 The Shadow Thief (Level 36)","285 Fallen SABER (Level 36)",
    "286 Fallen SABER (Light 280) (Epic)","287 Fallen SABER (Level 40)","288 The Sunless Cell (Level 36)",
    "289 The Sunless Cell (Light 300) (Epic)","290 The Sunless Cell (Level 40)","291 Shield Brothers (Level 40)",
    "292 Shield Brothers (Light 290) (Epic, Specialist)","293 Shield Brothers (Level 40)","294 Echo Chamber (Level 36)",
    "295 Dust Palace (Level 36)","296 Cerberus Vae III (Level 36)","297 The Undying Mind (Level 40) (Heroic)",
    "298 The Undying Mind (Level 36)","299 The Devil's Lair (Level 8)","300 The Will of Crota (Level 16)",
    "301 Dust Palace (Level 20)","302 Cerberus Vae III (Level 20)","303 The Undying Mind (Level 20)",
    "304 The Summoning Pits (Level 10)","305 The Shadow Thief (Level 20)","306 The Nexus (Level 14)",
    "307 Winter's Run (Level 14)","308 The Devil's Lair (Level 20)","309 The Will of Crota (Level 20)",
    "310 Dust Palace (Level 20)","311 Cerberus Vae III (Level 20)","312 The Undying Mind (Level 20)",
    "313 The Summoning Pits (Level 20)","314 The Shadow Thief (Level 20)","315 The Nexus (Level 20)",
    "316 Winter's Run (Level 20)","317 Vanguard Heroic Strikes (Light 260)","318 The Will of Crota (Light 260)",
    "319 Fallen SABER (Light 260)","320 The Sunless Cell (Light 260)","321 Shield Brothers (Light 260)",
    "322 Echo Chamber (Light 260)","323 Dust Palace (Light 260)","324 Cerberus Vae III (Light 260)",
    "325 The Undying Mind (Light 260)","326 The Shadow Thief (Light 260)","327 Blighted Chalice (Light 260)",
    "328 Winter's Run (Light 260)","329 PoE (Light 260)","330 CoE (Light 320)",
    "331 PoE (Light 260) (No Taken)","332 PoE: Taken (Light 260)","333 CoE (Light 320) (No Modifiers)",
    "334 Pretender to the Throne (Level 36)","335 Blighted Chalice (Level 36)","336 Blighted Chalice (Level 40)",
    "337 Winter's Run (Level 36)"
  ];

  // Populate the select on load
  function populateActivitySelect(filter) {
    const sel = document.getElementById('act-select');
    if (!sel) return;
    const prev = sel.value;
    sel.innerHTML = '';
    // Blank placeholder so nothing is preselected
    const placeholder = document.createElement('option');
    placeholder.value = '';
    placeholder.textContent = '— Select an activity —';
    placeholder.disabled = true;
    placeholder.selected = !prev; // selected only when no previous value
    sel.appendChild(placeholder);
    ALL_ACTIVITIES.forEach((name, i) => {
      if (!filter || name.toLowerCase().includes(filter.toLowerCase())) {
        const opt = document.createElement('option');
        opt.value = i + 1; // 1-indexed activity ID
        opt.textContent = name;
        sel.appendChild(opt);
      }
    });
    if (prev) sel.value = prev;
  }

  function filterActivities() {
    const q = document.getElementById('act-search').value;
    populateActivitySelect(q);
  }

  // Mirrors C# LoadActivity(ushort index): WriteUInt16 to all 4 addresses.
  // The four activity addresses include two unaligned ones (byteOffset=2):
  //   0x30656E6E → aligned at 0x30656E6C, UInt16 in upper word (bytes 2-3) — wait, offset 2 = lower word
  //   0x336CBC46 → aligned at 0x336CBC44, UInt16 in bytes 2-3
  // Previously used read-modify-write via ps3getmem, but that read fails on
  // file:// origin (CORS) and returned garbage, corrupting the 4-byte word.
  //
  // Fix: the adjacent bytes at all 4 addresses are constant zero padding in
  // Destiny's activity struct — safe to write 00 for the non-owned half.
  // This matches what the C# SoulsAPI WriteUInt16 effectively does on a
  // fresh load where the surrounding bytes are already zeroed.
  async function ps3writeU16(addr, value) {
    const alignedAddr  = addr & ~3;
    const byteOffset   = addr - alignedAddr; // 0 or 2
    const hi = (value >> 8) & 0xFF;
    const lo =  value       & 0xFF;

    // Build the 4-byte word: zero-fill the non-owned half
    let bytes = [0, 0, 0, 0];
    bytes[byteOffset]     = hi;
    bytes[byteOffset + 1] = lo;

    const newHex      = bytes.map(b => b.toString(16).padStart(2, '0').toUpperCase()).join('');
    const alignedHex  = alignedAddr.toString(16).toUpperCase().padStart(8, '0');
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${alignedHex}&val=${newHex}`);
  }

  async function loadActivity(index) {
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      for (const addr of ACT_ADDRS) {
        await ps3writeU16(addr, index);
      }
      const name = ALL_ACTIVITIES[index - 1] || `Activity ${index}`;
      log(`Activity loaded: ${name}`, 'ok');
      toast(name.length > 30 ? name.slice(0,30)+'…' : name, 'ok');
    } catch(e) {
      log(`LoadActivity failed: ${e.message}`, 'err');
      toast('Load failed', 'err');
    }
  }

  async function loadActivityFromSelect() {
    const sel = document.getElementById('act-select');
    if (!sel || !sel.value) { toast('Select an activity first', 'err'); return; }
    await loadActivity(parseInt(sel.value));
  }

  // Mirrors C# button15: reads current activity index from first address.
  // Read 4 bytes and take the first 2 (big-endian UInt16) — reading len=2 via
  // PS3MAPI can return a padded/misaligned response that inflates the value.
  async function readCurrentActivity() {
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      // ReadUInt16 on PS3 (big-endian): read 2 bytes — the index is in the
      // FIRST 2 bytes (hex chars 0–3), not the last 2.  The old slice(4,8)
      // was reading the wrong half and producing garbage values.
      const hex = await ps3getmem(ACT_ADDRS[0], 2);
      const idx = parseInt(hex.slice(0, 4), 16); // big-endian UInt16
      if (!idx || idx > ALL_ACTIVITIES.length) {
        log(`Read activity: unexpected index ${idx} — are you in a game?`, 'err');
        toast('Unexpected activity index', 'err');
        return;
      }
      const name = ALL_ACTIVITIES[idx - 1] || `Unknown (${idx})`;
      log(`Current activity: [${idx}] ${name}`, 'ok');
      toast(`[${idx}] ${name.slice(0,28)}`, 'ok');
      // Highlight in dropdown
      const sel = document.getElementById('act-select');
      document.getElementById('act-search').value = '';
      populateActivitySelect('');
      sel.value = idx;
    } catch(e) {
      log(`Read activity failed: ${e.message}`, 'err');
      toast('Read failed', 'err');
    }
  }

  document.addEventListener('DOMContentLoaded', () => populateActivitySelect(''));


  // ── Toggle cheats with XMB notification ──────────────────────────────────
  // togFire / togFireMulti: like fire/fireMulti but always toggle, always notify,
  // and visually mark the button as active/inactive.
  const cheatState = {};

  // ── Cheat active-state persistence ────────────────────────────────────────
  // The "active-cheat" highlight previously lived only in memory, so it reset
  // on every page reload even though the actual PS3 memory writes (and the
  // cheat's real in-game effect) were untouched. This persists which toggle
  // buttons are currently shown as active and restores both the highlight and
  // the internal on/off trackers (cheatState, antiMMState, srvMode) on load,
  // so the next click sends the correct opposite value instead of resending ON.
  const ACTIVE_CHEATS_KEY = 'rtm_active_cheats';

  function loadActiveCheatIds() {
    try { return JSON.parse(localStorage.getItem(ACTIVE_CHEATS_KEY) || '{}'); } catch (e) { return {}; }
  }

  function persistActiveCheatIds() {
    const state = {};
    document.querySelectorAll('[id^="tog-"], [id^="srv-"]').forEach(el => {
      if (el.classList.contains('active-cheat')) state[el.id] = true;
    });
    try { localStorage.setItem(ACTIVE_CHEATS_KEY, JSON.stringify(state)); } catch (e) {}
  }

  function restoreActiveCheatUI() {
    const saved = loadActiveCheatIds();
    Object.keys(saved).forEach(id => {
      const el = document.getElementById(id);
      if (el) el.classList.add('active-cheat');
    });

    // Rebuild each toggle's internal tracker from the restored UI state.
    document.querySelectorAll('[id^="tog-"]').forEach(el => {
      if (el.classList.contains('active-cheat') && typeof getCheatLabel === 'function') {
        cheatState[getCheatLabel(el)] = true;
      }
    });
    if (saved['tog-anti-mm']) antiMMState = true;

    const ropkEl = document.getElementById('ropk-controls');
    if (saved['srv-void'])      srvMode = 'void';
    else if (saved['srv-ropk']) { srvMode = 'ropk'; if (ropkEl) ropkEl.style.display = ''; }
    else if (saved['srv-off'])  srvMode = 'off';
  }

  function setupCheatPersistenceObserver() {
    const observer = new MutationObserver(persistActiveCheatIds);
    document.querySelectorAll('[id^="tog-"], [id^="srv-"]').forEach(el => {
      observer.observe(el, { attributes: true, attributeFilter: ['class'] });
    });
  }

  document.addEventListener('DOMContentLoaded', () => {
    restoreActiveCheatUI();
    setupCheatPersistenceObserver();
  });

  // ── PS3HEN Mode ───────────────────────────────────────────────────────────
  // When enabled, adds a delay between every PS3MAPI write to prevent
  // overwhelming HEN's memory write handler, which is slower than CFW.
  let henMode = false;
  try {
    const savedHenMode = localStorage.getItem('henMode');
    if (savedHenMode !== null) {
      henMode = savedHenMode === '1';
    } else {
      // Migrate the older per-session preference if it exists.
      henMode = sessionStorage.getItem('henMode') === '1';
      localStorage.setItem('henMode', henMode ? '1' : '0');
    }
  } catch (e) {
    try { henMode = sessionStorage.getItem('henMode') === '1'; } catch (_) {}
  }
  const HEN_WRITE_DELAY = 400; // ms between writes in HEN mode

  function henDelay() {
    return henMode ? new Promise(r => setTimeout(r, HEN_WRITE_DELAY)) : Promise.resolve();
  }

  function applyHenToggleUI() {
    const track  = document.getElementById('hen-toggle-track');
    const thumb  = document.getElementById('hen-toggle-thumb');
    const label  = document.getElementById('hen-toggle-label');
    if (!track) return;
    if (henMode) {
      track.style.background    = 'rgba(62,207,116,0.15)';
      track.style.borderColor   = 'var(--success)';
      thumb.style.background    = 'var(--success)';
      thumb.style.transform     = 'translateX(16px)';
      label.style.color         = 'var(--success)';
      label.textContent         = 'ON';
    } else {
      track.style.background    = 'var(--surface)';
      track.style.borderColor   = 'var(--border)';
      thumb.style.background    = 'var(--muted)';
      thumb.style.transform     = 'translateX(0)';
      label.style.color         = 'var(--muted)';
      label.textContent         = 'OFF';
    }
  }

  function toggleHenMode() {
    henMode = !henMode;
    try { localStorage.setItem('henMode', henMode ? '1' : '0'); } catch (e) {}
    try { sessionStorage.setItem('henMode', henMode ? '1' : '0'); } catch (e) {}
    applyHenToggleUI();
    log(`PS3HEN mode ${henMode ? 'ON' : 'OFF'} — write delay ${henMode ? HEN_WRITE_DELAY + 'ms' : 'disabled'}`, henMode ? 'ok' : '');
    toast(`HEN mode ${henMode ? 'ON' : 'OFF'}`, henMode ? 'ok' : '');
  }

  // Apply HEN toggle UI on load
  applyHenToggleUI();


  // ── RTM Category Tabs ───────────────────────────────────────────────────
  const RTM_TAB_KEY = 'rtm_active_tab';

  const RTM_TAB_GROUPS = {
    home: [
      'sec-connection',
      'sec-quick-access',
      'sec-character-utils'
    ],
    mods: [
      'sec-main-cheats',
      'sec-server-cheats',
      'sec-damage-tuning'
    ],
    player: [
      'sec-teleport',
      'sec-character-utils',
      'sec-gamespeed',
      'sec-fov'
    ],
    tools: [
      'sec-custom-offset',
      'sec-useless'
    ]
  };

  function getRTMTab() {
    const saved = localStorage.getItem(RTM_TAB_KEY) || 'home';
    return ['home','mods','player','tools','all'].includes(saved) ? saved : 'home';
  }

  function applyRTMTab() {
    const activeTab = getRTMTab();
    const groupedIds = new Set(Object.values(RTM_TAB_GROUPS).flat());

    groupedIds.forEach(id => {
      const el = document.getElementById(id);
      if (!el) return;
      const belongs = activeTab === 'all' || (RTM_TAB_GROUPS[activeTab] || []).includes(id);
      el.classList.toggle('rtm-tab-hidden', !belongs);
    });

    ['home','mods','player','tools','all'].forEach(tab => {
      document.getElementById(`rtm-tab-${tab}`)?.classList.toggle('active', tab === activeTab);
    });
  }

  function setRTMTab(tab) {
    if (!['home','mods','player','tools','all'].includes(tab)) return;
    localStorage.setItem(RTM_TAB_KEY, tab);
    applyRTMTab();
    window.scrollTo({ top: 0, behavior: 'smooth' });
  }

  window.setRTMTab = setRTMTab;
  document.addEventListener('DOMContentLoaded', applyRTMTab);

  // ── Light / Dark appearance ─────────────────────────────────────────────
  const RTM_APPEARANCE_KEY = 'rtm_appearance';

  function getRTMAppearance() {
    return localStorage.getItem(RTM_APPEARANCE_KEY) || 'dark';
  }

  function applyRTMAppearance() {
    const mode = getRTMAppearance();
    document.body.classList.toggle('light-mode', mode === 'light');

    const darkBtn = document.getElementById('appearance-dark');
    const lightBtn = document.getElementById('appearance-light');
    if (darkBtn) darkBtn.classList.toggle('active', mode === 'dark');
    if (lightBtn) lightBtn.classList.toggle('active', mode === 'light');
  }

  function setRTMAppearance(mode) {
    if (!['dark','light'].includes(mode)) return;
    localStorage.setItem(RTM_APPEARANCE_KEY, mode);
    applyRTMAppearance();
    if (typeof toast === 'function') toast(`Appearance: ${mode === 'light' ? 'Light' : 'Dark'}`, 'ok');
  }

  window.setRTMAppearance = setRTMAppearance;
  document.addEventListener('DOMContentLoaded', applyRTMAppearance);

  // ── Compact / Detailed UI mode ──────────────────────────────────────────
  const UI_MODE_KEY = 'rtm_ui_mode';

  function getUIMode() {
    return localStorage.getItem(UI_MODE_KEY) || 'compact';
  }

  function applyUIMode() {
    const mode = getUIMode();
    document.body.classList.toggle('compact-ui', mode === 'compact');
    document.body.classList.toggle('detailed-ui', mode === 'detailed');

    const compact = document.getElementById('ui-mode-compact');
    const detailed = document.getElementById('ui-mode-detailed');
    if (compact) compact.classList.toggle('active', mode === 'compact');
    if (detailed) detailed.classList.toggle('active', mode === 'detailed');
  }

  function setUIMode(mode) {
    if (!['compact','detailed'].includes(mode)) return;
    localStorage.setItem(UI_MODE_KEY, mode);
    applyUIMode();
    toast(`Interface: ${mode === 'compact' ? 'Compact' : 'Detailed'}`, 'ok');
  }

  window.setUIMode = setUIMode;
  document.addEventListener('DOMContentLoaded', applyUIMode);

  // ── Cheat status / feature label system ─────────────────────────────────
  const CHEAT_INFO = {
    'tog-god-mode': {
      feature:'stable', label:'Stable',
      description:'Prevents your Guardian from taking normal damage.',
      notes:'Instance Leader can affect how this behaves around enemies. Treat it cautiously when other players are present.'
    },
    'tog-ohk': {
      feature:'stable', label:'Stable',
      description:'Makes most enemies die from a single hit.',
      notes:'Instance Leader can change the behaviour of this option. It also conflicts with 4× Damage, so only one should be considered active.'
    },
    'tog-ammo': {
      feature:'stable', label:'Stable',
      description:'Keeps weapons supplied with ammunition.',
      notes:'Current known issue: this option may not restore correctly when switched off.'
    },
    'tog-power': {
      feature:'experimental', label:'Experimental',
      description:'Alternative unlimited-ability/power behaviour.',
      notes:'Use Unlimited Abilities instead when you want the more established option.'
    },
    'tog-bullets': {
      feature:'stable', label:'Stable',
      description:'Stops affected bullets and projectiles from moving normally.',
      notes:'Can produce unusual behaviour in encounters with lots of projectiles.'
    },
    'tog-notarget': {
      feature:'stable', label:'Stable',
      description:'Prevents enemies from normally targeting your Guardian.',
      notes:'Enemy behaviour can still vary between encounters.'
    },
    'tog-instant-respawn': {
      feature:'stable', label:'Stable',
      description:'Removes the usual respawn delay after death.',
      notes:'Some activities can still enforce their own death or wipe behaviour.'
    },
    'tog-no-recoil': {
      feature:'stable', label:'Stable',
      description:'Removes weapon recoil for steadier aiming.',
      notes:'Applies to supported weapon behaviour while enabled.'
    },
    'tog-unl-abilities': {
      feature:'stable', label:'Stable',
      description:'Keeps abilities available without the normal recharge wait.',
      notes:'This is the preferred unlimited-ability option in the current RTM.'
    },
    'tog-rapid-fire': {
      feature:'stable', label:'Stable',
      description:'Makes supported weapons fire much faster.',
      notes:'Some weapons may behave differently from others.'
    },
    'tog-no-collision': {
      feature:'stable', label:'Stable',
      description:'Lets you pass through some walls and solid objects.',
      notes:'Physics objects can behave badly with this enabled. Disable it if movement becomes unstable.'
    },
    'tog-no-gravity': {
      feature:'stable', label:'Stable',
      description:'Removes normal gravity from your Guardian.',
      notes:'Movement can become difficult to control in some spaces.'
    },
    'tog-no-floors': {
      feature:'stable', label:'Stable',
      description:'Lets you pass through floor surfaces.',
      notes:'You can fall out of playable areas. Keep a saved teleport position available.'
    },
    'tog-static-actors': {
      feature:'stable', label:'Stable',
      description:'Stops affected characters or actors from moving normally.',
      notes:'Encounter scripting may react unpredictably while actors are frozen.'
    },
    'tog-unl-sparrow': {
      feature:'stable', label:'Stable',
      description:'Keeps Sparrow boost available while enabled.',
      notes:'Intended for supported Sparrow movement.'
    },
    'tog-super-speed': {
      feature:'stable', label:'Stable',
      description:'Greatly increases movement speed.',
      notes:'High movement speed can make precise platforming harder.'
    },
    'tog-variable-buff': {
      feature:'experimental', label:'Experimental',
      description:'Applies the RTM’s miscellaneous power-boost behaviour.',
      notes:'This option is intentionally left as a discovery/testing feature.'
    },
    'tog-no-pushbacks': {
      feature:'experimental', label:'Experimental',
      description:'Prevents pushback barriers from moving you backwards.',
      notes:'Also affects some pull/push behaviour and may change encounter movement.'
    },
    'tog-warpriest': {
      feature:'experimental', label:'Experimental',
      description:'Changes Warpriest immunity behaviour while enabled.',
      notes:'Encounter-specific. Disable it when you are finished testing the Warpriest encounter.'
    },
    'tog-sword-energy': {
      feature:'nonfunctional', label:'Non-functional',
      description:'Intended to keep sword energy from running out.',
      notes:'This feature is currently marked non-functional.'
    },
    'tog-anti-mm': {
      feature:'stable', label:'Stable',
      description:'Prevents normal matchmaking from finding or joining lobbies.',
      notes:'Only enable from Orbit or the Tower. Do not activate while matchmaking is already in progress.'
    },
    'srv-void': {
      feature:'volatile', label:'Volatile',
      description:'Applies the Void Enemies server-cheat mode.',
      notes:'Server Cheats are highly volatile. Wait for confirmation before changing modes again.'
    },
    'srv-ropk': {
      feature:'volatile', label:'Volatile',
      description:'Moves affected enemies relative to your position using the X, Y and Z controls.',
      notes:'Server Cheats are highly volatile. Make small offset changes and wait for each update to complete.'
    },
    'srv-4x-damage': {
      feature:'stable', label:'Stable',
      description:'Applies a four-times damage mode.',
      notes:'Conflicts with One-Hit Enemies. Enabling one should be treated as replacing the other.'
    },
    'srv-off': {
      feature:'stable', label:'Safe State',
      description:'Restores the Server Cheats mode to its normal off state.',
      notes:'Use this when you want to stop the active server-cheat mode.'
    }
  };

  const cheatRuntimeStatus = Object.create(null);

  function normalizeCheatStatus(state) {
    return ['unknown','on','off','applying','failed'].includes(state) ? state : 'off';
  }

  function cheatStatusText(state) {
    return ({
      unknown:'UNKNOWN',
      on:'ON REQUESTED',
      off:'OFF REQUESTED',
      applying:'APPLYING…',
      failed:'FAILED'
    })[normalizeCheatStatus(state)];
  }

  function updateCheatStatus(idOrEl, state) {
    const stateNorm = normalizeCheatStatus(state);
    const id = typeof idOrEl === 'string' ? idOrEl : idOrEl?.id;
    if (!id) return;
    cheatRuntimeStatus[id] = stateNorm;

    document.querySelectorAll(`[id="${CSS.escape(id)}"]`).forEach(el => {
      const badge = el.querySelector('.cheat-status-label');
      if (badge) {
        badge.dataset.state = stateNorm;
        badge.textContent = cheatStatusText(stateNorm);
      }
      el.classList.toggle('cheat-status-failed', stateNorm === 'failed');
    });

  }

  function setCheatStatusFromButton(btn, state) {
    if (btn?.id) updateCheatStatus(btn.id, state);
  }

  function decorateCheatButton(btn) {
    if (!btn || !btn.id || !CHEAT_INFO[btn.id]) return;
    if (btn.querySelector('.cheat-meta-row')) return;

    const info = CHEAT_INFO[btn.id];
    const row = document.createElement('span');
    row.className = 'cheat-meta-row';

    const feature = document.createElement('span');
    feature.className = `cheat-feature-label ${info.feature}`;
    feature.textContent = info.label;

    const status = document.createElement('span');
    status.className = 'cheat-status-label';
    const state = cheatRuntimeStatus[btn.id] || 'unknown';
    status.dataset.state = state;
    status.textContent = cheatStatusText(state);

    row.append(feature, status);
    btn.appendChild(row);
  }

  function decorateCheatButtons() {
    Object.keys(CHEAT_INFO).forEach(id => {
      document.querySelectorAll(`[id="${CSS.escape(id)}"]`).forEach(decorateCheatButton);
    });
  }

  window.updateCheatStatus = updateCheatStatus;

  document.addEventListener('DOMContentLoaded', () => {
    setDamageMultiplierPreset(1);
    decorateCheatButtons();
    updateCheatStatus('tog-sword-energy', 'failed');
    setTimeout(decorateCheatButtons, 100);
    setTimeout(decorateCheatButtons, 600);

    const pinnedGrid = document.getElementById('pinned-grid');
    if (pinnedGrid && typeof MutationObserver !== 'undefined') {
      new MutationObserver(decorateCheatButtons).observe(pinnedGrid, { childList:true, subtree:true });
    }

  });


  // ── Cheat Conflict Detection ─────────────────────────────────────────────
  // Only true memory conflicts are listed here. These options touch the same
  // underlying value/patch and can leave the UI or game state inconsistent
  // if both are treated as active at the same time.
  const CHEAT_CONFLICTS = {
    'tog-ohk': ['srv-4x-damage', 'damage-custom'],
    'srv-4x-damage': ['tog-ohk', 'damage-custom'],
    'damage-custom': ['tog-ohk', 'srv-4x-damage'],

    // Infinite Power and Unlimited Abilities both patch 0x02C36FC.
    'tog-power': ['tog-unl-abilities'],
    'tog-unl-abilities': ['tog-power']
  };

  function setAllButtonsActive(id, active) {
    document.querySelectorAll(`[id="${CSS.escape(id)}"]`).forEach(el => {
      el.classList.toggle('active-cheat', !!active);
    });
  }

  function setCheatLabelState(label, active) {
    if (typeof cheatState !== 'undefined') cheatState[label] = !!active;
  }

  async function disablePowerConflict(id) {
    if (id === 'tog-power') {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=02C36FC&val=C3C30000`);
      await henDelay();
      setCheatLabelState('Infinite Power', false);
      setAllButtonsActive('tog-power', false);
      updateCheatStatus('tog-power', 'off');
      return 'Infinite Power';
    }

    if (id === 'tog-unl-abilities') {
      const offPairs = [
        ['2C1DB4','D03B019C'],
        ['CCB9E8','3063FFFF'],
        ['2C36FC','C3C30000']
      ];
      for (const [addr, val] of offPairs) {
        await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${val}`);
        await henDelay();
      }
      setCheatLabelState('Unlimited Abilities', false);
      setAllButtonsActive('tog-unl-abilities', false);
      updateCheatStatus('tog-unl-abilities', 'off');
      return 'Unlimited Abilities';
    }

    return id;
  }

  function clearDamageConflictUI(exceptId) {
    if (exceptId !== 'tog-ohk') {
      setCheatLabelState('One-Hit Enemies', false);
      setCheatLabelState('One Hit Kill', false);
      setAllButtonsActive('tog-ohk', false);
      updateCheatStatus('tog-ohk', 'off');
    }

    if (exceptId !== 'srv-4x-damage') {
      server4xDamageState = false;
      setAllButtonsActive('srv-4x-damage', false);
      updateCheatStatus('srv-4x-damage', 'off');
    }
  }

  async function resolveCheatConflicts(targetId) {
    const conflicts = CHEAT_CONFLICTS[targetId] || [];
    const disabled = [];

    // Damage controls all replace the same 16-bit value, so the incoming write
    // itself replaces the old mode. We only need to correct state/UI first.
    if (['tog-ohk','srv-4x-damage','damage-custom'].includes(targetId)) {
      if (targetId !== 'tog-ohk' && cheatState['One-Hit Enemies']) disabled.push('One-Hit Enemies');
      if (targetId !== 'srv-4x-damage' && server4xDamageState) disabled.push('4× Damage');
      clearDamageConflictUI(targetId);
    }

    // Power conflict needs real OFF writes because Unlimited Abilities contains
    // additional patches beyond the shared address.
    if (targetId === 'tog-power' && cheatState['Unlimited Abilities']) {
      disabled.push(await disablePowerConflict('tog-unl-abilities'));
    } else if (targetId === 'tog-unl-abilities' && cheatState['Infinite Power']) {
      disabled.push(await disablePowerConflict('tog-power'));
    }

    if (disabled.length) {
      const msg = `Conflict detected: disabled ${disabled.join(' + ')}`;
      log(msg, 'ok');
      toast(msg, 'ok');
    }

    return disabled;
  }

  async function togFire(addr, onVal, offVal, label, btn, size = 4) {
    if (!ip) { setCheatStatusFromButton(btn, 'failed'); toast('Not connected', 'err'); return; }
    const isOn = !cheatState[label];
    if (isOn && btn?.id) await resolveCheatConflicts(btn.id);
    const writeVal = isOn ? onVal : offVal;
    const sizeParam = size !== 4 ? `&size=${size}` : '';
    setCheatStatusFromButton(btn, 'applying');
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${writeVal}${sizeParam}`);
      await henDelay();
      cheatState[label] = isOn;
      btn.classList.toggle('active-cheat', isOn);
      setCheatStatusFromButton(btn, isOn ? 'on' : 'off');
      const stateWord = isOn ? 'ON' : 'OFF';
      log(`${label} ${stateWord} → addr:${addr} val:${writeVal}`, 'ok');
      toast(`${label} ${stateWord}`, 'ok');
      notifyPS3(`${label}: ${stateWord}`);
    } catch(e) {
      setCheatStatusFromButton(btn, 'failed');
      log(`${label} failed: ${e.message}`, 'err');
      toast(`${label} failed`, 'err');
    }
  }

  async function togFireMulti(onPairs, offPairs, label, btn) {
    if (!ip) { setCheatStatusFromButton(btn, 'failed'); toast('Not connected', 'err'); return; }
    const isOn = !cheatState[label];
    if (isOn && btn?.id) await resolveCheatConflicts(btn.id);
    const pairs = isOn ? onPairs : offPairs;
    setCheatStatusFromButton(btn, 'applying');
    try {
      for (const [addr, val] of pairs) {
        await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${val}`);
        await henDelay();
      }
      cheatState[label] = isOn;
      btn.classList.toggle('active-cheat', isOn);
      setCheatStatusFromButton(btn, isOn ? 'on' : 'off');
      const stateWord = isOn ? 'ON' : 'OFF';
      log(`${label} ${stateWord} → ${pairs.length} writes`, 'ok');
      toast(`${label} ${stateWord}`, 'ok');
      notifyPS3(`${label}: ${stateWord}`);
    } catch(e) {
      setCheatStatusFromButton(btn, 'failed');
      log(`${label} failed: ${e.message}`, 'err');
      toast(`${label} failed`, 'err');
    }
  }

  // togFireFloat: toggle a single float address ON/OFF
  // onFloat/offFloat are JS number strings e.g. '0', '-1', '1'
  async function togFireFloat(addr, onFloat, offFloat, label, btn) {
    if (!ip) { setCheatStatusFromButton(btn, 'failed'); toast('Not connected', 'err'); return; }
    const isOn = !cheatState[label];
    const fVal = isOn ? parseFloat(onFloat) : parseFloat(offFloat);
    const valHex = floatToBEHex(fVal);
    setCheatStatusFromButton(btn, 'applying');
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${valHex}`);
      await henDelay();
      cheatState[label] = isOn;
      btn.classList.toggle('active-cheat', isOn);
      setCheatStatusFromButton(btn, isOn ? 'on' : 'off');
      const stateWord = isOn ? 'ON' : 'OFF';
      log(`${label} ${stateWord} → addr:${addr} val:${valHex}`, 'ok');
      toast(`${label} ${stateWord}`, 'ok');
      notifyPS3(`${label}: ${stateWord}`);
    } catch(e) {
      setCheatStatusFromButton(btn, 'failed');
      log(`${label} failed: ${e.message}`, 'err');
      toast(`${label} failed`, 'err');
    }
  }

  // togFireBytes: toggle a setMemory-style multi-byte write
  // onHex/offHex are full hex strings (e.g. 'C0A000000000000000000000')
  // Written in 4-byte chunks sequentially
  async function togFireBytes(baseAddr, onHex, offHex, label, btn) {
    if (!ip) { setCheatStatusFromButton(btn, 'failed'); toast('Not connected', 'err'); return; }
    const isOn = !cheatState[label];
    const hex = isOn ? onHex : offHex;
    const chunks = hex.match(/.{8}/g) || [];
    setCheatStatusFromButton(btn, 'applying');
    try {
      const base = parseInt(baseAddr, 16);
      for (let i = 0; i < chunks.length; i++) {
        const addr = (base + i * 4).toString(16).toUpperCase();
        await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${chunks[i]}`);
        await henDelay();
      }
      cheatState[label] = isOn;
      btn.classList.toggle('active-cheat', isOn);
      setCheatStatusFromButton(btn, isOn ? 'on' : 'off');
      const stateWord = isOn ? 'ON' : 'OFF';
      log(`${label} ${stateWord} → ${chunks.length} writes from ${baseAddr}`, 'ok');
      toast(`${label} ${stateWord}`, 'ok');
      notifyPS3(`${label}: ${stateWord}`);
    } catch(e) {
      setCheatStatusFromButton(btn, 'failed');
      log(`${label} failed: ${e.message}`, 'err');
      toast(`${label} failed`, 'err');
    }
  }


  // ── Deathsinger's Power patch ─────────────────────────────────────────────
  // King's Fall raid buff mechanic patch.
  // The game's deposit handler runs:  addi r6, r6, -1  (38C6FFFF)
  // which decrements the stack counter by 1 on each deposit.
  // Patching the SIMM field to +0xFFF gives:  addi r6, r6, 0xFFF  (38C60FFF)
  // so every deposit instead adds 4095 stacks (community calls this x4094
  // because the deposit action itself consumes one tick before the patch fires).
  //
  // Alternate encoding if source register is r0 (li r6, -1):
  //   OFF: 38C0FFFF  ->  ON: 38C00FFF
  // dsReadBytes() auto-detects which form is present and stores it.

  const DS_ON_VARIANTS  = { '38C6FFFF': '38C60FFF', '38C0FFFF': '38C00FFF' };
  const DS_OFF_VARIANTS = { '38C60FFF': '38C6FFFF', '38C00FFF': '38C0FFFF' };
  let dsDetectedOff = '38C6FFFF'; // default: addi r6, r6, -1

  async function dsReadBytes() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const addr = document.getElementById('ds-addr').value.trim().replace(/^0x/i, '');
    if (!addr) { toast('Enter the instruction address first', 'err'); return; }
    const outBtn = document.getElementById('ds-read-out');
    outBtn.textContent = 'Reading...';
    try {
      const hex = (await ps3getmem(parseInt(addr, 16), 4)).toUpperCase().slice(0, 8);
      outBtn.textContent = hex;
      if (DS_ON_VARIANTS[hex]) {
        dsDetectedOff = hex;
        outBtn.style.color = 'var(--danger)';
        log('Deathsinger read: ' + hex + ' — original instruction found (OFF)', 'ok');
        toast('Original bytes confirmed', 'ok');
        cheatState["Deathsinger's Power"] = false;
        document.getElementById('tog-deathsinger').classList.remove('active-cheat');
      } else if (DS_OFF_VARIANTS[hex]) {
        dsDetectedOff = DS_OFF_VARIANTS[hex];
        outBtn.style.color = 'var(--success)';
        log('Deathsinger read: ' + hex + ' — patch already active (ON)', 'ok');
        toast('Patch already active!', 'ok');
        cheatState["Deathsinger's Power"] = true;
        document.getElementById('tog-deathsinger').classList.add('active-cheat');
      } else {
        outBtn.style.color = 'var(--gold)';
        log('Deathsinger read: ' + hex + ' — unrecognised (wrong address?)', 'err');
        toast('Unrecognised bytes — check address', 'err');
      }
    } catch(e) {
      outBtn.textContent = 'ERR';
      outBtn.style.color = 'var(--danger)';
      log('Deathsinger read failed: ' + e.message, 'err');
      toast('Read failed — serve from PS3 web server', 'err');
    }
  }

  async function togDeathsinger(btn) {
    if (!ip) { toast('Not connected', 'err'); return; }
    const addr = document.getElementById('ds-addr').value.trim().replace(/^0x/i, '');
    if (!addr) { toast('Enter the instruction address first', 'err'); return; }

    const isOn   = !cheatState["Deathsinger's Power"];
    const onVal  = DS_ON_VARIANTS[dsDetectedOff]  || '38C60FFF';
    const offVal = dsDetectedOff;
    const writeVal = isOn ? onVal : offVal;

    try {
      await ps3fetch('http://' + ip + '/setmem.ps3mapi?addr=' + addr + '&val=' + writeVal);
      cheatState["Deathsinger's Power"] = isOn;
      btn.classList.toggle('active-cheat', isOn);
      const stateWord = isOn ? 'ON' : 'OFF';
      const outBtn = document.getElementById('ds-read-out');
      outBtn.textContent = writeVal;
      outBtn.style.color = isOn ? 'var(--success)' : 'var(--danger)';
      log("Deathsinger's Power " + stateWord + ' -> addr:' + addr + ' val:' + writeVal, 'ok');
      toast("Deathsinger's Power " + stateWord, 'ok');
      notifyPS3("Deathsinger's Power: " + stateWord);
    } catch(e) {
      log("Deathsinger's Power failed: " + e.message, 'err');
      toast("Deathsinger's Power failed", 'err');
    }
  }

  // ── Deathsinger Auto-Scan ────────────────────────────────────────────────
  // Scans 0x00010000 → 0x10000000 — the full practical range of PS3 user-space
  // where Destiny game code can live. Anything above 0x10000000 is kernel/system
  // territory that PS3MAPI can't reliably read.
  //
  // Scan patterns (OFF state):  38C6FFFF (addi r6, r6, -1)
  //                             38C0FFFF (addi r6, r0, -1)
  // Valid confirm set (ON or OFF):
  //   38C6FFFF, 38C0FFFF (unpatched)
  //   38C60FFF,38C00FFF  (already patched)

  const DS_SCAN_RANGES = [
    [0x00010000, 0x00400000],
    [0x00400000, 0x00800000],
    [0x00800000, 0x00C00000],
    [0x00C00000, 0x01000000],
    [0x01000000, 0x01800000],
    [0x01800000, 0x02000000],
    [0x02000000, 0x03000000],
    [0x03000000, 0x04000000],
    [0x04000000, 0x06000000],
    [0x06000000, 0x08000000],
    [0x08000000, 0x0A000000],
    [0x0A000000, 0x0C000000],
    [0x0C000000, 0x0E000000],
    [0x0E000000, 0x10000000],
  ];

  const DS_SCAN_PATTERNS  = ['38C6FFFF', '38C0FFFF'];
  const DS_SCAN_VALID     = new Set(['38C6FFFF', '38C0FFFF', '38C60FFF', '38C00FFF']);
  const DS_SCAN_CHUNK     = 0x4000;   // 16 KB per request — small enough not to time out
  const DS_SCAN_DELAY     = 150;      // ms between chunks
  const DS_SCAN_FAIL_DELAY = 1000;    // ms back-off on a failed chunk
  const DS_SCAN_MAX_RETRY  = 2;       // retries before skipping a chunk

  let _dsScanRunning = false;

  function dsScanSetStatus(msg, colour) {
    const el = document.getElementById('ds-scan-status');
    if (el) { el.textContent = msg; el.style.color = colour || 'var(--muted)'; }
  }

  async function dsScanGetmem(addr, len) {
    for (let attempt = 0; attempt <= DS_SCAN_MAX_RETRY; attempt++) {
      try {
        const raw = await ps3getmem(addr, len);
        return raw.toUpperCase().replace(/\s+/g, '').replace(/^0X/i, '');
      } catch(e) {
        if (attempt < DS_SCAN_MAX_RETRY) {
          await new Promise(r => setTimeout(r, DS_SCAN_FAIL_DELAY));
        } else {
          return null; // skip this chunk
        }
      }
    }
  }

  async function dsScanVerify(addrHex) {
    try {
      await new Promise(r => setTimeout(r, 200));
      const raw = await ps3getmem(parseInt(addrHex, 16), 4);
      const hex = raw.toUpperCase().replace(/\s+/g, '').replace(/^0X/i, '').slice(0, 8);
      return DS_SCAN_VALID.has(hex) ? hex : null;
    } catch(e) {
      return null;
    }
  }

  async function dsAutoScan() {
    if (!ip) { toast('Not connected', 'err'); return; }
    if (_dsScanRunning) return;
    _dsScanRunning = true;

    const scanBtn  = document.getElementById('ds-scan-btn');
    const stopBtn  = document.getElementById('ds-scan-stop-btn');
    const progWrap = document.getElementById('ds-scan-progress');
    const progText = document.getElementById('ds-scan-progress-text');
    const progFill = document.getElementById('ds-scan-progress-fill');

    scanBtn.style.display = 'none';
    stopBtn.style.display = '';
    progWrap.style.display = '';
    dsScanSetStatus('Starting scan…', 'var(--accent)');
    log('Deathsinger auto-scan started — 0x00010000 → 0x10000000', '');

    // Count total chunks for progress bar
    const totalChunks = DS_SCAN_RANGES.reduce((acc, [s, e]) => acc + Math.ceil((e - s) / DS_SCAN_CHUNK), 0);
    let chunksScanned = 0;

    outer:
    for (const [START, END] of DS_SCAN_RANGES) {
      if (!_dsScanRunning) break;
      dsScanSetStatus(`Range 0x${START.toString(16).toUpperCase()} → 0x${END.toString(16).toUpperCase()}`, 'var(--accent)');

      for (let base = START; base < END; base += DS_SCAN_CHUNK) {
        if (!_dsScanRunning) break outer;

        const hex = await dsScanGetmem(base, DS_SCAN_CHUNK);
        chunksScanned++;
        const pct = Math.round((chunksScanned / totalChunks) * 100);
        progFill.style.width = pct + '%';
        progText.textContent = `Chunk 0x${base.toString(16).toUpperCase().padStart(8,'0')} (${pct}%)`;

        if (hex) {
          for (const pattern of DS_SCAN_PATTERNS) {
            let pos = 0;
            while ((pos = hex.indexOf(pattern, pos)) !== -1) {
              const addr = (base + pos / 2).toString(16).toUpperCase().padStart(8, '0');
              dsScanSetStatus(`Hit at 0x${addr} — verifying…`, 'var(--gold)');

              const verified = await dsScanVerify(addr);
              if (verified) {
                // Determine ON/OFF state from what we read back
                const isPatched = (verified === '38C60FFF' || verified === '38C00FFF');
                const stateLabel = isPatched ? 'already patched (ON)' : 'unpatched (OFF)';

                document.getElementById('ds-addr').value = addr;

                // Update the read-out button to reflect current state
                const outBtn = document.getElementById('ds-read-out');
                outBtn.textContent = verified;
                outBtn.style.color = isPatched ? 'var(--success)' : 'var(--danger)';

                // Sync cheat state so Toggle works correctly without needing a separate Read Bytes
                if (isPatched) {
                  dsDetectedOff = DS_OFF_VARIANTS[verified] || '38C6FFFF';
                  cheatState["Deathsinger's Power"] = true;
                  document.getElementById('tog-deathsinger').classList.add('active-cheat');
                } else {
                  dsDetectedOff = verified;
                  cheatState["Deathsinger's Power"] = false;
                  document.getElementById('tog-deathsinger').classList.remove('active-cheat');
                }

                dsScanSetStatus(` Found at 0x${addr} — ${stateLabel}`, 'var(--success)');
                log(`Deathsinger auto-scan confirmed → 0x${addr} = ${verified} (${stateLabel})`, 'ok');
                toast(`Deathsinger found at 0x${addr}`, 'ok');
                _dsScanRunning = false;
                break outer;
              }

              pos += 2;
            }
          }
        }

        await new Promise(r => setTimeout(r, DS_SCAN_DELAY));
      }

      await new Promise(r => setTimeout(r, 500)); // breathe between ranges
    }

    // Cleanup UI
    scanBtn.style.display = '';
    stopBtn.style.display = 'none';
    progWrap.style.display = 'none';

    if (_dsScanRunning) {
      // Finished all ranges with no result
      dsScanSetStatus('No match found — load into the encounter and try again', 'var(--danger)');
      log('Deathsinger auto-scan complete — no valid address found', 'err');
      toast('No address found — are you in the encounter?', 'err');
    } else if (document.getElementById('ds-addr').value) {
      // Stopped by user OR found — if addr is filled it was a find
    } else {
      dsScanSetStatus('Scan stopped', 'var(--muted)');
    }

    _dsScanRunning = false;
  }

  function dsStopScan() {
    _dsScanRunning = false;
    document.getElementById('ds-scan-btn').style.display = '';
    document.getElementById('ds-scan-stop-btn').style.display = 'none';
    document.getElementById('ds-scan-progress').style.display = 'none';
    dsScanSetStatus('Scan stopped', 'var(--muted)');
    log('Deathsinger auto-scan stopped by user', '');
    toast('Scan stopped', '');
  }

  // ── Server Cheats: Void Enemies / Relative OPK ───────────────────────────
  // All three modes write a PPC code block to the hook address 0x10D8EC0.
  // OFF also zeroes the tail region at 0x10D8F34.
  const SRV_HOOK_ADDR  = 0x10D8EC0;
  const SRV_TAIL_ADDR  = 0x10D8F34;
  // ROPK offset float addresses (trackBar5=X, trackBar4=Y, trackBar2=Z in C#)
  const ROPK_X_ADDR = 0x190EBA0;
  const ROPK_Y_ADDR = 0x190EBA4;
  const ROPK_Z_ADDR = 0x190EBA8;

  const VOID_ON_HEX = '3C6001906063EBB0F883FFC8F8A3FFC0F8C3FFB8F9C3FFD8F9E3FFD0DA03FFE0DA23FFE8D243FFB4D263FFB0D283FFAC7DE00026C263FFFC81C30014C2040140C22E0000EE508828FE409210FC12984041810038C2040144C22E0004EE508828FE409210FC12984041810020C2040148C22E0008EE508828FE409210FC1298404181000890830000C22E0000C2040140EE508828FE409210EE94A028EE92A02AC22E0004C2040144EE508828FE409210EE92A02AC22E0008C2040148EE508828FE409210EE92A02AFC1498404180002080A300007C04280040820008480000103CC047C360C6500090C401487DEFF120E883FFC8E8A3FFC0E8C3FFB8E9C3FFD8E9E3FFD0CA03FFE0CA23FFE8C243FFB4C263FFB0C283FFAC306401403C80010A4B0EB134';
  const ROPK_ON_HEX = '3C6001906063EBB0F883FFC8F8A3FFC0F8C3FFB8F9C3FFD8F9E3FFD0DA03FFE0DA23FFE8D243FFB4D263FFB0D283FFAC7DE00026C263FFFC81C30014C2040140C22E0000EE508828FE409210FC12984041810038C2040144C22E0004EE508828FE409210FC12984041810020C2040148C22E0008EE508828FE409210FC1298404181000890830000C22E0000C2040140EE508828FE409210EE94A028EE92A02A41800074C22E0004C2040144EE508828FE409210EE92A02A4180005CC22E0008C2040148EE508828FE409210EE92A02AFC1498404180004080A300007C04280041820034C203FFF0C22E0000EE31802AD2240140C203FFF4C22E0004EE31802AD2240144C22E0008C203FFF8EE31802AD22401487DEFF120E883FFC8E8A3FFC0E8C3FFB8E9C3FFD8E9E3FFD0CA03FFE0CA23FFE8C243FFB4C263FFB0C283FFAC306401403C80010A4B0EB10C';
  const OFF_HEX     = '3C6001906063EBB0F9C3FFD8F9E3FFD0DA03FFE0DA23FFE87DE0002681C30014C2040140C22E0000FC10884040820028C2040144C22E0004FC10884040820018C2040148C22E0008FC10884040820008908300007DEFF120E9C3FFD8E9E3FFD0CA03FFE0CA23FFE8306401403C80010A4B0EB1E4';

  let srvMode = 'off'; // 'void' | 'ropk' | 'off'
  let srvBusy = false;

  async function writeHookBytes(hexStr) {
    // Deactivate the branch BEFORE rewriting the stub to prevent crashes.
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=1C4110&val=30640140`);
    await henDelay();

    const chunks = hexStr.match(/.{8}/g) || [];
    const base = SRV_HOOK_ADDR; // 0x10D8EC0

    // 1. Write the new payload
    for (let i = 0; i < chunks.length; i++) {
      const addr = (base + i * 4).toString(16).toUpperCase();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=${chunks[i]}`);
      await henDelay();
    }

    // 2. Zero out the REMAINING tail region to clean up old payloads.
    // ROPK is our largest payload at 228 bytes (57 chunks of 4 bytes).
    const MAX_CHUNKS = 57;
    for (let i = chunks.length; i < MAX_CHUNKS; i++) {
      const addr = (base + i * 4).toString(16).toUpperCase();
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addr}&val=00000000`);
      await henDelay();
    }

    // 3. Reactivate the branch
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=1C4110&val=48F14DB0`);
  }

  // ── Custom Damage Multiplier ─────────────────────────────────────────────
  const DAMAGE_ADDR = 0x0043E626;
  const DAMAGE_NORMAL_HEX = '3F80';
  const DAMAGE_4X_HEX = '4120';
  const DAMAGE_OHK_HEX = '447A';
  let customDamageBusy = false;
  let customDamageHex = DAMAGE_NORMAL_HEX;

  function normalizeDamageHex(value) {
    const cleaned = String(value ?? '').replace(/[^0-9a-f]/gi, '').toUpperCase().slice(0, 4);
    return cleaned.padStart(4, '0');
  }

  function damageMultiplierToHex(multiplier) {
    const n = Number(multiplier);
    if (!Number.isFinite(n) || n <= 0) return null;

    const buf = new ArrayBuffer(4);
    const view = new DataView(buf);
    view.setFloat32(0, n, false);
    return view.getUint16(0, false).toString(16).padStart(4, '0').toUpperCase();
  }

  function damageHexToEffectiveFloat(hex) {
    hex = normalizeDamageHex(hex);
    const buf = new ArrayBuffer(4);
    const view = new DataView(buf);
    view.setUint16(0, parseInt(hex, 16), false);
    view.setUint16(2, 0, false);
    return view.getFloat32(0, false);
  }

  function damageValueName(hex) {
    hex = normalizeDamageHex(hex);
    if (hex === DAMAGE_NORMAL_HEX) return 'Normal';
    if (hex === DAMAGE_4X_HEX) return '10× preset';
    if (hex === DAMAGE_OHK_HEX) return 'One-Hit';
    return 'Custom';
  }

  function refreshDamageMultiplierDisplays(multiplier) {
    const hex = damageMultiplierToHex(multiplier);
    if (!hex) return;

    const effective = damageHexToEffectiveFloat(hex);
    const slider = document.getElementById('damage-multiplier-slider');
    const sliderVal = document.getElementById('damage-multiplier-val');
    const sliderHex = document.getElementById('damage-multiplier-hex');
    const encoded = document.getElementById('damage-encoded-hex');
    const effectiveEl = document.getElementById('damage-effective-value');
    const status = document.getElementById('damage-current-status');

    if (slider && Number(multiplier) >= Number(slider.min) && Number(multiplier) <= Number(slider.max)) {
      slider.value = String(multiplier);
    }
    if (sliderVal) sliderVal.textContent = `${Number(multiplier).toFixed(Number(multiplier) < 10 ? 1 : 0)}×`;
    if (sliderHex) sliderHex.textContent = hex;
    if (encoded) encoded.textContent = hex;
    if (effectiveEl) effectiveEl.textContent = Number.isFinite(effective) ? String(effective) : '—';
    if (status) status.textContent = `Current selection: ${multiplier}× → ${hex}`;
  }

  function updateDamageMultiplierUI(value) {
    const input = document.getElementById('damage-custom-multiplier');
    if (input) input.value = String(value);
    refreshDamageMultiplierDisplays(Number(value));
  }

  function syncDamageMultiplierInput() {
    const input = document.getElementById('damage-custom-multiplier');
    if (!input) return;

    const n = Number(input.value);
    if (!Number.isFinite(n) || n <= 0) return;
    refreshDamageMultiplierDisplays(n);
  }

  function setDamageMultiplierPreset(multiplier) {
    const input = document.getElementById('damage-custom-multiplier');
    if (input) input.value = String(multiplier);
    refreshDamageMultiplierDisplays(multiplier);
  }

  async function applyDamageHex(hex, sourceLabel = 'Custom Damage') {
    if (typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      openServerSafetyGuide();
      toast('Read the Server Cheats safety guide first', 'err');
      return false;
    }
    if (!ip) {
      toast('Not connected', 'err');
      return false;
    }
    if (customDamageBusy || server4xDamageBusy) {
      toast('Damage value is already being changed', 'err');
      return false;
    }

    hex = normalizeDamageHex(hex);
    await resolveCheatConflicts('damage-custom');
    customDamageBusy = true;

    try {
      await writeHexAt(DAMAGE_ADDR, hex);
      customDamageHex = hex;

      const is4x = hex === DAMAGE_4X_HEX;
      const isOHK = hex === DAMAGE_OHK_HEX;

      server4xDamageState = is4x;
      document.getElementById('srv-4x-damage')?.classList.toggle('active-cheat', is4x);
      updateCheatStatus('srv-4x-damage', is4x ? 'on' : 'off');

      const ohk = document.getElementById('tog-ohk');
      if (ohk) ohk.classList.toggle('active-cheat', isOHK);
      updateCheatStatus('tog-ohk', isOHK ? 'on' : 'off');

      if (typeof cheatState !== 'undefined') {
        cheatState['One-Hit Enemies'] = isOHK;
        cheatState['One Hit Kill'] = isOHK;
      }

      const effective = damageHexToEffectiveFloat(hex);
      const status = document.getElementById('damage-current-status');
      if (status) status.textContent = `Applied: ${effective}× (${hex})`;

      log(`${sourceLabel} → 0043E626 = ${hex} (~${effective}×)`, 'ok');
      toast(`Damage multiplier applied: ~${effective}×`, 'ok');
      return true;
    } catch (e) {
      log(`${sourceLabel} failed: ${e.message}`, 'err');
      toast('Damage write failed', 'err');
      return false;
    } finally {
      customDamageBusy = false;
    }
  }

  async function applyCustomDamageMultiplier() {
    const input = document.getElementById('damage-custom-multiplier');
    if (!input) return;

    const multiplier = Number(input.value);
    if (!Number.isFinite(multiplier) || multiplier <= 0) {
      toast('Enter a multiplier greater than 0', 'err');
      input.focus();
      return;
    }

    const hex = damageMultiplierToHex(multiplier);
    if (!hex) {
      toast('Could not encode multiplier', 'err');
      return;
    }

    await applyDamageHex(hex, `Damage ${multiplier}×`);
    refreshDamageMultiplierDisplays(multiplier);
  }

  async function resetCustomDamageMultiplier() {
    setDamageMultiplierPreset(1);
    await applyDamageHex(DAMAGE_NORMAL_HEX, 'Damage Reset');
  }

  window.updateDamageMultiplierUI = updateDamageMultiplierUI;
  window.syncDamageMultiplierInput = syncDamageMultiplierInput;
  window.setDamageMultiplierPreset = setDamageMultiplierPreset;
  window.applyCustomDamageMultiplier = applyCustomDamageMultiplier;
  window.resetCustomDamageMultiplier = resetCustomDamageMultiplier;

  let server4xDamageState = false;
  let server4xDamageBusy = false;

  async function toggleServer4xDamage(btn) {
    if (typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      openServerSafetyGuide();
      toast('Read the Server Cheats safety guide first', 'err');
      return;
    }
    if (!ip) { updateCheatStatus('srv-4x-damage', 'failed'); toast('Not connected', 'err'); return; }
    if (server4xDamageBusy) { toast('Please wait…', 'err'); return; }

    const willEnable = !server4xDamageState;
    if (willEnable) await resolveCheatConflicts('srv-4x-damage');
    server4xDamageBusy = true;
    updateCheatStatus('srv-4x-damage', 'applying');
    try {
      await writeHexAt(DAMAGE_ADDR, willEnable ? DAMAGE_4X_HEX : DAMAGE_NORMAL_HEX);
      customDamageHex = willEnable ? DAMAGE_4X_HEX : DAMAGE_NORMAL_HEX;
      setDamageMultiplierPreset(willEnable ? 10 : 1);
      server4xDamageState = willEnable;
      if (btn) btn.classList.toggle('active-cheat', server4xDamageState);
      updateCheatStatus('srv-4x-damage', server4xDamageState ? 'on' : 'off');

      if (willEnable) {
        const ohk = document.getElementById('tog-ohk');
        if (ohk) ohk.classList.remove('active-cheat');
        updateCheatStatus('tog-ohk', 'off');
        if (typeof cheatState !== 'undefined') {
          cheatState['One-Hit Enemies'] = false;
          cheatState['One Hit Kill'] = false;
        }
      }

      log(`4x Damage ${willEnable ? 'ON' : 'OFF'}`, 'ok');
      toast(`4x Damage ${willEnable ? 'ON' : 'OFF'}`, 'ok');
    } catch (e) {
      updateCheatStatus('srv-4x-damage', 'failed');
      log(`4x Damage failed: ${e.message}`, 'err');
      toast('4x Damage failed', 'err');
    } finally {
      server4xDamageBusy = false;
    }
  }

  async function srvSetMode(mode, btn) {
    if (typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      openServerSafetyGuide();
      if (typeof toast === 'function') toast('Read the Server Cheats safety guide first', 'err');
      return;
    }
    if (!ip) { if (btn?.id) updateCheatStatus(btn.id, 'failed'); toast('Not connected', 'err'); return; }
    if (mode === 'off' && server4xDamageState) {
      try {
        await writeHexAt(DAMAGE_ADDR, DAMAGE_NORMAL_HEX);
        customDamageHex = DAMAGE_NORMAL_HEX;
        setDamageMultiplierPreset(1);
        server4xDamageState = false;
        document.getElementById('srv-4x-damage')?.classList.remove('active-cheat');
      } catch (e) {
        log(`4x Damage restore failed: ${e.message}`, 'err');
      }
    }

    // Block if the player hook install is in progress OR if a cheat is currently writing
    if (hookInstalling || srvBusy) {
      toast('Writing to memory, please wait…', 'err');
      return;
    }

    srvBusy = true; // Lock the function
    if (btn?.id) updateCheatStatus(btn.id, 'applying');

    // Deactivate all three buttons visually
    ['srv-void','srv-ropk','srv-off'].forEach(id => {
      if (id !== btn?.id) updateCheatStatus(id, 'off');
      const el = document.getElementById(id);
      if (el) el.classList.remove('active-cheat');
    });
    document.getElementById('ropk-controls').style.display = 'none';
    srvMode = mode;
    try {
      if (mode === 'void') {
        await writeHookBytes(VOID_ON_HEX);
        btn.classList.add('active-cheat');
        updateCheatStatus('srv-void', 'on');
        log('Void Enemies ON → hook written', 'ok');
        toast('Void Enemies ON', 'ok');
        notifyPS3('Void Enemies: ON');
      } else if (mode === 'ropk') {
        await writeHookBytes(ROPK_ON_HEX);
        // Reset sliders and write 0,0,0 offsets
        ['ropk-x','ropk-y','ropk-z'].forEach(id => {
          document.getElementById(id).value = 0;
        });
        ['ropk-x-val','ropk-y-val','ropk-z-val'].forEach(id => {
          document.getElementById(id).textContent = '0';
        });
        await ropkWrite();
        document.getElementById('ropk-controls').style.display = '';
        btn.classList.add('active-cheat');
        updateCheatStatus('srv-ropk', 'on');
        log('Relative OPK ON → hook written', 'ok');
        toast('Relative OPK ON', 'ok');
        notifyPS3('Relative OPK: ON');
      } else {
        // Off — restore original hook, clear tail
        await writeHookBytes(OFF_HEX);
        btn.classList.add('active-cheat');
        updateCheatStatus('srv-off', 'on');
        updateCheatStatus('srv-void', 'off');
        updateCheatStatus('srv-ropk', 'off');
        updateCheatStatus('srv-4x-damage', 'off');
        log('Server Cheats OFF → hook restored', 'ok');
        toast('Server Cheats OFF', 'ok');
        notifyPS3('Server Cheats: OFF');
      }
    } catch(e) {
      if (btn?.id) updateCheatStatus(btn.id, 'failed');
      log(`Server cheat (${mode}) failed: ${e.message}`, 'err');
      toast('Server cheat failed', 'err');
    } finally {
      srvBusy = false; // Release the lock so you can click again
    }
  }

  // Write the current ROPK X/Y/Z slider values as floats to the offset addresses
  async function ropkWrite() {
    if (typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      return;
    }
    if (!ip || srvMode !== 'ropk') return;
    const x = parseFloat(document.getElementById('ropk-x').value) || 0;
    const y = parseFloat(document.getElementById('ropk-y').value) || 0;
    const z = parseFloat(document.getElementById('ropk-z').value) || 0;
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${ROPK_X_ADDR.toString(16).toUpperCase()}&val=${floatToBEHex(x)}`);
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${ROPK_Y_ADDR.toString(16).toUpperCase()}&val=${floatToBEHex(y)}`);
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${ROPK_Z_ADDR.toString(16).toUpperCase()}&val=${floatToBEHex(z)}`);
  }

  // ── Read Player Position via pointer ─────────────────────────────────────
  async function readPlayerPos() {
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      // Step 1: Get the pointer from the static address
      const pointerRaw = await ps3getmem(0x190EBB0, 4);
      console.log(`pointerRaw="${pointerRaw}" length=${pointerRaw.length}`);
      const basePtr = parseInt(pointerRaw.slice(0, 8), 16); // hard-slice to 8 chars
      console.log(`basePtr=0x${basePtr.toString(16)}`);
      if (!basePtr || basePtr > 0x3FFFFFFF) throw new Error(`Bad pointer 0x${basePtr.toString(16)} — still reading VSH memory`);

      // Step 2: Calculate the coordinate addresses
      const xAddr = basePtr + 0x140;
      const yAddr = basePtr + 0x144;
      const zAddr = basePtr + 0x148;

      // Step 3: Fetch hex and convert to Float32 (ps3readFloat handles both)
      const x = await ps3readFloat(xAddr);
      const y = await ps3readFloat(yAddr);
      const z = await ps3readFloat(zAddr);
      console.log(`Player Pos: X:${x} Y:${y} Z:${z}`);
      log(`Player Pos → X:${x.toFixed(3)} Y:${y.toFixed(3)} Z:${z.toFixed(3)}`, 'ok');
      toast(`X:${x.toFixed(1)} Y:${y.toFixed(1)} Z:${z.toFixed(1)}`, 'ok');
    } catch(e) {
      log(`readPlayerPos failed: ${e.message}`, 'err');
      toast('Pos read failed', 'err');
    }
  }



  // ── Memory Scanner ────────────────────────────────────────────────────────
  // Snapshot A/B: reads a memory range from PS3 in chunks, stores raw hex.
  // Diff: compares the two snapshots word-by-word and lists changed addresses.
  // Results can be written back or pinned for later testing.

  const scanState = {
    snapA: null,   // hex string
    snapB: null,   // hex string
    base: 0,
    len: 0,
    type: 'u32',
    results: [],   // [{addr, valA, valB}]
    pinned: [],    // [{addr, valA, valB, label}]
  };

  // Read memory in CHUNK_SIZE chunks to avoid PS3MAPI timeouts
  const SCAN_CHUNK = 4096; // bytes per request

  async function scanSnapshot(which) {
    if (!ip) { toast('Not connected', 'err'); return; }

    const base = parseInt(document.getElementById('scan-base').value.trim().replace(/^0x/i,''), 16);
    const len  = parseInt(document.getElementById('scan-len').value) || 65536;
    const type = document.getElementById('scan-type').value;

    if (isNaN(base) || base === 0) { toast('Enter a valid base address', 'err'); return; }
    if (len < 4 || len > 524288) { toast('Length must be 4–524288 bytes', 'err'); return; }

    const btnId = which === 'A' ? 'btn-snap-a' : 'btn-snap-b';
    const btn   = document.getElementById(btnId);
    btn.disabled = true;
    btn.textContent = `Reading…`;

    const prog     = document.getElementById('scan-progress');
    const progText = document.getElementById('scan-progress-text');
    const progFill = document.getElementById('scan-progress-fill');
    prog.style.display = '';

    let hex = '';
    const chunks = Math.ceil(len / SCAN_CHUNK);

    try {
      for (let i = 0; i < chunks; i++) {
        const chunkAddr   = base + i * SCAN_CHUNK;
        const chunkLen    = Math.min(SCAN_CHUNK, len - i * SCAN_CHUNK);
        const pct = Math.round(((i + 1) / chunks) * 100);
        progText.textContent = `Snapshot ${which}: chunk ${i+1}/${chunks} @ 0x${chunkAddr.toString(16).toUpperCase()}`;
        progFill.style.width = pct + '%';

        const chunkHex = await ps3getmem(chunkAddr, chunkLen);
        hex += chunkHex;
      }

      if (which === 'A') {
        scanState.snapA = hex;
        scanState.base  = base;
        scanState.len   = len;
        scanState.type  = type;
        document.getElementById('stat-a').innerHTML = `Snapshot A: <span>${(hex.length/2).toLocaleString()} bytes </span>`;
        log(`Scanner: Snapshot A taken — 0x${base.toString(16).toUpperCase()}, ${(hex.length/2).toLocaleString()} bytes`, 'ok');
        toast('Snapshot A ', 'ok');
      } else {
        scanState.snapB = hex;
        document.getElementById('stat-b').innerHTML = `Snapshot B: <span>${(hex.length/2).toLocaleString()} bytes </span>`;
        log(`Scanner: Snapshot B taken — 0x${base.toString(16).toUpperCase()}, ${(hex.length/2).toLocaleString()} bytes`, 'ok');
        toast('Snapshot B ', 'ok');
      }
    } catch(e) {
      toast(`Snapshot ${which} failed: ${e.message}`, 'err');
      log(`Scanner: Snapshot ${which} failed — ${e.message}`, 'err');
    } finally {
      prog.style.display = 'none';
      btn.disabled = false;
      btn.textContent = `Snapshot ${which}`;
    }
  }

  function scanWordSize() {
    const t = scanState.type;
    if (t === 'u8')  return 1;
    if (t === 'u16') return 2;
    return 4; // u32 and float are both 4 bytes
  }

  function scanParseWord(hex, byteOffset, type) {
    const size = scanWordSize();
    const pos  = byteOffset * 2;
    const raw  = hex.slice(pos, pos + size * 2);
    if (raw.length < size * 2) return null;
    if (type === 'float') {
      const buf  = new ArrayBuffer(4);
      const view = new DataView(buf);
      for (let i = 0; i < 4; i++) view.setUint8(i, parseInt(raw.slice(i*2, i*2+2), 16));
      return view.getFloat32(0, false).toPrecision(6);
    }
    return raw.toUpperCase();
  }

  function scanDiff() {
    if (!scanState.snapA || !scanState.snapB) {
      toast('Need both snapshots first', 'err'); return;
    }
    const a    = scanState.snapA;
    const b    = scanState.snapB;
    const type = document.getElementById('scan-type').value;
    scanState.type = type;
    const step = scanWordSize();
    const minLen = Math.min(a.length, b.length);
    const results = [];

    for (let byteOff = 0; byteOff + step * 2 <= minLen / 2; byteOff += step) {
      // Alignment: only report 4-byte aligned addresses (cleaner results)
      if (step < 4 && (byteOff % 4 !== 0)) continue;
      const posA = byteOff * 2;
      const posB = byteOff * 2;
      const rawA = a.slice(posA, posA + step * 2);
      const rawB = b.slice(posB, posB + step * 2);
      if (rawA !== rawB) {
        const addr   = scanState.base + byteOff;
        const valA   = scanParseWord(a, byteOff, type);
        const valB   = scanParseWord(b, byteOff, type);
        results.push({ addr, valA, valB, rawA: rawA.toUpperCase(), rawB: rawB.toUpperCase() });
      }
    }

    scanState.results = results;
    document.getElementById('stat-diff').innerHTML = `Diffs: <span>${results.length.toLocaleString()}</span>`;
    log(`Scanner: diff complete — ${results.length} changed addresses found`, results.length ? 'ok' : 'err');
    toast(results.length ? `${results.length} diffs found` : 'No differences found', results.length ? 'ok' : 'err');
    scanRenderResults(results);
  }

  function scanRenderResults(results, filter) {
    const container = document.getElementById('scan-results');
    const filtered  = filter ? results.filter(r => r.addr.toString(16).toUpperCase().includes(filter.toUpperCase())) : results;

    if (filtered.length === 0) {
      container.innerHTML = '<div class="scan-empty">' + (results.length === 0 ? 'No differences found. Try a wider range or different address.' : 'No results match filter.') + '</div>';
      return;
    }

    // Cap display at 200 rows for performance
    const display = filtered.slice(0, 200);
    const type    = scanState.type;

    container.innerHTML = display.map(r => {
      const addrHex = r.addr.toString(16).toUpperCase().padStart(8, '0');
      const valADisplay = type === 'float' ? r.valA : r.rawA;
      const valBDisplay = type === 'float' ? r.valB : r.rawB;
      return `
        <div class="scan-result-row">
          <span class="scan-result-addr">${addrHex}</span>
          <div class="scan-result-vals">
            <span class="scan-val-chip scan-val-a" title="Snapshot A (primary)">A: ${valADisplay}</span>
            <span class="scan-val-chip scan-val-b" title="Snapshot B (heavy)">B: ${valBDisplay}</span>
          </div>
          <div class="scan-result-actions">
            <button class="scan-act-btn write-a" onclick="scanWrite('${addrHex}','${r.rawA}','${addrHex} ← A')" title="Write A value back">← A</button>
            <button class="scan-act-btn write-b" onclick="scanWrite('${addrHex}','${r.rawB}','${addrHex} ← B')" title="Write B value">← B</button>
            <button class="scan-act-btn pin-btn"  onclick="scanPin(${r.addr},'${r.rawA}','${r.rawB}')" title="Pin this address">Pin</button>
          </div>
        </div>`;
    }).join('');

    if (filtered.length > 200) {
      container.insertAdjacentHTML('beforeend', `<div class="scan-empty">${filtered.length - 200} more — use filter to narrow down.</div>`);
    }
  }

  function scanFilterResults() {
    const filter = document.getElementById('scan-filter').value.trim();
    scanRenderResults(scanState.results, filter);
  }

  async function scanWrite(addrHex, valHex, label) {
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${addrHex}&val=${valHex}`);
      log(`Scanner write → addr:${addrHex} val:${valHex}`, 'ok');
      toast(`Wrote ${valHex} → ${addrHex}`, 'ok');
    } catch(e) {
      log(`Scanner write failed: ${e.message}`, 'err');
      toast('Write failed', 'err');
    }
  }

  function scanPin(addr, rawA, rawB) {
    const addrHex = addr.toString(16).toUpperCase().padStart(8, '0');
    // Don't duplicate
    if (scanState.pinned.some(p => p.addr === addr)) {
      toast(`${addrHex} already pinned`, 'err'); return;
    }
    const type = scanState.type;
    const valA = type === 'float' ? scanParseWord(rawA.toLowerCase(), 0, 'float') : rawA;
    const valB = type === 'float' ? scanParseWord(rawB.toLowerCase(), 0, 'float') : rawB;
    scanState.pinned.push({ addr, addrHex, rawA, rawB, valA, valB });
    toast(`Pinned ${addrHex}`, 'ok');
    scanRenderPinned();
  }

  function scanRenderPinned() {
    const container = document.getElementById('scan-pinned');
    const empty     = document.getElementById('scan-pinned-empty');
    if (scanState.pinned.length === 0) {
      container.innerHTML = '<div class="scan-empty" id="scan-pinned-empty">No pinned addresses yet — pin results to test them later.</div>';
      return;
    }
    const type = scanState.type;
    container.innerHTML = scanState.pinned.map((p, i) => `
      <div class="scan-result-row">
        <span class="scan-result-addr">${p.addrHex}</span>
        <div class="scan-result-vals">
          <span class="scan-val-chip scan-val-a">A: ${p.valA}</span>
          <span class="scan-val-chip scan-val-b">B: ${p.valB}</span>
        </div>
        <div class="scan-result-actions">
          <button class="scan-act-btn write-a" onclick="scanWrite('${p.addrHex}','${p.rawA}','restore A')" title="Write A (primary) value">← A</button>
          <button class="scan-act-btn write-b" onclick="scanWrite('${p.addrHex}','${p.rawB}','write B (heavy)')" title="Write B (heavy) value">← B</button>
          <button class="scan-act-btn" style="border-color:var(--danger);color:var(--danger)" onclick="scanUnpin(${i})" title="Remove pin"></button>
        </div>
      </div>`).join('');
  }

  function scanUnpin(i) {
    scanState.pinned.splice(i, 1);
    scanRenderPinned();
  }

  function scanReset() {
    scanState.snapA = null;
    scanState.snapB = null;
    scanState.results = [];
    document.getElementById('stat-a').innerHTML    = `Snapshot A: <span>—</span>`;
    document.getElementById('stat-b').innerHTML    = `Snapshot B: <span>—</span>`;
    document.getElementById('stat-diff').innerHTML = `Diffs: <span>—</span>`;
    document.getElementById('scan-results').innerHTML = '<div class="scan-empty">Take Snapshot A in one weapon state, switch weapon, take Snapshot B, then Diff.</div>';
    document.getElementById('scan-filter').value = '';
    toast('Scanner reset', 'ok');
    log('Scanner reset', '');
  }

  // ── Console ID (CID) ─────────────────────────────────────────────────────

  async function fetchConsoleID() {
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      const response = await fetch(`http://${ip}/getcid.ps3mapi`);
      const htmlText = await response.text();

      // Parse the HTML response and extract the CID from the <h2> tag.
      // webMAN embeds the CID as part of the h2 text content.
      const parser = new DOMParser();
      const doc = parser.parseFromString(htmlText, 'text/html');

      const h2 = doc.querySelector('h2');
      if (h2) {
        // Use regex to extract the first 32-char hex string found.
        const match = (h2.textContent || '').match(/[0-9A-Fa-f]{32}/);
        if (match) {
          document.getElementById('cid-input').value = match[0];
          log(`Fetched CID: ${match[0]}`, 'ok');
          toast('CID fetched', 'ok');
        } else {
          log('CID pattern not found in response', 'err');
          toast('CID not found', 'err');
        }
      } else {
        log('Could not parse response header', 'err');
        toast('Could not parse CID', 'err');
      }
    } catch(e) {
      log('Failed to fetch CID: ' + e.message, 'err');
      toast('Failed to fetch CID', 'err');
    }
  }

  async function setConsoleID() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const newCid = document.getElementById('cid-input').value.trim();
    if (!newCid) { toast('Enter a CID first', 'err'); return; }
    if (!confirm(`Are you sure you want to change CID to: ${newCid}?`)) return;
    try {
      await ps3fetch(`http://${ip}/setcid.ps3mapi?cid=${newCid}`);
      log('CID updated successfully', 'ok');
      toast('CID updated', 'ok');
    } catch(e) {
      log('Failed to set CID', 'err');
      toast('Failed to set CID', 'err');
    }
  }

