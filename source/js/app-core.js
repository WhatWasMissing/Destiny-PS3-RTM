
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
  // Source-backed Destiny 1.24 weapon-ammo patch. Keep the three original
  // instructions together so the main toggle and Reset All use the same bytes.
  const UNLIMITED_AMMO_PATCH = {
    on: [
      ['003108EC', '3880270F'],
      ['00310554', '38A0270F'],
      ['00CF95AC', '60000000FC1EF8003FC0461C63DE3C0093DF0110']
    ],
    off: [
      ['003108EC', '63C40000'],
      ['00310554', '63C50000'],
      ['00CF95AC', '63DD0000408200083BA00001807F001CD3DF0110']
    ]
  };

  async function resetAllCheats() {
    const operationGeneration = connectionGeneration;
    log('Resetting all active cheats…', '');
    const CHEAT_DEFS = [
      { id: 'tog-god-mode',        addr: '00444CA0', offVal: 'C0210358', label: 'Invincibility',         size: 4 },
      { id: 'tog-ohk',             addr: '43E626',   offVal: '3F80',     label: 'One-Hit Enemies',     size: 2 },
      { id: 'tog-power',           addr: '02C36FC',  offVal: 'C3C30000', label: 'Infinite Power',  size: 4 },
      { id: 'tog-no-spread',       addr: '30D230',   offVal: '3F800000', label: 'No Spread',       size: 4 },
      { id: 'tog-bouncy-items',    addr: 'DB8BE8',   offVal: '2C030000', label: 'Bouncy Items',    size: 4 },
      { id: 'tog-jump-boost',      addr: 'CCB9E8',   offVal: '3063FFFF', label: '4× Jump Boost',   size: 4 },
      { id: 'tog-bullets',         addr: '51E7E0',   offVal: '3F800000', label: 'Freeze Projectiles',     size: 4 },
      { id: 'tog-instant-respawn', addr: '0071C348', offVal: '93BF0030', label: 'Instant Respawn',  size: 4 },
      { id: 'tog-rapid-fire',      addr: '307590',   offVal: '3F800000', label: 'Faster Fire Rate',       size: 4 },
      { id: 'tog-super-speed',     addr: '1A96C0',   offVal: '00',       label: 'Super Speed',      size: 1, preconditions: [['1A96C0', '04000000']] },
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
      { id: 'tog-unl-abilities', label: 'Unlimited Abilities', pairs: [['2C1DB4','D03B019C'],['CCB9E8','3063FFFF'],['2C36FC','C3C30000']] },
      { id: 'tog-ammo', label: 'Unlimited Ammo', pairs: UNLIMITED_AMMO_PATCH.off },
    ];
    const BYTES_DEFS = [
      { id: 'tog-no-pushbacks', addr: '1389760', offHex: '3F8000003F8000003F800000', label: 'No Pushback Barriers' },
      { id: 'tog-rock-jump', addr: '1389760', offHex: '3F8000003F8000003F800000', label: 'Rock Jump' },
    ];
    const definitions = [
      ...CHEAT_DEFS.map(item => ({...item,pairs:[[item.addr,item.offVal]]})),
      ...FLOAT_DEFS.map(item => ({...item,pairs:[[item.addr,floatToBEHex(item.offFloat)]]})),
      ...MULTI_DEFS,
      { id:'tog-deathsinger', label:"Deathsinger's Power", pairs:[[document.getElementById('ds-addr')?.value.trim().replace(/^0x/i,'') || '1C7B58',dsDetectedOff || '38C60001']] },
      ...BYTES_DEFS.map(item => ({...item,pairs:(item.offHex.match(/.{8}/g)||[]).map((value,index)=>[(parseInt(item.addr,16)+index*4).toString(16),value])}))
    ];
    let verified = 0, failed = 0;
    for (const definition of definitions) {
      if (operationGeneration !== connectionGeneration) return {verified,failed:failed+1};
      const element = document.getElementById(definition.id);
      if (!element || (!element.classList.contains('active-cheat') && !requestedCheatState[definition.label])) continue;
      try {
        const result = await patchRunner.apply(definition.pairs, definition.preconditions ? {preconditions:definition.preconditions} : {});
        if (result.status !== 'verified') throw new Error('readback unavailable');
        cheatState[definition.label] = false; requestedCheatState[definition.label] = false;
        element.classList.remove('active-cheat'); updateCheatStatus(element,'verified-off'); verified++;
      } catch (error) {
        element.classList.remove('active-cheat'); updateCheatStatus(element,'unknown'); failed++;
        log(`${definition.label} reset not confirmed: ${error.message}`,'err');
      }
    }
    if (operationGeneration === connectionGeneration && srvMode !== 'off') {
      if (await srvSetMode('off',document.getElementById('srv-off'))) verified++; else failed++;
    }
    log(`Reset result: ${verified} verified off; ${failed} unconfirmed`, failed ? 'err' : 'ok');
    toast(`${verified} reset verified; ${failed} unconfirmed`, failed ? 'err' : 'ok');
    return {verified,failed};
  }

  function ts() {
    return new Date().toTimeString().slice(0,8);
  }

  function log(msg, type = '', automationKind = '') {
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
      automationNotify(msg, automationKind || 'error', { ip, fallbackKind:automationKind === 'connect' ? 'error' : '' });
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
    const target = new URL(url);
    if (target.pathname === '/setmem.ps3mapi') {
      if (target.hostname !== ip || (target.searchParams.has('proc') && target.searchParams.get('proc').toLowerCase() !== GAME_PID.toLowerCase())) throw new Error('Memory write target does not match active game session');
      let value = target.searchParams.get('val') || '';
      if (!value) throw new Error('Memory write value is required');
      if (target.searchParams.has('size')) {
        const bytes = Number(target.searchParams.get('size'));
        if (![1,2,4].includes(bytes) || value.length > bytes*2) throw new Error('Memory value exceeds selected width');
        value = value.padStart(bytes*2,'0');
      }
      await verifiedMemoryValue(target.searchParams.get('addr') || '',value,timeoutMs);
      return new Response('Memory write verified', {status:200});
    }
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

  // ── Shared PS3MAPI memory-read helpers ──────────────────────────────────
  // Teleport and character tools use these helpers to resolve live values.
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

  async function ps3getmem(addr, len = 4, timeoutMs = 8000) {
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
      const response = await consoleTransport.request(url, timeoutMs);
      page = await response.text();
    } catch (e) {
      firstError = e;
    }

    if (!page) {
      try {
        const xhr = await ps3fetchRead(url, timeoutMs);
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
      const byte = parseInt(hex.slice(i, i + 2), 16);
      if (!Number.isFinite(byte) || byte === 0) break;
      result += String.fromCharCode(byte);
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

  let lastSavedTeleportSnapshot = JSON.stringify({ savedTeleports, teleportFolders });
  function saveTeleportsToStorage() {
    try {
      RtmBackups.restore(localStorage, { format: 'destiny-rtm-settings', version: 1, settings: {
        [TELEPORTS_KEY]: JSON.stringify(savedTeleports),
        [TELEPORT_FOLDERS_KEY]: JSON.stringify(teleportFolders)
      } });
      lastSavedTeleportSnapshot = JSON.stringify({ savedTeleports, teleportFolders });
      return true;
    } catch (error) {
      const previous = JSON.parse(lastSavedTeleportSnapshot);
      savedTeleports = previous.savedTeleports; teleportFolders = previous.teleportFolders;
      toast(`Positions were not saved: ${error.message}`, 'err');
      return false;
    }
  }

  function tpEscape(v) {
    return String(v ?? '').replace(/[&<>\"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',"'":'&#39;'}[c]));
  }
  async function tpNewFolder() {
    const result = await RtmDialogs.fields({ title:'New folder', fields:[{name:'name',label:'Folder name'}] });
    if (!result) return;
    tpRemember(); teleportFolders.push({ id:'folder_'+crypto.randomUUID().replaceAll('-',''), name:result.name, open:true });
    if (!saveTeleportsToStorage()) { tpRenderSlots(); return; } tpRenderSlots();
  }
  async function tpRenameFolder(id) {
    const folder = teleportFolders.find(folder => folder.id === id); if (!folder || folder.system) return;
    const result = await RtmDialogs.fields({ title:'Rename folder', fields:[{name:'name',label:'Folder name',value:folder.name}] });
    if (!result) return;
    tpRemember(); folder.name = result.name;
    if (!saveTeleportsToStorage()) { tpRenderSlots(); return; } tpRenderSlots();
  }
  async function tpDeleteFolder(id) {
    const folder = teleportFolders.find(folder => folder.id === id); if (!folder || folder.system) return;
    if (!await RtmDialogs.confirm(`Delete folder "${folder.name}"? Positions will move to Unsorted.`)) return;
    tpRemember(); savedTeleports.forEach(position => { if (position.folderId === id) position.folderId = 'unsorted'; });
    teleportFolders = teleportFolders.filter(folder => folder.id !== id);
    if (!saveTeleportsToStorage()) { tpRenderSlots(); return; } tpRenderSlots();
  }

  function tpToggleFolder(id) {
    const folder = teleportFolders.find(f => f.id === id); if (!folder) return;
    folder.open = !folder.open; if (!saveTeleportsToStorage()) { tpRenderSlots(); return; } tpRenderSlots();
  }
  async function tpMoveSlot(idx) {
    const position = savedTeleports[idx]; if (!position) return;
    const result = await RtmDialogs.fields({ title:'Move position', fields:[{ name:'folder', label:'Destination folder', value:position.folderId,
      options:teleportFolders.map(folder => ({value:folder.id,label:folder.name})) }] });
    if (!result) return;
    tpRemember(); position.folderId = result.folder;
    if (!saveTeleportsToStorage()) { tpRenderSlots(); return; } tpRenderSlots();
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
    const operationGeneration = connectionGeneration;
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
      ensureOperationSession(operationGeneration);
      await ps3fetch(
        `http://${ip}/setmem.ps3mapi?addr=${xAddr.toString(16).padStart(8, '0').toUpperCase()}&val=${combinedHex}`
      );

      toast("Position bytes verified; movement not observed", "");
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
    const operationGeneration = connectionGeneration;
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
          ensureOperationSession(operationGeneration);
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

      toast("Position write acknowledged; movement not verified", "");
      log(`Position write acknowledged: X:${x} Y:${y} Z:${z}; movement not verified`, "");
    } catch (e) {
      toast("Teleport failed", "err");
      log("Teleport error: " + e.message, "err");
    }
  }

  async function tpSaveCurrent() {
    const position = Object.fromEntries(['x','y','z'].map(axis => [axis,document.getElementById('tp-'+axis).value]));
    if (!['x','y','z'].every(axis => RtmBackups.coordinate(position[axis]))) { toast('Enter finite coordinates first', 'err'); return; }
    const result = await RtmDialogs.fields({ title:'Save position', fields:[
      {name:'name',label:'Location name',value:`Position ${savedTeleports.length+1}`},
      {name:'folder',label:'Folder',options:teleportFolders.map(folder => ({value:folder.id,label:folder.name}))}
    ] });
    if (!result) return;
    if (tpIsDuplicate(position) && !await RtmDialogs.confirm('These coordinates are already saved. Save another copy?')) return;
    tpRemember(); savedTeleports.push({...position,name:result.name,folderId:result.folder});
    if (!saveTeleportsToStorage()) { tpRenderSlots(); return; } tpRenderSlots(); toast('Position saved', 'ok');
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
    if (!saveTeleportsToStorage()) { tpRenderSlots(); return; }
    tpRenderSlots();
    toast("Position removed", "ok");
  }

  // Render any teleports restored from localStorage. The tp-saved-list
  // element is defined later in the DOM, so wait until it exists instead
  // of calling this immediately (which was throwing and aborting the rest
  // of this script — including all the Settings modal code below it).
  document.addEventListener('DOMContentLoaded', tpRenderSlots);

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
    await verifiedMemoryValue(addrHex,hex);
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

  const EMBLEM_VALUE_ADDR = 0x600C8C80;
  const EMBLEM_SCAN_BASE = 0x600E0000;
  const EMBLEM_SCAN_LENGTH = 0x20000;
  const EMBLEM_SCAN_CHUNK = 0x4000;
  const EMBLEM_RESTORE_STORAGE_KEY = 'rtm_emblem_restore_v1';
  let emblemRestoreStack = [];
  try {
    const saved = JSON.parse(localStorage.getItem(EMBLEM_RESTORE_STORAGE_KEY) || '[]');
    if (Array.isArray(saved)) emblemRestoreStack = saved.filter(item => item &&
      Number.isInteger(item.address) && /^[0-9A-F]{4}$/.test(item.originalHex) && /^[0-9A-F]{4}$/.test(item.writtenHex) &&
      typeof item.ip === 'string' && item.ip.length <= 15);
  } catch (e) {}

  function saveEmblemRestoreStack(nextStack) {
    if (nextStack.length) {
      localStorage.setItem(EMBLEM_RESTORE_STORAGE_KEY, JSON.stringify(nextStack));
      emblemRestoreStack = nextStack;
    } else {
      emblemRestoreStack = [];
      try { localStorage.removeItem(EMBLEM_RESTORE_STORAGE_KEY); } catch (e) {}
    }
  }

  function setEmblemStatus(message, failed = false) {
    const status = document.getElementById('emblem-status');
    if (!status) return;
    status.textContent = message;
    status.style.color = failed ? 'var(--danger)' : 'var(--muted)';
  }

  function populateEmblemOptions() {
    const select = document.getElementById('emblem-select');
    if (!select || !Array.isArray(window.RTM_EMBLEM_OPTIONS)) return;
    select.replaceChildren();
    for (const emblem of window.RTM_EMBLEM_OPTIONS) {
      if (emblem.itemClass !== 5) continue;
      const option = document.createElement('option');
      option.value = String(emblem.id);
      option.textContent = `${emblem.name} · ${emblem.id}`;
      select.appendChild(option);
    }
  }

  async function locateCharacterEmblem(generation) {
    const currentHex = (await ps3getmem(EMBLEM_VALUE_ADDR, 2)).toUpperCase();
    ensureOperationSession(generation);
    if (!/^[0-9A-F]{4}$/.test(currentHex)) throw new Error('Current emblem value is not a 16-bit value');

    const scanBytes = new Uint8Array(EMBLEM_SCAN_LENGTH);
    for (let offset = 0; offset < EMBLEM_SCAN_LENGTH; offset += EMBLEM_SCAN_CHUNK) {
      ensureOperationSession(generation);
      const length = Math.min(EMBLEM_SCAN_CHUNK, EMBLEM_SCAN_LENGTH - offset);
      const raw = await ps3getmem(EMBLEM_SCAN_BASE + offset, length);
      ensureOperationSession(generation);
      const bytes = RtmMemoryFeatures.bytesFromHex(raw);
      if (bytes.length !== length) throw new Error(`Emblem scan was short at 0x${(EMBLEM_SCAN_BASE + offset).toString(16).toUpperCase()}`);
      scanBytes.set(bytes, offset);
    }

    const emblemId = parseInt(currentHex, 16);
    const matches = RtmMemoryFeatures.findEmblemMatches(scanBytes, EMBLEM_SCAN_BASE, emblemId, RtmMemoryFeatures.EMBLEM_EXCLUDED_RANGES);
    if (matches.length !== 1) throw new Error(matches.length
      ? `Current emblem matched ${matches.length} locations; refusing an ambiguous write`
      : 'Could not locate the current emblem in the source address range');
    return { currentHex, emblemId, address: matches[0] };
  }

  async function readCharacterEmblem() {
    if (!ip || !document.getElementById('status-badge')?.classList.contains('connected')) {
      setEmblemStatus('Connect to the console first.', true);
      toast('Not connected', 'err');
      return;
    }
    const generation = connectionGeneration;
    const button = document.getElementById('emblem-read-btn');
    if (button) button.disabled = true;
    setEmblemStatus('Reading the current emblem and scanning its source range…');
    try {
      const result = await locateCharacterEmblem(generation);
      ensureOperationSession(generation);
      const select = document.getElementById('emblem-select');
      if (select) {
        if (![...select.options].some(option => Number(option.value) === result.emblemId)) {
          const option = document.createElement('option');
          option.value = String(result.emblemId);
          option.textContent = `Current unlisted emblem · ${result.emblemId}`;
          select.insertBefore(option, select.firstChild);
        }
        select.value = String(result.emblemId);
      }
      document.getElementById('emblem-current').value = `${result.emblemId} · 0x${result.currentHex}`;
      setEmblemStatus(`Located at 0x${result.address.toString(16).toUpperCase().padStart(8, '0')} · unique match`);
      log(`Character emblem read — ${result.emblemId}, target 0x${result.address.toString(16).toUpperCase()}`, 'ok');
      toast('Emblem read', 'ok');
    } catch (error) {
      setEmblemStatus(error.message || 'Could not read the current emblem.', true);
      log(`Character emblem read failed: ${error.message}`, 'err');
      toast('Emblem read failed', 'err');
    } finally {
      if (button) button.disabled = false;
    }
  }

  async function setCharacterEmblem() {
    if (!ip || !document.getElementById('status-badge')?.classList.contains('connected')) {
      setEmblemStatus('Connect to the console first.', true);
      toast('Not connected', 'err');
      return;
    }
    const id = Number(document.getElementById('emblem-select')?.value);
    const generation = connectionGeneration;
    const button = document.getElementById('emblem-set-btn');
    if (button) button.disabled = true;
    setEmblemStatus('Rechecking the source location before writing…');
    try {
      if (emblemRestoreStack.length >= 32) throw new Error('Restore or review the saved emblem changes before making another edit');
      if (!Number.isInteger(id) || id < 0 || id > 0xFFFF) throw new Error('Select a valid 16-bit emblem');
      if (!window.RTM_EMBLEM_OPTIONS?.some(option => option.itemClass === 5 && Number(option.id) === id)) {
        throw new Error('Choose a source-listed emblem (item class 5); emote and other item IDs are not emblem selections');
      }
      const target = await locateCharacterEmblem(generation);
      const selectedHex = [...RtmMemoryFeatures.beU16(id)].map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
      const [targetBefore, currentBefore] = await Promise.all([
        ps3getmem(target.address, 2),
        ps3getmem(EMBLEM_VALUE_ADDR, 2)
      ]);
      ensureOperationSession(generation);
      if (targetBefore.toUpperCase() !== target.currentHex || currentBefore.toUpperCase() !== target.currentHex) {
        throw new Error('Emblem changed during validation; read it again before writing');
      }
      const restorePoint = { address: target.address, originalHex: target.currentHex, writtenHex: selectedHex, generation, ip };
      saveEmblemRestoreStack([...emblemRestoreStack, restorePoint]);
      document.getElementById('emblem-restore-btn').disabled = false;
      try {
        await verifiedMemorySequence([[target.address, selectedHex], [EMBLEM_VALUE_ADDR, selectedHex]]);
      } catch (writeError) {
        if (ip !== restorePoint.ip || connectionGeneration !== restorePoint.generation) {
          throw new Error(`Emblem write was interrupted by a connection change. Reconnect to ${restorePoint.ip} and use Restore to finish recovery.`);
        }
        const [targetAfter, currentAfter] = await Promise.all([
          ps3getmem(target.address, 2).catch(() => ''),
          ps3getmem(EMBLEM_VALUE_ADDR, 2).catch(() => '')
        ]);
        ensureOperationSession(generation);
        const targetValue = targetAfter.toUpperCase();
        const currentValue = currentAfter.toUpperCase();
        if (![target.currentHex, selectedHex].includes(targetValue) || ![target.currentHex, selectedHex].includes(currentValue)) {
          throw new Error(`Emblem write did not complete cleanly; memory changed unexpectedly and was left untouched: ${writeError.message}`);
        }
        const rollback = [];
        if (targetValue === selectedHex) rollback.push([target.address, target.currentHex]);
        if (currentValue === selectedHex) rollback.push([EMBLEM_VALUE_ADDR, target.currentHex]);
        if (rollback.length) {
          try {
            await verifiedMemorySequence(rollback);
          } catch (rollbackError) {
            throw new Error(`Emblem write failed and rollback could not be verified: ${rollbackError.message}`);
          }
        }
        saveEmblemRestoreStack(emblemRestoreStack.slice(0, -1));
        throw new Error(`Emblem write failed${rollback.length ? ' and was rolled back' : ''}: ${writeError.message}`);
      }
      ensureOperationSession(generation);
      document.getElementById('emblem-current').value = `${id} · 0x${selectedHex}`;
      setEmblemStatus(`Set emblem ${id} at both source locations · readback verified`);
      log(`Character emblem set to ${id}; both 16-bit writes verified`, 'ok');
      toast('Emblem updated', 'ok');
    } catch (error) {
      setEmblemStatus(error.message || 'Emblem update failed.', true);
      log(`Character emblem update failed: ${error.message}`, 'err');
      toast('Emblem update not confirmed', 'err');
    } finally {
      if (button) button.disabled = false;
    }
  }

  async function restoreCharacterEmblem() {
    const saved = emblemRestoreStack[emblemRestoreStack.length - 1];
    if (!saved || saved.ip !== ip || !document.getElementById('status-badge')?.classList.contains('connected')) {
      setEmblemStatus(saved ? `Reconnect to ${saved.ip} before restoring the saved emblem.` : 'Read and set an emblem before restoring.', true);
      toast(saved ? 'Reconnect to the saved console to restore' : 'No emblem restore point', 'err');
      return;
    }
    const generation = connectionGeneration;
    const button = document.getElementById('emblem-restore-btn');
    if (button) button.disabled = true;
    try {
      const [targetHex, currentHex] = await Promise.all([ps3getmem(saved.address, 2), ps3getmem(EMBLEM_VALUE_ADDR, 2)]);
      ensureOperationSession(generation);
      const targetValue = targetHex.toUpperCase();
      const currentValue = currentHex.toUpperCase();
      if (![saved.originalHex, saved.writtenHex].includes(targetValue) || ![saved.originalHex, saved.writtenHex].includes(currentValue)) {
        throw new Error('Emblem value changed since the write; refusing to overwrite it');
      }
      const restorePairs = [];
      if (targetValue === saved.writtenHex && targetValue !== saved.originalHex) restorePairs.push([saved.address, saved.originalHex]);
      if (currentValue === saved.writtenHex && currentValue !== saved.originalHex) restorePairs.push([EMBLEM_VALUE_ADDR, saved.originalHex]);
      if (restorePairs.length) await verifiedMemorySequence(restorePairs);
      ensureOperationSession(generation);
      saveEmblemRestoreStack(emblemRestoreStack.slice(0, -1));
      document.getElementById('emblem-current').value = `${parseInt(saved.originalHex, 16)} · 0x${saved.originalHex}`;
      document.getElementById('emblem-restore-btn').disabled = emblemRestoreStack.length === 0;
      setEmblemStatus(`Last emblem change restored · readback verified${emblemRestoreStack.length ? ` · ${emblemRestoreStack.length} earlier change(s) can also be restored` : ''}`);
      log('Original character emblem restored and verified', 'ok');
      toast('Original emblem restored', 'ok');
    } catch (error) {
      setEmblemStatus(error.message || 'Emblem restore failed.', true);
      log(`Character emblem restore failed: ${error.message}`, 'err');
      toast('Emblem restore not confirmed', 'err');
    } finally {
      if (button) button.disabled = emblemRestoreStack.length === 0;
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    populateEmblemOptions();
    const restoreButton = document.getElementById('emblem-restore-btn');
    if (restoreButton) restoreButton.disabled = emblemRestoreStack.length === 0;
    if (emblemRestoreStack.length) setEmblemStatus(`A saved emblem change can be restored on ${emblemRestoreStack[emblemRestoreStack.length - 1].ip}.`);
  });

  // PS3 system notification — uses PS3MAPI's built-in notify endpoint
  // Shows as an XMB-style popup bubble with the PS3 system icon
  async function sendPS3Notification() {
    if (!ip) { toast('Not connected', 'err'); return; }
    const msg = document.getElementById('notif-msg').value.trim();
    if (!msg) { toast('Enter a message', 'err'); return; }
    const encoded = encodeURIComponent(msg);
    try {
      await ps3fetch(`http://${ip}/notify.ps3mapi?msg=${encoded}`);
      log('PS3 system notification request dispatched; delivery is not confirmed', 'ok');
      toast('Notification request sent', 'ok');
    } catch (error) {
      log(`PS3 notification request failed: ${error.message}`, 'err');
      toast('Notification request failed', 'err');
    }
  }

  function notifyPS3(msg) {
    if (!ip) return;
    const encoded = encodeURIComponent(msg);
    ps3fetch(`http://${ip}/notify.ps3mapi?msg=${encoded}`).catch(() => {});
  }

  // ── Explicit player/physics hook control ─────────────────────────────────
  // This is the old AFKCrucible writePhysHook payload. Its branch site and code
  // cave overlap the Server Cheats and Overwalls physics hooks, so Link must
  // never install it implicitly. The Game Speed control writes 0x30586864,
  // while the old hook comment names 0x030606E4; the claimed dependency is not
  // established. Only switch the hook when its current bytes are recognized.
  const PLAYER_HOOK_SITE = 0x001C4110;
  const PLAYER_HOOK_CAVE = 0x010D8EC0;
  const PLAYER_HOOK_ORIGINAL = '30640140';
  const PLAYER_HOOK_BRANCH = '48F14DB0';
  const PLAYER_HOOK_STUB_HEX = '3C6001906063EBB0F9C3FFD8F9E3FFD0DA03FFE0DA23FFE87DE0002681C30014C2040140C22E0000FC10884040820028C2040144C22E0004FC10884040820018C2040148C22E0008FC10884040820008908300007DEFF120E9C3FFD8E9E3FFD0CA03FFE0CA23FFE8306401403C80010A4B0EB1E4';
  const PLAYER_HOOK_TAIL = 0x010D8F34;
  const PLAYER_HOOK_TAIL_LENGTH = 152;
  const PLAYER_HOOK_DATA = [
    ['0190EBC4','306E8D58'], ['0190EBAC','3F99999A'], ['0190EBD0','C499DA3F'],
    ['0190EBD4','C41B299C'], ['0190EBD8','4230BCFE'], ['0190EBE0','C49979E6'],
    ['0190EBE4','C42005F0'], ['0190EBE8','41F563DA'], ['0190EBF0','C499A918'],
    ['0190EBF4','C425A282'], ['0190EBF8','420AF6F6'], ['0190EBFC','00000001'],
    ['0190EC00','00000002'],
  ];
  let hookInstalling = false;
  let playerHookTarget = null;
  let playerHookPayload = '';

  function setPlayerHookStatus(message, isError = false) {
    const status = document.getElementById('player-hook-status');
    if (!status) return;
    status.textContent = message;
    status.style.color = isError ? 'var(--danger)' : 'var(--muted)';
  }

  async function readHookRange(address, length) {
    let output = '';
    for (let offset = 0; offset < length; offset += 0x100) {
      const chunkLength = Math.min(0x100, length - offset);
      const chunk = String(await ps3getmem(address + offset, chunkLength) || '').replace(/\s/g, '').toUpperCase();
      if (chunk.length !== chunkLength * 2) throw new Error(`Short hook read at 0x${(address + offset).toString(16).toUpperCase()}`);
      output += chunk;
    }
    return output;
  }

  function recognizedHookPayloads() {
    return [
      PLAYER_HOOK_STUB_HEX,
      typeof OFF_HEX === 'string' ? OFF_HEX : '',
      typeof VOID_ON_HEX === 'string' ? VOID_ON_HEX : '',
      typeof ROPK_ON_HEX === 'string' ? ROPK_ON_HEX : '',
    ].filter((payload, index, all) => payload && all.indexOf(payload) === index);
  }

  async function installPlayerHook() {
    if (!ip) throw new Error('Link the console before installing the player hook');
    if (hookInstalling || srvBusy) throw new Error('Another hook write is already active');
    const generation = connectionGeneration;
    const target = ip;
    hookInstalling = true;
    const write = async (address,value) => {
      if (generation !== connectionGeneration) throw new Error('Hook session changed');
      await verifiedMemoryValue(address,value);
      if (generation !== connectionGeneration) throw new Error('Hook session changed');
    };
    try {
      const current = String(await ps3getmem(PLAYER_HOOK_SITE,4) || '').toUpperCase();
      if (generation !== connectionGeneration) throw new Error('Hook session changed');
      if (current === PLAYER_HOOK_BRANCH) {
        const activeStub = await readHookRange(PLAYER_HOOK_CAVE, PLAYER_HOOK_STUB_HEX.length / 2);
        const activeTail = await readHookRange(PLAYER_HOOK_TAIL, PLAYER_HOOK_TAIL_LENGTH);
        if (activeStub !== PLAYER_HOOK_STUB_HEX || /[^0]/.test(activeTail)) {
          throw new Error('Another hook is active at the shared 0x001C4110 / 0x010D8EC0 site; restore it before switching');
        }
        playerHookTarget = target;
        playerHookPayload = PLAYER_HOOK_STUB_HEX;
        setPlayerHookStatus('Base player hook is already active; no code was rewritten.');
        return;
      }
      if (current !== PLAYER_HOOK_ORIGINAL) throw new Error('Hook site does not match the supported BLES 01.24 instruction');

      const oldStub = await readHookRange(PLAYER_HOOK_CAVE, PLAYER_HOOK_STUB_HEX.length / 2);
      const oldTail = await readHookRange(PLAYER_HOOK_TAIL, PLAYER_HOOK_TAIL_LENGTH);
      if ((oldStub !== '0'.repeat(PLAYER_HOOK_STUB_HEX.length) && oldStub !== PLAYER_HOOK_STUB_HEX) || /[^0]/.test(oldTail)) {
        throw new Error('The inactive hook cave contains unrecognized data; it was left untouched');
      }

      // Write the PPC stub and empty tail first; activate the branch last.
      const stubChunks = PLAYER_HOOK_STUB_HEX.match(/.{8}/g) || [];
      for (let i = 0; i < stubChunks.length; i++) {
        await write(PLAYER_HOOK_CAVE + i * 4, stubChunks[i]);
      }
      for (let i = 0; i < PLAYER_HOOK_TAIL_LENGTH / 4; i++) {
        await write(PLAYER_HOOK_TAIL + i * 4, '00000000');
      }
      for (const [address,value] of PLAYER_HOOK_DATA) await write(address,value);

      // Track ownership before the final write in case the response is lost
      // after the console has already activated the branch.
      playerHookTarget = target;
      playerHookPayload = PLAYER_HOOK_STUB_HEX;
      await write(PLAYER_HOOK_SITE, PLAYER_HOOK_BRANCH);
      setPlayerHookStatus('Player hook installed. Link alone does not install it.');
      log('Player hook installed and activation branch verified', 'ok');
    } catch(e) {
      setPlayerHookStatus(`Hook not confirmed: ${e.message}`, true);
      log(`Player hook not confirmed: ${e.message}`, 'err');
      throw e;
    } finally {
      hookInstalling = false;
    }
  }

  async function uninstallPlayerHook({ allowRecognized = false } = {}) {
    if (!ip) throw new Error('Link the console before restoring a hook');
    const target = ip;
    if (!allowRecognized && playerHookTarget !== target) return;
    const generation = connectionGeneration;
    const current = String(await ps3getmem(PLAYER_HOOK_SITE,4) || '').toUpperCase();
    ensureOperationSession(generation);
    if (current === PLAYER_HOOK_ORIGINAL) {
      if (playerHookTarget === target) { playerHookTarget = null; playerHookPayload = ''; }
      setPlayerHookStatus('Hook branch is already at its original instruction.');
      return;
    }
    if (current !== PLAYER_HOOK_BRANCH) throw new Error('Hook site changed; restoration was not attempted');

    let expectedPayload = playerHookTarget === target ? playerHookPayload : '';
    if (!expectedPayload && allowRecognized) {
      for (const candidate of recognizedHookPayloads()) {
        const candidateBytes = await readHookRange(PLAYER_HOOK_CAVE, candidate.length / 2);
        ensureOperationSession(generation);
        if (candidateBytes === candidate) { expectedPayload = candidate; break; }
      }
    }
    if (!expectedPayload) throw new Error('This page did not install the active hook; use the explicit recovery action to inspect known payloads');
    const actualPayload = await readHookRange(PLAYER_HOOK_CAVE, expectedPayload.length / 2);
    ensureOperationSession(generation);
    if (actualPayload !== expectedPayload) throw new Error('The code cave no longer matches this hook; another feature owns it, so it was left active');

    await verifiedConditionalMemoryValue(
      PLAYER_HOOK_SITE.toString(16), PLAYER_HOOK_BRANCH, PLAYER_HOOK_ORIGINAL
    );
    if (playerHookTarget === target) { playerHookTarget = null; playerHookPayload = ''; }
    setPlayerHookStatus('Hook branch restored. Its code and data bytes remain inert in Destiny memory.');
    log('Owned player/server hook branch restored', 'ok');
  }

  window.installPlayerHookAction = async () => {
    if (!ip) { setPlayerHookStatus('Link the console first.', true); return; }
    if (!await RtmDialogs.confirm('Install the experimental player/physics hook? This writes PPC code and redirects Destiny at 0x001C4110. It shares the code cave used by Server Cheats and Overwalls.')) return;
    try { await installPlayerHook(); }
    catch (error) { toast(`Hook not installed: ${error.message}`, 'err'); }
  };

  window.restorePlayerHookAction = async () => {
    if (!ip) { setPlayerHookStatus('Link the console first.', true); return; }
    if (!await RtmDialogs.confirm('Restore Destiny’s original instruction at 0x001C4110 if the active hook matches a known RTM payload?')) return;
    try { await uninstallPlayerHook({ allowRecognized: true }); }
    catch (error) { setPlayerHookStatus(`Hook left unchanged: ${error.message}`, true); toast(`Hook not restored: ${error.message}`, 'err'); }
  };

  const toggleState = {};

  // size: 4 (default, UInt32), 2 (UInt16), or 1 (byte) — must match what the
  // solution uses or adjacent memory gets corrupted (e.g. immunity bug)
  async function fire(addr, val, label, restoreVal, size = 4) {
    const generation = connectionGeneration;
    const verifiedWrite = async (address,value) => {
      if (generation !== connectionGeneration) throw new Error('Console session changed');
      await verifiedMemoryValue(address,value);
      if (generation !== connectionGeneration) throw new Error('Console session changed');
    };
    if (!ip) { toast('Not connected', 'err'); return; }
    const key = addr + '_' + label;
    const isToggle = restoreVal !== undefined;
    const isOn = isToggle ? !toggleState[key] : true;
    const writeVal = (isToggle && toggleState[key]) ? restoreVal : val;
    const stateLabel = isToggle ? (isOn ? ' ON' : ' OFF') : '';
    const sizeParam = size !== 4 ? `&size=${size}` : '';

    try {
      await verifiedWrite(`${addr}`,`${writeVal}`);
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
    const generation = connectionGeneration;
    const verifiedWrite = async (address,value) => {
      if (generation !== connectionGeneration) throw new Error('Console session changed');
      await verifiedMemoryValue(address,value);
      if (generation !== connectionGeneration) throw new Error('Console session changed');
    };
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
          await verifiedWrite(`${addr}`,`${v}`);
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
        await verifiedWrite(`${addr}`,`${val}`);
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

  async function sendCustom() {
    const addr = document.getElementById('c-addr').value.trim().replace(/^0x/i,'');
    const value = document.getElementById('c-val').value.trim().replace(/^0x/i,'');
    const bytes = advancedMode ? writeSize : 4;
    if (![1,2,4].includes(bytes) || !/^[0-9a-f]{1,8}$/i.test(addr) || !/^[0-9a-f]+$/i.test(value) || value.length>bytes*2) { toast('Enter a valid address and value for the selected width','err'); return; }
    try {
      await verifiedMemoryValue(addr,value.padStart(bytes*2,'0'));
      toast('Custom write verified','ok');
    } catch (error) { log(`Custom write not confirmed: ${error.message}`,'err'); toast('Custom write not confirmed','err'); }
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
      await verifiedMemoryValue('30586864',hex);
      log(`Gamespeed → x${speed} (${hex})`, 'ok');
      toast(`Gamespeed x${speed} `, 'ok');
    } catch(e) { log('Gamespeed write failed', 'err'); toast('Gamespeed failed', 'err'); }
  }

  async function resetGamespeed() {
    if (!ip) { toast('Not connected', 'err'); return; }
    document.getElementById('gs-input').value = '1';
    try {
      await verifiedMemoryValue('30586864','3F800000');
      log('Gamespeed reset → x1', 'ok');
      toast('Gamespeed reset ', 'ok');
    } catch(e) { log('Gamespeed reset failed', 'err'); toast('Gamespeed reset failed', 'err'); }
  }

  // FOV is represented directly (2.0–3.0 on the slider), not as a 0–10 offset.
  const FOV_ADDR = 0x0070906C;
  function floatToHex(value) {
    if (!RtmBackups.coordinate(value)) throw new Error('Value cannot be represented as a finite float');
    const bytes = new Uint8Array(4); new DataView(bytes.buffer).setFloat32(0, Number(value), false);
    return Array.from(bytes, byte => byte.toString(16).padStart(2,'0')).join('').toUpperCase();
  }
  function updateFOV(value) {
    if (!Number.isFinite(Number(value))) return;
    document.getElementById('fov-val').textContent = Number(value).toFixed(2);
    const custom = document.getElementById('fov-custom');
    if (custom && document.activeElement !== custom) custom.value = String(value);
  }
  function fovDisplay(value) { return Number(value).toFixed(2); }
  async function verifiedMemorySequence(pairs, timeoutMs = 8000) {
    const result = await patchRunner.apply(pairs,{timeoutMs});
    if (result.status !== 'verified') throw new Error('Write acknowledged but readback unavailable; value not confirmed');
    return result;
  }
  async function verifiedConditionalMemorySequence(pairs, preconditions, timeoutMs = 8000) {
    const result = await patchRunner.apply(pairs, { timeoutMs, preconditions });
    if (result.status !== 'verified') throw new Error('Conditional write acknowledged but readback unavailable; value not confirmed');
    return result;
  }
  async function verifiedMemoryValue(address, hex, timeoutMs = 8000) {
    const result = await verifiedMemorySequence([[address,hex]],timeoutMs);
    if (result.status !== 'verified') throw new Error('Write acknowledged but readback unavailable; value not confirmed');
    return hex;
  }
  async function verifiedConditionalMemoryValue(address, expectedHex, hex, beforeWrite) {
    const options = { timeoutMs: 8000, preconditions: [[address, expectedHex]] };
    if (beforeWrite) options.beforeWrite = beforeWrite;
    const result = await patchRunner.apply([[address, hex]], options);
    if (result.status !== 'verified') throw new Error('Write acknowledged but readback unavailable; value not confirmed');
    return hex;
  }
  async function writeFOVValue(value) {
    const number = Number(value);
    if (!RtmBackups.coordinate(value) || number <= 0) throw new Error('Enter a positive finite FOV value');
    return verifiedMemoryValue('70906C', floatToHex(number));
  }
  async function applyFOV() {
    const value = Number(document.getElementById('fov-slider').value);
    try { await writeFOVValue(value); updateFOV(value); toast(`FOV ${value} verified`, 'ok'); }
    catch (error) { log(`FOV not confirmed: ${error.message}`, 'err'); toast('FOV not confirmed', 'err'); }
  }
  async function applyCustomFOV() {
    const input = document.getElementById('fov-custom'); if (!input) return;
    const value = Number(input.value);
    try {
      await writeFOVValue(input.value);
      if (value >= 2 && value <= 3) document.getElementById('fov-slider').value = String(value);
      updateFOV(value); toast(`FOV ${value} verified`, 'ok');
    } catch (error) { log(`FOV not confirmed: ${error.message}`, 'err'); toast('FOV not confirmed', 'err'); }
  }
  async function resetFOV() {
    try {
      await writeFOVValue(2);
      document.getElementById('fov-slider').value = '2'; updateFOV(2);
      toast('FOV 2.00 verified', 'ok');
    } catch (error) { log(`FOV reset not confirmed: ${error.message}`, 'err'); toast('FOV reset not confirmed', 'err'); }
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
    return verifiedMemorySequence((hex.match(/.{8}/g)||[]).map((value,index)=>[(baseAddr+index*4).toString(16),value]));
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
    const operationGeneration = connectionGeneration;
    if (!ip)        { updateCheatStatus('tog-anti-mm', 'failed'); toast('Not connected', 'err'); return; }
    if (antiMMBusy) { toast('Please wait…', 'err'); return; }

    const willEnable = !antiMMState;
    if (willEnable && !confirm('Only activate Anti-Matchmaking from Orbit or the Tower.\n\nActivating during matchmaking WILL crash the game.\n\nContinue?')) return;

    antiMMBusy = true;
    updateCheatStatus('tog-anti-mm', 'applying');
    const val = willEnable ? '3BA0005E' : 'A3BF00DA';
    try {
      ensureOperationSession(operationGeneration);
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
    { id: 'sec-server-cheats',       label: 'Server Cheats',   setup: true,  description: 'Server-side and session-related options.' },    { id: 'sec-character-utils',     label: 'Character Utilities', setup: true, description: 'Read and edit character name, emblem, and in-memory player scale.' },    { id: 'sec-gamespeed',           label: 'Gamespeed',       setup: true,  description: 'Game-speed controls and presets.' },
    { id: 'sec-damage-tuning',       label: 'Damage Tuning',   setup: true,  description: 'Custom damage-value controls and presets.' },
    { id: 'sec-fov',                 label: 'FOV',             setup: true,  description: 'Field-of-view controls.' },
    { id: 'sec-overwalls',            label: 'Overwalls Tools', setup: true,  description: 'Overwalls scenario, routine, inventory and Flyhack workflows.' },
    { id: 'sec-local-data',           label: 'Local Data & Diagnostics', setup: true, description: 'Read-only game snapshots, offline payloads and user-supplied catalogs.' },
    { id: 'sec-custom-offset',       label: 'Custom Offset',   setup: true,  description: 'Manual memory address/value tools.', recommended: false },
    { id: 'sec-ps3-notify',          label: 'PS3 System Notification', setup: true, description: 'Send a documented PS3MAPI system notification.' },
    { id: 'sec-jukebox',             label: 'PS3 Jukebox',     setup: true, description: 'Play source-defined PS3 system tone sequences.' },
    { id: 'sec-scanner',             label: 'Memory Scanner',  setup: true,  description: 'Compare bounded memory snapshots and inspect selected values.', recommended: false },
    { id: 'sec-ammo-editor',         label: 'Ammo Editor',     setup: true,  description: 'Experimental source-build scanner; writes one selected ammo slot.', recommended: false },
    { id: 'sec-screenshots',         label: 'Screenshots',     setup: true,  description: 'Experimental source-build framebuffer addresses; may pause the game.', recommended: false },
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

  const MEMORY_TOOLS_VISIBILITY_MIGRATION_KEY = 'destinyRtmMemoryToolsOptInV1';
  try {
    if (localStorage.getItem(MEMORY_TOOLS_VISIBILITY_MIGRATION_KEY) !== '1') {
      for (const id of ['sec-ammo-editor', 'sec-screenshots']) {
        const key = SECTION_STORAGE_PREFIX + id;
        if (localStorage.getItem(key) === null && sessionStorage.getItem('secvis_' + id) === null) {
          localStorage.setItem(key, '0');
        }
      }
      localStorage.setItem(MEMORY_TOOLS_VISIBILITY_MIGRATION_KEY, '1');
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
    const styles = getComputedStyle(document.documentElement);
    const current = styles.getPropertyValue('--accent-choice').trim() || styles.getPropertyValue('--accent').trim();
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
    if (typeof setAccent === 'function') setAccent('#c4b784');
    if (picker) picker.value = '#c4b784';
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

  // ── Optional PS3/XMB notification automation ─────────────────────────────
  const NOTIFICATION_AUTOMATION_KEY = 'destiny_rtm_notification_automation_v1';
  const NOTIFICATION_AUTOMATION_DEFAULTS = Object.freeze({
    master:false, connect:true, activity:true, cheat:false, error:true, restore:true,
    template:'Destiny RTM: {event}', cooldown:5
  });
  const NOTIFICATION_COOLDOWNS = new Set([0,2,5,10,30]);
  let notificationAutomation = {...NOTIFICATION_AUTOMATION_DEFAULTS};
  let notificationLastSentAt = 0;
  let notificationRequestCount = 0;

  function readNotificationAutomation() {
    let saved = {};
    try { saved = JSON.parse(localStorage.getItem(NOTIFICATION_AUTOMATION_KEY) || '{}') || {}; } catch (e) {}
    const cooldown = Number(saved.cooldown);
    notificationAutomation = {
      master:saved.master === true,
      connect:saved.connect !== false,
      activity:saved.activity !== false,
      cheat:saved.cheat === true,
      error:saved.error !== false,
      restore:saved.restore !== false,
      template:typeof saved.template === 'string' ? saved.template.slice(0,128) : NOTIFICATION_AUTOMATION_DEFAULTS.template,
      cooldown:NOTIFICATION_COOLDOWNS.has(cooldown) ? cooldown : NOTIFICATION_AUTOMATION_DEFAULTS.cooldown
    };
    applyNotificationAutomationUI();
  }

  function applyNotificationAutomationUI() {
    const fields = {
      master:'auto-notif-master', connect:'auto-notif-connect', activity:'auto-notif-activity',
      cheat:'auto-notif-cheat', error:'auto-notif-error', restore:'auto-notif-restore'
    };
    Object.entries(fields).forEach(([key,id]) => {
      const input = document.getElementById(id);
      if (input) input.checked = !!notificationAutomation[key];
    });
    const template = document.getElementById('auto-notif-template');
    if (template) template.value = notificationAutomation.template;
    const cooldown = document.getElementById('auto-notif-cooldown');
    if (cooldown) cooldown.value = String(notificationAutomation.cooldown);
    updateNotificationAutomationStatus(notificationAutomation.master
      ? `Automatic notices enabled · ${notificationRequestCount} request${notificationRequestCount === 1 ? '' : 's'} sent this session.`
      : 'Automatic notices are off.');
  }

  function updateNotificationAutomationStatus(message) {
    const status = document.getElementById('notif-automation-status');
    if (status) status.textContent = message;
  }

  function saveNotificationAutomationFromUI() {
    const fields = {
      master:'auto-notif-master', connect:'auto-notif-connect', activity:'auto-notif-activity',
      cheat:'auto-notif-cheat', error:'auto-notif-error', restore:'auto-notif-restore'
    };
    Object.entries(fields).forEach(([key,id]) => {
      const input = document.getElementById(id);
      if (input) notificationAutomation[key] = !!input.checked;
    });
    const template = document.getElementById('auto-notif-template');
    if (template) notificationAutomation.template = template.value.slice(0,128) || NOTIFICATION_AUTOMATION_DEFAULTS.template;
    const cooldown = Number(document.getElementById('auto-notif-cooldown')?.value);
    notificationAutomation.cooldown = NOTIFICATION_COOLDOWNS.has(cooldown)
      ? cooldown : NOTIFICATION_AUTOMATION_DEFAULTS.cooldown;
    try { localStorage.setItem(NOTIFICATION_AUTOMATION_KEY, JSON.stringify(notificationAutomation)); } catch (e) {}
    updateNotificationAutomationStatus(notificationAutomation.master
      ? `Automatic notices enabled · ${notificationRequestCount} request${notificationRequestCount === 1 ? '' : 's'} sent this session.`
      : 'Automatic notices are off.');
  }

  function notificationAutomationAllows(kind) {
    return notificationAutomation.master && notificationAutomation[kind] === true;
  }

  function dispatchNotificationAutomation(event, data = {}) {
    if (!ip) return {sent:false, reason:'Connect to a PS3 first.'};
    const now = Date.now();
    const cooldownMs = notificationAutomation.cooldown * 1000;
    const waitMs = cooldownMs - (now - notificationLastSentAt);
    if (cooldownMs && waitMs > 0) return {sent:false, reason:`Wait ${Math.ceil(waitMs / 1000)} seconds before the next notice.`};

    const values = {
      event:String(event || ''), activity:String(data.activity || ''),
      cheat:String(data.cheat || ''), ip:String(data.ip || ip || '')
    };
    const message = String(notificationAutomation.template || NOTIFICATION_AUTOMATION_DEFAULTS.template)
      .replace(/\{event\}/g, values.event)
      .replace(/\{activity\}/g, values.activity)
      .replace(/\{cheat\}/g, values.cheat)
      .replace(/\{ip\}/g, values.ip)
      .trim().slice(0,128);
    if (!message) return {sent:false, reason:'Enter a message template first.'};

    notificationLastSentAt = now;
    notifyPS3(message);
    notificationRequestCount++;
    updateNotificationAutomationStatus(`Request dispatched · ${notificationRequestCount} this session. PS3 delivery is not confirmed.`);
    return {sent:true, message};
  }

  function automationNotify(event, kind, data = {}) {
    const fallbackKind = kind === 'connect' && data.fallbackKind === 'error' ? 'error' : '';
    if (!notificationAutomationAllows(kind) && (!fallbackKind || !notificationAutomationAllows(fallbackKind))) {
      return {sent:false, reason:'This event is disabled.'};
    }
    return dispatchNotificationAutomation(event, data);
  }

  function testNotificationAutomation() {
    const result = dispatchNotificationAutomation('Automation test', {ip});
    if (!result.sent) {
      updateNotificationAutomationStatus(result.reason);
      toast(result.reason, 'err');
      return;
    }
    log('Test PS3 notification request dispatched; delivery is not confirmed', 'ok');
    toast('Test request sent', 'ok');
  }

  function resetNotificationAutomation() {
    notificationAutomation = {...NOTIFICATION_AUTOMATION_DEFAULTS};
    try { localStorage.setItem(NOTIFICATION_AUTOMATION_KEY, JSON.stringify(notificationAutomation)); } catch (e) {}
    applyNotificationAutomationUI();
    toast('Notification preferences reset', 'ok');
  }

  window.automationNotify = automationNotify;
  window.testNotificationAutomation = testNotificationAutomation;
  window.resetNotificationAutomation = resetNotificationAutomation;

  document.addEventListener('DOMContentLoaded', () => {
    readNotificationAutomation();
    document.querySelectorAll('#notification-automation input, #notification-automation select').forEach(input => {
      input.addEventListener('change', saveNotificationAutomationFromUI);
      if (input.id === 'auto-notif-template') input.addEventListener('input', saveNotificationAutomationFromUI);
    });
  });

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
    if (!Number.isInteger(value) || value < 0 || value > 65535) throw new Error('Invalid activity index');
    await verifiedMemoryValue(addr.toString(16),value.toString(16).padStart(4,'0'));
  }

  async function loadActivity(index) {
    const operationGeneration = connectionGeneration;
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      for (const addr of ACT_ADDRS) {
        ensureOperationSession(operationGeneration);
        await ps3writeU16(addr, index);
      }
      const name = ALL_ACTIVITIES[index - 1] || `Activity ${index}`;
      log(`Activity index writes verified: ${name}; load completion not observed`, 'ok');
      automationNotify(`Activity request written: ${name}`, 'activity', {activity:name, ip});
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

  // Mirrors the local Activity Loader source: activityRead() reads decimal
  // 811954340 (0x306570A4), the second address in the write list.
  // Read 4 bytes and take the first 2 (big-endian UInt16) — reading len=2 via
  // PS3MAPI can return a padded/misaligned response that inflates the value.
  async function readCurrentActivity() {
    if (!ip) { toast('Not connected', 'err'); return; }
    try {
      // ReadUInt16 on PS3 (big-endian): read 2 bytes — the index is in the
      // FIRST 2 bytes (hex chars 0–3), not the last 2.  The old slice(4,8)
      // was reading the wrong half and producing garbage values.
      const hex = await ps3getmem(ACT_ADDRS[1], 2);
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
    // Browser storage records prior UI intent, never current console memory.
    // A new page/session starts unknown until a write and readback succeed.
    document.querySelectorAll('[id^="tog-"], [id^="srv-"]').forEach(element => {
      element.classList.remove('active-cheat');
      element.dataset.consoleState = 'unknown';
    });
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
    overwalls: [
      'sec-overwalls'
    ],
    tools: [
      'sec-custom-offset',
      'sec-ps3-notify',
      'sec-jukebox',
      'sec-ammo-editor',
      'sec-screenshots',
      'sec-scanner',
      'sec-useless',
      'sec-local-data'
    ]
  };

  function getRTMTab() {
    const saved = localStorage.getItem(RTM_TAB_KEY) || 'home';
    return ['home','mods','player','overwalls','tools','all'].includes(saved) ? saved : 'home';
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

    ['home','mods','player','overwalls','tools','all'].forEach(tab => {
      document.getElementById(`rtm-tab-${tab}`)?.classList.toggle('active', tab === activeTab);
    });
  }

  function setRTMTab(tab) {
    if (!['home','mods','player','overwalls','tools','all'].includes(tab)) return;
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
      notes:'Instance Leader can change the behaviour of this option. It also conflicts with the Damage Preset, so only one should be considered active.'
    },
    'tog-ammo': {
      feature:'stable', label:'Stable',
      description:'Keeps weapons supplied with ammunition.',
      notes:'Current known issue: this option may not restore correctly when switched off.'
    },
    'tog-no-spread': {
      feature:'stable', label:'Stable',
      description:'Reduces weapon spread while enabled.',
      notes:'Ported from the local Destiny v1.24 source. The toggle writes the source values and confirms each write by readback.'
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
    'tog-jump-boost': {
      feature:'experimental', label:'Experimental',
      description:'Applies the source RTM’s four-times jump-boost patch.',
      notes:'Shares 0x00CCB9E8 with Unlimited Abilities. Enabling either option disables the other first.'
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
    'tog-bouncy-items': {
      feature:'experimental', label:'Experimental',
      description:'Applies the source RTM’s bouncy-item patch.',
      notes:'The original source provides fixed on/off values but no further usage notes.'
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
      notes:'Also affects some pull/push behaviour and may change encounter movement. Shares a 12-byte patch with Rock Jump; enabling either disables the other.'
    },
    'tog-rock-jump': {
      feature:'experimental', label:'Experimental',
      description:'Applies the Rock Jump values from the local source RTM.',
      notes:'Shares a 12-byte patch with No Pushback Barriers; enabling either disables the other.'
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
      feature:'experimental', label:'Experimental',
      description:'Writes the source damage preset at 0x0043E626.',
      notes:'Overwalls C# and the legacy HTML toggle call this 4× Damage, while the HTML calculator maps the same bytes to its 10× preset. Its in-game multiplier has not been verified.'
    },
    'srv-off': {
      feature:'stable', label:'Safe State',
      description:'Restores the Server Cheats mode to its normal off state.',
      notes:'Use this when you want to stop the active server-cheat mode.'
    }
  };

  const cheatRuntimeStatus = Object.create(null);

  function normalizeCheatStatus(state) {
    return ['unknown','on','off','verified-on','verified-off','applying','failed'].includes(state) ? state : 'off';
  }

  function cheatStatusText(state) {
    return ({
      unknown:'UNKNOWN',
      'verified-on':'ON VERIFIED',
      'verified-off':'OFF VERIFIED',
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
    'tog-unl-abilities': ['tog-power', 'tog-jump-boost'],
    'tog-jump-boost': ['tog-unl-abilities'],

    // These two source features write different values to the same 12 bytes.
    'tog-no-pushbacks': ['tog-rock-jump'],
    'tog-rock-jump': ['tog-no-pushbacks']
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
    const operationGeneration = connectionGeneration;
    if (id === 'tog-power') {
      ensureOperationSession(operationGeneration);
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
        ensureOperationSession(operationGeneration);
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

  const PATCH_CONFLICT_OFF = {
    'tog-no-pushbacks': {
      label: 'No Pushback Barriers',
      pairs: [['1389760','3F800000'],['1389764','3F800000'],['1389768','3F800000']]
    },
    'tog-rock-jump': {
      label: 'Rock Jump',
      pairs: [['1389760','3F800000'],['1389764','3F800000'],['1389768','3F800000']]
    },
    'tog-jump-boost': {
      label: '4× Jump Boost',
      pairs: [['CCB9E8','3063FFFF']]
    },
    'tog-unl-abilities': {
      label: 'Unlimited Abilities',
      pairs: [['2C1DB4','D03B019C'],['CCB9E8','3063FFFF'],['2C36FC','C3C30000']]
    }
  };

  async function disablePatchConflict(id) {
    const definition = PATCH_CONFLICT_OFF[id];
    if (!definition || !cheatState[definition.label]) return null;
    const generation = connectionGeneration;
    const result = await patchRunner.apply(definition.pairs);
    if (generation !== connectionGeneration) throw new Error('Console session changed');
    if (result.status !== 'verified') throw new Error(`${definition.label} could not be disabled and verified`);
    cheatState[definition.label] = false;
    requestedCheatState[definition.label] = false;
    setAllButtonsActive(id, false);
    updateCheatStatus(id, 'verified-off');
    return definition.label;
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
      if (targetId !== 'srv-4x-damage' && server4xDamageState) disabled.push('Damage Preset');
      clearDamageConflictUI(targetId);
    }

    // Power conflict needs real OFF writes because Unlimited Abilities contains
    // additional patches beyond the shared address.
    if (targetId === 'tog-power' && cheatState['Unlimited Abilities']) {
      disabled.push(await disablePowerConflict('tog-unl-abilities'));
    } else if (targetId === 'tog-unl-abilities' && cheatState['Infinite Power']) {
      disabled.push(await disablePowerConflict('tog-power'));
    }

    for (const conflictId of conflicts) {
      if (PATCH_CONFLICT_OFF[conflictId] && cheatState[PATCH_CONFLICT_OFF[conflictId].label]) {
        disabled.push(await disablePatchConflict(conflictId));
      }
    }

    const actualDisabled = disabled.filter(Boolean);
    if (actualDisabled.length) {
      const msg = `Conflict detected: disabled ${actualDisabled.join(' + ')}`;
      log(msg, 'ok');
      toast(msg, 'ok');
    }

    return disabled;
  }

  const buildCompatibilityGuard = RtmBuildCompatibility.create({
    session: () => ip && document.getElementById('status-badge').classList.contains('connected') ? `${connectionGeneration}:${ip}` : '',
    read: (address, length) => ps3getmem(parseInt(address, 16), length)
  });
  function activitySessionOwner() {
    const badge = document.getElementById('status-badge');
    return ip && badge?.classList.contains('connected') ? `${connectionGeneration}:${ip}` : '';
  }
  const activitySessionReadCapabilities = new WeakMap();
  const ACTIVITY_READ_MAX = 0x100; // webMAN PS3MAPI clamps GETMEM to 256 bytes per request.
  const activitySessionMemoryApi = Object.freeze({
    session: activitySessionOwner,
    async verify(expectedSession) {
      if (!expectedSession || activitySessionOwner() !== expectedSession) throw new Error('Console session changed; reconnect and retry');
      const result = await buildCompatibilityGuard.inspect();
      if (activitySessionOwner() !== expectedSession) throw new Error('Console session changed during build verification');
      if (result.status !== 'verified' || result.profile !== 'BLES01857 01.24 signature profile') {
        throw new Error('BLES01857 01.24 signature check failed; session inspection refused');
      }
      const capability = Object.freeze({});
      activitySessionReadCapabilities.set(capability, { session: expectedSession, reads: 0, bytes: 0 });
      return { profile: result.profile, anchors: result.anchors.length, capability };
    },
    async read(address, length, expectedSession, capability) {
      const budget = activitySessionReadCapabilities.get(capability);
      if (!budget || budget.session !== expectedSession) throw new Error('Verify the BLES01857 01.24 profile before reading session memory');
      if (!expectedSession || activitySessionOwner() !== expectedSession) throw new Error('Console session changed; read refused');
      if (!Number.isSafeInteger(address) || address < 0x10000 || address >= 0x40000000 ||
          !Number.isInteger(length) || length < 1 || length > 0x600 || address + length > 0x40000000) {
        throw new Error('Activity inspector read is outside its bounded PS3 user-memory range');
      }
      const requestCount = Math.ceil(length / ACTIVITY_READ_MAX);
      if (budget.reads + requestCount > 192 || budget.bytes + length > 0x8000) throw new Error('Activity inspector snapshot read budget exceeded');
      budget.reads += requestCount;
      budget.bytes += length;
      return RtmMemoryReadChunks.readHexChunks(address, length, ps3getmem, {
        chunkBytes: ACTIVITY_READ_MAX,
        assertCurrent() {
          if (activitySessionOwner() !== expectedSession) throw new Error('Console session changed during memory read');
        }
      });
    }
  });
  window.rtmActivitySessionMemory = activitySessionMemoryApi;
  function ensureDestinyWriteProfile() {
    return buildCompatibilityGuard.ensure();
  }
  window.ensureDestinyWriteProfile = ensureDestinyWriteProfile;
  let buildProfileCheckBusy = false;
  function setDestinyBuildProfileStatus(message, state = '') {
    const status = document.getElementById('build-profile-status');
    if (!status) return;
    status.textContent = message;
    if (state) status.dataset.state = state;
    else delete status.dataset.state;
  }
  function clearDestinyBuildProfileResults() {
    const list = document.getElementById('build-profile-results');
    if (!list) return;
    list.replaceChildren();
    list.hidden = true;
  }
  function renderDestinyBuildProfileResults(anchors) {
    const list = document.getElementById('build-profile-results');
    if (!list) return;
    list.replaceChildren();
    for (const anchor of anchors) {
      const item = document.createElement('li');
      item.className = 'build-profile-result';
      item.dataset.state = anchor.status;

      const identity = document.createElement('span');
      identity.className = 'build-profile-result-identity';
      const address = document.createElement('code');
      address.textContent = `0x${anchor.address}`;
      const role = document.createElement('small');
      role.textContent = anchor.role || 'Code anchor';
      identity.append(address, role);
      const state = document.createElement('span');
      state.className = 'build-profile-result-state';
      state.textContent = anchor.status === 'match' ? 'Match' : anchor.status === 'mismatch' ? 'Mismatch' : 'Unreadable';
      const detail = document.createElement('span');
      detail.className = 'build-profile-result-detail';
      if (anchor.status === 'match') {
        const observed = document.createElement('code');
        observed.textContent = anchor.observed;
        detail.append(observed);
      } else if (anchor.status === 'mismatch') {
        const observed = document.createElement('code');
        const expected = document.createElement('code');
        observed.textContent = anchor.observed;
        expected.textContent = anchor.expected;
        detail.append('Observed ', observed, ' · expected ', expected);
      } else detail.textContent = anchor.error || 'No bytes returned';

      item.append(identity, state, detail);
      list.append(item);
    }
    list.hidden = false;
  }
  async function checkDestinyBuildProfile() {
    const button = document.getElementById('build-profile-check-btn');
    const badge = document.getElementById('status-badge');
    if (!button || buildProfileCheckBusy) return;
    if (!ip || !badge?.classList.contains('connected')) {
      clearDestinyBuildProfileResults();
      setDestinyBuildProfileStatus('Link the console and start Destiny before checking its build.', 'error');
      return;
    }

    buildProfileCheckBusy = true;
    const targetIp = ip;
    const generation = connectionGeneration;
    button.disabled = true;
    clearDestinyBuildProfileResults();
    const anchorCount = RtmBuildCompatibility.anchorCount;
    setDestinyBuildProfileStatus(`Checking ${anchorCount} code anchors…`, 'pending');
    try {
      const result = await buildCompatibilityGuard.inspect();
      if (generation !== connectionGeneration || targetIp !== ip || !badge.classList.contains('connected')) {
        setDestinyBuildProfileStatus('Console session changed during the check. Check again.', 'error');
        return;
      }
      renderDestinyBuildProfileResults(result.anchors);
      const matched = result.anchors.filter(anchor => anchor.status === 'match').length;
      const mismatches = result.anchors.filter(anchor => anchor.status === 'mismatch');
      const unreadable = result.anchors.filter(anchor => anchor.status === 'unreadable');
      if (result.status === 'verified') {
        setDestinyBuildProfileStatus(`Last check matched the BLES01857 01.24 signature profile · ${matched}/${anchorCount} anchors.`, 'matched');
      } else {
        const issues = [];
        if (mismatches.length) issues.push(`mismatch at ${mismatches.map(anchor => `0x${anchor.address}`).join(', ')}`);
        if (unreadable.length) issues.push(`unreadable at ${unreadable.map(anchor => `0x${anchor.address}`).join(', ')}`);
        setDestinyBuildProfileStatus(`No verified build match · ${matched}/${anchorCount} anchors match; ${issues.join('; ')}. Memory writes remain locked.`, 'error');
      }
    } catch (error) {
      if (generation !== connectionGeneration || targetIp !== ip || !badge.classList.contains('connected')) {
        setDestinyBuildProfileStatus('Console session changed during the check. Check again.', 'error');
      } else {
        const message = String(error?.message || error);
        setDestinyBuildProfileStatus(`Build check failed: ${message}`, 'error');
      }
    } finally {
      buildProfileCheckBusy = false;
      button.disabled = false;
    }
  }
  window.checkDestinyBuildProfile = checkDestinyBuildProfile;
  const patchRunner = RtmOperations.createRunner({
    session: () => ip && document.getElementById('status-badge').classList.contains('connected') ? `${connectionGeneration}:${ip}` : '',
    beforeApply: (owner, pairs) => buildCompatibilityGuard.ensure(pairs),
    write: async (address, value, options) => {
      const target = ip;
      await consoleTransport.request(`http://${target}/setmem.ps3mapi?addr=${address.toString(16)}&val=${value}`,options.timeoutMs || 8000);
      await henDelay();
    },
    read: (address, length, options) => ps3getmem(address, length, options.timeoutMs || 8000)
  });
  const SPAWN_EXECUTOR_WRITE_RANGES = Object.freeze([
    Object.freeze([0x00354074, 0x00354078]), // the one verified hook instruction
    Object.freeze([0x0183E000, 0x01840000]), // bootstrap and emitted program
    Object.freeze([0x01C74000, 0x01C74008]), // BRX1 header
    Object.freeze([0x01C74010, 0x01C74050]), // eight callback/context descriptors
    Object.freeze([0x01C75000, 0x01C76CD0])  // the exact 0x1CD0 BRC1 block
  ]);
  function isSpawnExecutorWriteRange(address, length) {
    const end = address + length;
    return SPAWN_EXECUTOR_WRITE_RANGES.some(([start, limit]) => address >= start && end <= limit && end > address);
  }
  const spawnExecutorMemoryApi = Object.freeze({
    session: activitySessionOwner,
    async writeIfEqual(owner, address, expectedHex, replacementHex) {
      const before = String(expectedHex || '').replace(/\s/g, '').toUpperCase();
      const after = String(replacementHex || '').replace(/\s/g, '').toUpperCase();
      if (!Number.isSafeInteger(address) || address < 0x10000 || address >= 0x40000000 ||
          !/^(?:[0-9A-F]{2})+$/.test(before) || before.length !== after.length ||
          !/^(?:[0-9A-F]{2})+$/.test(after) || before.length > 0x200 ||
          !isSpawnExecutorWriteRange(address, before.length / 2)) {
        throw new Error('Spawn executor write is outside its exact BLES 01.24-owned ranges or length bounds');
      }
      if (!owner || activitySessionOwner() !== owner) throw new Error('Console session changed; spawn executor write refused');
      const result = await patchRunner.apply([[address, after]], {
        preconditions: [[address, before]],
        timeoutMs: 8000
      });
      if (activitySessionOwner() !== owner) throw new Error('Console session changed during spawn executor write');
      if (result.status !== 'verified') throw new Error('Spawn executor write did not pass exact readback verification');
      return result;
    }
  });
  window.rtmSpawnExecutorMemory = spawnExecutorMemoryApi;
  const requestedCheatState = Object.create(null);
  function ensureOperationSession(generation) {
    if (generation !== connectionGeneration) throw new Error('Console session changed; operation cancelled');
  }
  function resetVerifiedStates() {
    antiMMState = false; server4xDamageState = false; srvMode = 'unknown';
    setDestinyBuildProfileStatus('Not checked for this connection.');
    clearDestinyBuildProfileResults();
    for (const key of Object.keys(toggleState)) delete toggleState[key];
    const controls = document.getElementById('ropk-controls'); if (controls) controls.style.display = 'none';
    for (const label of Object.keys(cheatState)) delete cheatState[label];
    for (const label of Object.keys(requestedCheatState)) delete requestedCheatState[label];
    document.querySelectorAll('[id^="tog-"], [id^="srv-"]').forEach(element => {
      element.classList.remove('active-cheat');
      updateCheatStatus(element, 'unknown');
    });
  }

  async function applyToggle(label, btn, pairsForState, optionsForState = () => ({})) {
    if (!ip || !document.getElementById('status-badge').classList.contains('connected')) { setCheatStatusFromButton(btn, 'unknown'); toast('Not connected', 'err'); return; }
    if (btn?.disabled) return;
    const generation = connectionGeneration;
    const isOn = !(requestedCheatState[label] ?? cheatState[label]);
    if (btn) btn.disabled = true;
    setCheatStatusFromButton(btn, 'applying');
    try {
      if (isOn && btn?.id) await resolveCheatConflicts(btn.id);
      if (generation !== connectionGeneration) return;
      requestedCheatState[label] = isOn;
      const patchOptions = typeof optionsForState === 'function' ? optionsForState(isOn) : optionsForState;
      const result = await patchRunner.apply(pairsForState(isOn), patchOptions || {});
      if (generation !== connectionGeneration) return;
      requestedCheatState[label] = isOn;
      if (result.status === 'verified') {
        cheatState[label] = isOn;
        btn?.classList.toggle('active-cheat', isOn);
        setCheatStatusFromButton(btn, isOn ? 'verified-on' : 'verified-off');
        if (btn?.id === 'tog-ohk') syncDamagePatchState(isOn ? DAMAGE_OHK_HEX : DAMAGE_NORMAL_HEX, 'Main One-Hit toggle');
        log(`${label}: ${isOn ? 'ON' : 'OFF'} verified by memory readback`, 'ok');
        toast(`${label}: ${isOn ? 'ON' : 'OFF'} verified`, 'ok');
      } else {
        btn?.classList.remove('active-cheat');
        setCheatStatusFromButton(btn, 'unknown');
        log(`${label}: write acknowledged, readback unavailable; state unknown`, 'err');
        toast(`${label}: sent, not verified`, '');
      }
    } catch (error) {
      if (generation !== connectionGeneration) return;
      if (cheatState[label] === undefined) delete requestedCheatState[label];
      else requestedCheatState[label] = cheatState[label];
      btn?.classList.remove('active-cheat');
      setCheatStatusFromButton(btn, 'unknown');
      log(`${label}: ${error.message}`, 'err'); toast(`${label}: state unknown`, 'err');
    } finally { if (btn) btn.disabled = false; }
  }
  async function togFire(addr, onVal, offVal, label, btn, size = 4) {
    return applyToggle(label, btn, isOn => [[addr, isOn ? onVal : offVal]]);
  }
  async function restoreSuperSpeedBaseline(btn) {
    const address = '1A96C0';
    const baseline = '00000000';
    const patched = '04000000';
    const status = document.getElementById('status-badge');
    if (!ip || !status?.classList.contains('connected')) {
      toast('Not connected', 'err');
      return;
    }
    if (btn?.disabled) return;

    const generation = connectionGeneration;
    const targetIp = ip;
    if (btn) btn.disabled = true;
    try {
      const profile = await buildCompatibilityGuard.inspect();
      if (generation !== connectionGeneration || targetIp !== ip || !status.classList.contains('connected')) {
        throw new Error('Console session changed during build verification');
      }
      if (profile.status !== 'verified') {
        throw new Error('BLES01857 01.24 build signature did not match; no write was attempted');
      }

      const observed = await ps3getmem(parseInt(address, 16), 4);
      if (generation !== connectionGeneration || targetIp !== ip || !status.classList.contains('connected')) {
        throw new Error('Console session changed during patch-site inspection');
      }
      if (observed === baseline) {
        log('Super Speed patch site already matches the 01.24 baseline', 'ok');
        toast('Patch site already at baseline', 'ok');
        return;
      }
      if (observed !== patched) {
        throw new Error(`Patch site contains ${observed}; expected ${patched}. No write was attempted`);
      }

      const result = await patchRunner.apply([[address, baseline]], {
        preconditions: [[address, patched]]
      });
      if (generation !== connectionGeneration || targetIp !== ip || !status.classList.contains('connected')) {
        throw new Error('Console session changed during baseline restore');
      }
      if (result.status !== 'verified') throw new Error('Baseline write was not confirmed by memory readback');

      cheatState['Super Speed'] = false;
      requestedCheatState['Super Speed'] = false;
      log('Super Speed patch site restored to the verified 01.24 baseline', 'ok');
      toast('Super Speed patch baseline restored', 'ok');
    } catch (error) {
      if (generation === connectionGeneration && targetIp === ip) {
        log(`Super Speed baseline restore refused: ${error.message}`, 'err');
        toast(`Baseline restore refused: ${error.message}`, 'err');
      }
    } finally {
      if (btn) btn.disabled = false;
    }
  }
  window.restoreSuperSpeedBaseline = restoreSuperSpeedBaseline;
  async function togFireMulti(onPairs, offPairs, label, btn) {
    return applyToggle(label, btn, isOn => isOn ? onPairs : offPairs);
  }
  async function toggleUnlimitedAmmo(btn) {
    return togFireMulti(UNLIMITED_AMMO_PATCH.on, UNLIMITED_AMMO_PATCH.off, 'Unlimited Ammo', btn);
  }
  async function togFireFloat(addr, onFloat, offFloat, label, btn) {
    return applyToggle(label, btn, isOn => [[addr, floatToBEHex(parseFloat(isOn ? onFloat : offFloat))]]);
  }
  async function togFireBytes(baseAddr, onHex, offHex, label, btn) {
    return applyToggle(label, btn, isOn => {
      const hex = isOn ? onHex : offHex;
      return (hex.match(/.{1,8}/g) || []).map((chunk, i) => [(parseInt(baseAddr,16) + i*4).toString(16), chunk]);
    });
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
    const operationGeneration = connectionGeneration;
    if (!ip) { toast('Not connected', 'err'); return; }
    const addr = document.getElementById('ds-addr').value.trim().replace(/^0x/i, '');
    if (!addr) { toast('Enter the instruction address first', 'err'); return; }

    const isOn   = !cheatState["Deathsinger's Power"];
    const onVal  = DS_ON_VARIANTS[dsDetectedOff]  || '38C60FFF';
    const offVal = dsDetectedOff;
    const writeVal = isOn ? onVal : offVal;

    try {
      ensureOperationSession(operationGeneration);
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
    const generation = connectionGeneration;
    const verifiedWrite = async (address,value) => {
      if (generation !== connectionGeneration) throw new Error('Console session changed');
      await verifiedMemoryValue(address,value);
      if (generation !== connectionGeneration) throw new Error('Console session changed');
    };
    const currentBranch = String(await ps3getmem(PLAYER_HOOK_SITE,4) || '').toUpperCase();
    if (generation !== connectionGeneration) throw new Error('Console session changed');
    if (currentBranch !== PLAYER_HOOK_ORIGINAL && currentBranch !== PLAYER_HOOK_BRANCH) {
      throw new Error('Shared hook site has an unexpected instruction; no code was written');
    }
    const currentRegion = await readHookRange(PLAYER_HOOK_CAVE, PLAYER_HOOK_STUB_HEX.length / 2 + PLAYER_HOOK_TAIL_LENGTH);
    if (generation !== connectionGeneration) throw new Error('Console session changed');
    const blankRegion = !/[^0]/.test(currentRegion);
    const knownRegion = recognizedHookPayloads().some(payload =>
      currentRegion === payload.padEnd((PLAYER_HOOK_STUB_HEX.length / 2 + PLAYER_HOOK_TAIL_LENGTH) * 2, '0')
    );
    if (!blankRegion && !knownRegion) {
      throw new Error('The shared code cave contains an unrecognized hook; it was left untouched');
    }
    // Deactivate a recognized owned branch BEFORE rewriting its payload.
    if (currentBranch === PLAYER_HOOK_BRANCH) {
      await verifiedConditionalMemoryValue(PLAYER_HOOK_SITE.toString(16), PLAYER_HOOK_BRANCH, PLAYER_HOOK_ORIGINAL);
    }
    await henDelay();

    const chunks = hexStr.match(/.{8}/g) || [];
    const base = SRV_HOOK_ADDR; // 0x10D8EC0

    // 1. Write the new payload
    for (let i = 0; i < chunks.length; i++) {
      const addr = (base + i * 4).toString(16).toUpperCase();
      await verifiedWrite(`${addr}`,`${chunks[i]}`);
      await henDelay();
    }

    // 2. Zero out the remainder of the shared 268-byte hook region. This also
    // clears bytes left by older RTMs that used a shorter payload.
    const MAX_CHUNKS = (PLAYER_HOOK_STUB_HEX.length / 2 + PLAYER_HOOK_TAIL_LENGTH) / 4;
    for (let i = chunks.length; i < MAX_CHUNKS; i++) {
      const addr = (base + i * 4).toString(16).toUpperCase();
      await verifiedWrite(`${addr}`,`00000000`);
      await henDelay();
    }

    // Track the exact known payload before activation so cleanup can avoid
    // restoring a branch after another feature has replaced this code cave.
    playerHookTarget = ip;
    playerHookPayload = hexStr;
    // 3. Reactivate the branch
    await verifiedWrite(`1C4110`,`48F14DB0`);
    setPlayerHookStatus('Server hook active. Link cleanup restores it only while its payload still matches.');
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
    return RtmMemoryFeatures.classifyDamagePatch(normalizeDamageHex(hex)).label;
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

  function syncDamagePatchState(value, sourceLabel = 'Damage patch') {
    const mode = RtmMemoryFeatures.classifyDamagePatch(normalizeDamageHex(value));
    const oneHitActive = mode.isOneHit;

    customDamageHex = mode.hex;
    server4xDamageState = mode.isTenX;
    setCheatLabelState('One-Hit Enemies', oneHitActive);
    setCheatLabelState('One Hit Kill', oneHitActive);
    requestedCheatState['One-Hit Enemies'] = oneHitActive;
    requestedCheatState['One Hit Kill'] = oneHitActive;
    setAllButtonsActive('tog-ohk', oneHitActive);
    setAllButtonsActive('srv-4x-damage', mode.isTenX);
    updateCheatStatus('tog-ohk', oneHitActive ? 'verified-on' : 'verified-off');
    updateCheatStatus('srv-4x-damage', mode.isTenX ? 'verified-on' : 'verified-off');

    if (mode.multiplier !== null) setDamageMultiplierPreset(mode.multiplier);
    if (mode.kind === 'one-hit-overwalls') {
      const value = document.getElementById('damage-multiplier-val');
      const encoded = document.getElementById('damage-multiplier-hex');
      const effective = document.getElementById('damage-effective-value');
      const advanced = document.getElementById('damage-encoded-hex');
      if (value) value.textContent = 'One-Hit';
      if (encoded) encoded.textContent = mode.hex;
      if (effective) effective.textContent = 'Not inferred';
      if (advanced) advanced.textContent = mode.hex;
    }
    const detail = mode.kind === 'one-hit-overwalls'
      ? `${mode.label} · multiplier not inferred`
      : mode.kind === 'one-hit-main'
        ? `${mode.label} · about ${mode.multiplier}×`
        : mode.kind === 'ten-x'
          ? `${mode.label}`
          : mode.kind === 'normal'
            ? mode.label
            : `${mode.label} (${mode.hex})`;
    const status = document.getElementById('damage-current-status');
    if (status) status.textContent = `${sourceLabel}: ${detail} · 0043E626=${mode.hex}`;
    return mode;
  }

  window.syncDamagePatchState = syncDamagePatchState;

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
      const mode = syncDamagePatchState(hex, sourceLabel);

      const effective = damageHexToEffectiveFloat(hex);
      const status = document.getElementById('damage-current-status');
      if (status) status.textContent = mode.kind === 'one-hit-overwalls'
        ? `${sourceLabel}: ${mode.label} · multiplier not inferred`
        : `Applied: ${effective}× (${hex})`;

      if (mode.kind === 'one-hit-overwalls') {
        log(`${sourceLabel} → 0043E626 = ${hex} (${mode.label}; multiplier not inferred)`, 'ok');
        toast(`${mode.label} patch applied`, 'ok');
      } else {
        log(`${sourceLabel} → 0043E626 = ${hex} (~${effective}×)`, 'ok');
        toast(`Damage multiplier applied: ~${effective}×`, 'ok');
      }
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

  // The recovered RTM labels these Float32 presets as damage modes. Static
  // PPC tracing only identifies a conditional ratio fallback, so preserve the
  // source labels and make their unconfirmed gameplay meaning explicit.
  const LEGACY_SCALAR_ADDR = 0x004449EC;
  const LEGACY_SCALAR_CODE_ADDR = 0x00444B64;
  const LEGACY_SCALAR_CODE_HEX = '3C600044C03601283CC03F803B4349EC3061034462C4000063850000C05A0000';
  const LEGACY_SCALAR_PRESETS = Object.freeze({
    normal: { hex: '3F800000', label: 'Normal · 1×' },
    max_damage: { hex: '3FFFFFFF', label: 'Source label: Max Damage · ~2×' },
    one_hit_kill_2: { hex: '42C80000', label: 'Source label: 1 Hit Kill 2 · 100×' },
  });
  let legacyScalarBusy = false;

  function setLegacyScalarStatus(message, isError = false) {
    const status = document.getElementById('legacy-scalar-status');
    if (!status) return;
    status.textContent = message;
    status.style.color = isError ? 'var(--danger)' : 'var(--muted)';
  }

  async function hasLegacyScalarBuildSignature() {
    const raw = await ps3getmem(LEGACY_SCALAR_CODE_ADDR, 32);
    const actual = String(raw || '').replace(/[^0-9a-f]/gi, '').toUpperCase();
    return actual === LEGACY_SCALAR_CODE_HEX;
  }

  async function readLegacyScalarPreset() {
    if (!ip) {
      setLegacyScalarStatus('Not connected · 004449EC', true);
      return false;
    }
    try {
      const supportedBuild = await hasLegacyScalarBuildSignature();
      const raw = await ps3getmem(LEGACY_SCALAR_ADDR, 4);
      const hex = String(raw || '').replace(/[^0-9a-f]/gi, '').toUpperCase();
      if (!/^[0-9A-F]{8}$/.test(hex)) throw new Error('Read returned an invalid 4-byte value');
      if (!supportedBuild) {
        setLegacyScalarStatus(`Build signature mismatch · raw 004449EC=${hex}`, true);
        return false;
      }
      const entry = Object.values(LEGACY_SCALAR_PRESETS).find((preset) => preset.hex === hex);
      setLegacyScalarStatus(`${entry ? entry.label : 'Unknown value'} · 004449EC=${hex}`, !entry);
      return Boolean(entry);
    } catch (error) {
      setLegacyScalarStatus(`Read failed · ${error.message}`, true);
      return false;
    }
  }

  async function applyLegacyScalarPreset() {
    if (typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      openServerSafetyGuide();
      toast('Read the Server Cheats safety guide first', 'err');
      return false;
    }
    if (!ip) {
      setLegacyScalarStatus('Not connected · 004449EC', true);
      toast('Not connected', 'err');
      return false;
    }
    if (legacyScalarBusy) return false;

    const select = document.getElementById('legacy-scalar-preset');
    const preset = LEGACY_SCALAR_PRESETS[select?.value];
    if (!preset) return false;
    const target = ip;
    const generation = connectionGeneration;
    legacyScalarBusy = true;
    try {
      if (!await hasLegacyScalarBuildSignature()) throw new Error('01.24 scalar-consumer signature mismatch; write refused');
      if (generation !== connectionGeneration || target !== ip) throw new Error('Connection changed; retry the operation');
      const before = String(await ps3getmem(LEGACY_SCALAR_ADDR, 4) || '').replace(/[^0-9a-f]/gi, '').toUpperCase();
      if (generation !== connectionGeneration || target !== ip) throw new Error('Connection changed; retry the operation');
      if (!/^[0-9A-F]{8}$/.test(before) || !Object.values(LEGACY_SCALAR_PRESETS).some((candidate) => candidate.hex === before)) {
        throw new Error(`Unknown current value ${before || '(unreadable)'}; write refused`);
      }
      await verifiedMemoryValue('004449EC', preset.hex);
      if (generation !== connectionGeneration || target !== ip) throw new Error('Connection changed during write');
      const readback = String(await ps3getmem(LEGACY_SCALAR_ADDR, 4) || '').replace(/[^0-9a-f]/gi, '').toUpperCase();
      if (readback !== preset.hex) throw new Error(`Readback mismatch: ${readback || '(unreadable)'}`);
      setLegacyScalarStatus(`Applied ${preset.label} · 004449EC=${readback}`);
      log(`Recovered scalar preset → 004449EC=${readback} (${preset.label}; gameplay effect unknown)`, 'ok');
      toast(`${preset.label} bytes verified`, 'ok');
      return true;
    } catch (error) {
      setLegacyScalarStatus(`Not applied · ${error.message}`, true);
      log(`Recovered scalar preset refused/failed: ${error.message}`, 'err');
      toast('Scalar preset was not applied', 'err');
      return false;
    } finally {
      legacyScalarBusy = false;
    }
  }

  window.readLegacyScalarPreset = readLegacyScalarPreset;
  window.applyLegacyScalarPreset = applyLegacyScalarPreset;

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
      syncDamagePatchState(willEnable ? DAMAGE_4X_HEX : DAMAGE_NORMAL_HEX, 'Damage preset toggle');

      log(`Damage Preset ${willEnable ? 'ON' : 'OFF'}`, 'ok');
      toast(`Damage Preset ${willEnable ? 'ON' : 'OFF'}`, 'ok');
    } catch (e) {
      updateCheatStatus('srv-4x-damage', 'failed');
      log(`Damage Preset failed: ${e.message}`, 'err');
      toast('Damage Preset failed', 'err');
    } finally {
      server4xDamageBusy = false;
    }
  }

  async function srvSetMode(mode, btn) {
    btn ||= document.getElementById('srv-'+mode);
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
        syncDamagePatchState(DAMAGE_NORMAL_HEX, 'Server Cheats Off');
      } catch (e) {
        log(`Damage Preset restore failed: ${e.message}`, 'err');
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
        updateCheatStatus('srv-void', 'verified-on');
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
        updateCheatStatus('srv-ropk', 'verified-on');
        log('Relative OPK ON → hook written', 'ok');
        toast('Relative OPK ON', 'ok');
        notifyPS3('Relative OPK: ON');
      } else {
        // Off — restore original hook, clear tail
        await writeHookBytes(OFF_HEX);
        btn.classList.add('active-cheat');
        updateCheatStatus('srv-off', 'verified-on');
        updateCheatStatus('srv-void', 'off');
        updateCheatStatus('srv-ropk', 'off');
        updateCheatStatus('srv-4x-damage', 'off');
        log('Server Cheats OFF → hook restored', 'ok');
        toast('Server Cheats OFF', 'ok');
        notifyPS3('Server Cheats: OFF');
      }
      return true;
    } catch(e) {
      srvMode = 'unknown';
      ['srv-void','srv-ropk','srv-off'].forEach(id => updateCheatStatus(id,'unknown'));
      if (btn?.id) updateCheatStatus(btn.id, 'failed');
      log(`Server cheat (${mode}) failed: ${e.message}`, 'err');
      toast('Server mode not confirmed', 'err');
      return false;
    } finally {
      srvBusy = false; // Release the lock so you can click again
    }
  }

  // Write the current ROPK X/Y/Z slider values as floats to the offset addresses
  async function ropkWrite() {
    const operationGeneration = connectionGeneration;
    if (typeof hasReadServerSafetyGuide === 'function' && !hasReadServerSafetyGuide()) {
      serverSafetyRequired = true;
      setServerCheatsLocked(true);
      return;
    }
    if (!ip || srvMode !== 'ropk') return;
    const x = parseFloat(document.getElementById('ropk-x').value) || 0;
    const y = parseFloat(document.getElementById('ropk-y').value) || 0;
    const z = parseFloat(document.getElementById('ropk-z').value) || 0;
    ensureOperationSession(operationGeneration);
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${ROPK_X_ADDR.toString(16).toUpperCase()}&val=${floatToBEHex(x)}`);
    ensureOperationSession(operationGeneration);
    await ps3fetch(`http://${ip}/setmem.ps3mapi?addr=${ROPK_Y_ADDR.toString(16).toUpperCase()}&val=${floatToBEHex(y)}`);
    ensureOperationSession(operationGeneration);
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



  // ── 1.24 Ammo Editor and framebuffer capture ───────────────────────────────
  const AMMO_SCAN_BASE = 0x60300000;
  const AMMO_SCAN_LENGTH = 0x400000;
  const AMMO_SCAN_CHUNK = 0x10000;
  const SCREENSHOT_PAUSE_STORAGE_KEY = 'rtm_framebuffer_pause_restore_v1';
  let ammoScanState = null;
  const ammoRestoreStack = [];
  let ammoScanBusy = false;
  const screenshotCaptures = RtmScreenshotTools.createCaptureStore(URL);
  const screenshotStretchModes = new Set();
  let screenshotPauseRestore = null;
  try { screenshotPauseRestore = JSON.parse(localStorage.getItem(SCREENSHOT_PAUSE_STORAGE_KEY) || 'null'); } catch (e) { screenshotPauseRestore = null; }
  window.rtmFramebufferBusy = !!screenshotPauseRestore;
  window.rtmFramebufferRecoveryIp = screenshotPauseRestore?.ip || '';

  function saveScreenshotPauseRestore(state) {
    if (state) {
      try { localStorage.setItem(SCREENSHOT_PAUSE_STORAGE_KEY, JSON.stringify(state)); }
      catch (error) { throw new Error(`Could not save pause recovery data: ${error.message || error}`); }
      screenshotPauseRestore = state;
    } else {
      try { localStorage.removeItem(SCREENSHOT_PAUSE_STORAGE_KEY); }
      catch (error) { throw new Error(`Could not clear pause recovery data: ${error.message || error}`); }
      screenshotPauseRestore = null;
    }
    window.rtmFramebufferRecoveryIp = screenshotPauseRestore?.ip || '';
  }

  function setAmmoStatus(message, failed = false) {
    const status = document.getElementById('ammo-scan-status');
    if (!status) return;
    status.textContent = message;
    status.style.color = failed ? 'var(--danger)' : 'var(--muted)';
  }

  function readAmmoNumber(id) {
    const input = document.getElementById(id);
    if (!input || input.value.trim() === '') throw new Error('Enter all four ammo counts first');
    const value = Number(input.value);
    if (!Number.isInteger(value) || value < 0 || value > 0x7FFFFFFF) throw new Error('Ammo counts must be whole numbers from 0 to 2147483647');
    return value;
  }

  function readAmmoFloat(id) {
    const input = document.getElementById(id);
    if (!input || input.value.trim() === '') throw new Error('Enter both special ammo values first');
    const value = Number(input.value);
    RtmMemoryFeatures.beFloat32(value);
    if (value < 0) throw new Error('Special ammo counts must be zero or greater');
    return value;
  }

  function updateAmmoFields() {
    const isSpecial = document.getElementById('ammo-family')?.value === 'Special';
    const integerFields = document.getElementById('ammo-integer-fields');
    const floatFields = document.getElementById('ammo-float-fields');
    if (integerFields) integerFields.style.display = isSpecial ? 'none' : '';
    if (floatFields) floatFields.style.display = isSpecial ? '' : 'none';
  }

  function renderAmmoCandidates(addresses) {
    const list = document.getElementById('ammo-results');
    const applyButton = document.getElementById('ammo-apply-btn');
    list.replaceChildren();
    if (!addresses.length) {
      list.innerHTML = '<div class="scan-empty">No validated candidates. Check the current counts and try again.</div>';
      if (applyButton) applyButton.disabled = true;
      return;
    }
    const shown = addresses.slice(0, 100);
    shown.forEach((address, index) => {
      const row = document.createElement('label');
      row.className = 'scan-result-row';
      const radio = document.createElement('input');
      radio.type = 'radio';
      radio.name = 'ammo-candidate';
      radio.value = String(index);
      radio.addEventListener('change', () => { if (applyButton) applyButton.disabled = !radio.checked; });
      const title = document.createElement('span');
      title.className = 'scan-result-addr';
      title.textContent = `${ammoScanState.family} · 0x${address.toString(16).toUpperCase().padStart(8, '0')}`;
      const detail = document.createElement('span');
      detail.className = 'scan-result-vals';
      detail.textContent = ammoScanState.family === 'Special'
        ? `Current special ammo ${ammoScanState.currentSpecial}`
        : `Current magazine ${ammoScanState.currentMag} · reserve ${ammoScanState.currentReserve}`;
      row.append(radio, title, detail);
      list.appendChild(row);
    });
    if (addresses.length > shown.length) {
      const note = document.createElement('div');
      note.className = 'scan-empty';
      note.textContent = `${addresses.length - shown.length} more candidates omitted; use a more distinctive current ammo count.`;
      list.appendChild(note);
    }
    if (applyButton) applyButton.disabled = true;
  }

  async function scanAmmoCandidates() {
    if (ammoScanBusy) return;
    if (!ip || !document.getElementById('status-badge')?.classList.contains('connected')) { toast('Not connected', 'err'); return; }
    let currentMag, currentReserve, newMag, newReserve, currentSpecial, newSpecial;
    const family = document.getElementById('ammo-family').value;
    try {
      if (family === 'Special') {
        currentSpecial = readAmmoFloat('ammo-current-special');
        newSpecial = readAmmoFloat('ammo-new-special');
      } else {
        currentMag = readAmmoNumber('ammo-current-mag');
        currentReserve = readAmmoNumber('ammo-current-reserve');
        newMag = readAmmoNumber('ammo-new-mag');
        newReserve = readAmmoNumber('ammo-new-reserve');
        if (currentMag === 0 && currentReserve === 0) throw new Error('At least one current ammo count must be nonzero');
      }
    } catch (error) {
      setAmmoStatus(error.message, true);
      toast(error.message, 'err');
      return;
    }

    const generation = connectionGeneration;
    const scanButton = document.getElementById('ammo-scan-btn');
    const applyButton = document.getElementById('ammo-apply-btn');
    if (scanButton) scanButton.disabled = true;
    if (applyButton) applyButton.disabled = true;
    ammoScanBusy = true;
    ammoScanState = null;
    setAmmoStatus('Reading the bounded 4 MiB ammo range…');
    const progress = document.getElementById('ammo-progress');
    const progressText = document.getElementById('ammo-progress-text');
    const progressFill = document.getElementById('ammo-progress-fill');
    progress.style.display = '';

    try {
      const snapshot = new Uint8Array(AMMO_SCAN_LENGTH);
      const chunkCount = Math.ceil(AMMO_SCAN_LENGTH / AMMO_SCAN_CHUNK);
      for (let offset = 0; offset < AMMO_SCAN_LENGTH; offset += AMMO_SCAN_CHUNK) {
        ensureOperationSession(generation);
        const length = Math.min(AMMO_SCAN_CHUNK, AMMO_SCAN_LENGTH - offset);
        const address = AMMO_SCAN_BASE + offset;
        progressText.textContent = `Ammo scan ${Math.floor(offset / AMMO_SCAN_CHUNK) + 1}/${chunkCount} · 0x${address.toString(16).toUpperCase()}`;
        progressFill.style.width = `${Math.round((offset + length) / AMMO_SCAN_LENGTH * 100)}%`;
        const bytes = RtmMemoryFeatures.bytesFromHex(await ps3getmem(address, length));
        ensureOperationSession(generation);
        if (bytes.length !== length) throw new Error(`Short read at 0x${address.toString(16).toUpperCase()}`);
        snapshot.set(bytes, offset);
      }
      const addresses = family === 'Special'
        ? RtmMemoryFeatures.findSpecialAmmoCandidates(snapshot, AMMO_SCAN_BASE, currentSpecial)
        : RtmMemoryFeatures.findAmmoCandidates(snapshot, AMMO_SCAN_BASE, currentMag, currentReserve);
      ensureOperationSession(generation);
      ammoScanState = { addresses, currentMag, currentReserve, newMag, newReserve, currentSpecial, newSpecial, family, generation, ip };
      renderAmmoCandidates(addresses);
      setAmmoStatus(addresses.length
        ? `${addresses.length} candidate${addresses.length === 1 ? '' : 's'} matched the RTM marker. Select one slot before writing.`
        : 'No candidates matched the current ammo values and RTM marker.', !addresses.length);
      log(`Ammo scan complete — ${addresses.length} candidates in 4 MiB`, addresses.length ? 'ok' : 'err');
    } catch (error) {
      ammoScanState = null;
      renderAmmoCandidates([]);
      setAmmoStatus(`Ammo scan failed: ${error.message}`, true);
      log(`Ammo scan failed: ${error.message}`, 'err');
      toast('Ammo scan failed', 'err');
    } finally {
      ammoScanBusy = false;
      progress.style.display = 'none';
      if (scanButton) scanButton.disabled = false;
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    document.getElementById('ammo-family')?.addEventListener('change', updateAmmoFields);
    updateAmmoFields();
  });

  function selectedAmmoAddress() {
    if (!ammoScanState) return null;
    const input = document.querySelector('input[name="ammo-candidate"]:checked');
    const index = input ? Number(input.value) : -1;
    return Number.isInteger(index) ? ammoScanState.addresses[index] ?? null : null;
  }

  async function applyAmmoCandidate() {
    const state = ammoScanState;
    const address = selectedAmmoAddress();
    if (!state || !Number.isInteger(address)) { toast('Select one ammo slot first', 'err'); return; }
    if (state.ip !== ip || state.generation !== connectionGeneration) { setAmmoStatus('Connection changed; scan again before writing.', true); return; }
    try {
      if (ammoRestoreStack.length >= 32) throw new Error('Restore the pending ammo changes before making another edit');
      if (document.getElementById('ammo-family').value !== state.family) throw new Error('Ammo candidate type changed after the scan; scan again');
      let originalHex, signatureHex, writtenHex, byteLength, markerOffset;
      if (state.family === 'Special') {
        if (readAmmoFloat('ammo-current-special') !== state.currentSpecial || readAmmoFloat('ammo-new-special') !== state.newSpecial) {
          throw new Error('Special ammo values changed after the scan; scan again');
        }
        signatureHex = [...RtmMemoryFeatures.beFloat32(state.currentSpecial), 1, 0, 0, 0]
          .map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
        const signatureBefore = (await ps3getmem(address, 8)).toUpperCase();
        ensureOperationSession(state.generation);
        if (signatureBefore !== signatureHex) throw new Error('Selected slot no longer has the scanned special ammo signature');
        const marker = RtmMemoryFeatures.bytesFromHex(await ps3getmem(address + 40, 2));
        ensureOperationSession(state.generation);
        if (marker.length !== 2 || marker[0] !== 0x80 || marker[1] !== 0x80) throw new Error('Selected special ammo marker changed; scan again');
        if ((await ps3getmem(address, 8)).toUpperCase() !== signatureBefore) throw new Error('Special ammo values changed during validation; scan again');
        ensureOperationSession(state.generation);
        originalHex = signatureBefore.slice(0, 8);
        writtenHex = RtmMemoryFeatures.beFloat32(state.newSpecial).map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
        byteLength = 4;
        markerOffset = 40;
      } else {
        if (readAmmoNumber('ammo-current-mag') !== state.currentMag || readAmmoNumber('ammo-current-reserve') !== state.currentReserve ||
            readAmmoNumber('ammo-new-mag') !== state.newMag || readAmmoNumber('ammo-new-reserve') !== state.newReserve) {
          throw new Error('Ammo values changed after the scan; scan again');
        }
        originalHex = (await ps3getmem(address, 8)).toUpperCase();
        ensureOperationSession(state.generation);
        const originalBytes = RtmMemoryFeatures.bytesFromHex(originalHex);
        const expected = [...RtmMemoryFeatures.beU32(state.currentReserve), ...RtmMemoryFeatures.beU32(state.currentMag)];
        if (originalBytes.length !== 8 || originalBytes.some((byte, index) => byte !== expected[index])) throw new Error('Selected slot no longer has the scanned ammo values');
        const marker = RtmMemoryFeatures.bytesFromHex(await ps3getmem(address + 28, 2));
        ensureOperationSession(state.generation);
        if (marker.length !== 2 || marker[0] !== 0x80 || marker[1] !== 0xA6) throw new Error('Selected slot marker changed; scan again');
        if ((await ps3getmem(address, 8)).toUpperCase() !== originalHex) throw new Error('Ammo values changed during validation; scan again');
        ensureOperationSession(state.generation);
        const reserveHex = RtmMemoryFeatures.beU32(state.newReserve).map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
        const magHex = RtmMemoryFeatures.beU32(state.newMag).map(byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
        writtenHex = reserveHex + magHex;
        byteLength = 8;
        markerOffset = 28;
      }
      try {
        await verifiedMemorySequence([[address, writtenHex]]);
      } catch (error) {
        let currentHex = '';
        try { currentHex = (await ps3getmem(address, byteLength)).toUpperCase(); } catch (_) {}
        if (currentHex === writtenHex) {
          try { await verifiedMemorySequence([[address, originalHex]]); }
          catch (restoreError) { throw new Error(`Ammo write failed and rollback could not be verified: ${restoreError.message}`); }
          throw new Error(`Ammo write was rolled back: ${error.message}`);
        }
        if (currentHex && currentHex !== originalHex) throw new Error(`Ammo slot changed during the write; state is unknown and was not overwritten: ${error.message}`);
        throw error;
      }
      ammoRestoreStack.push({ address, originalHex, writtenHex, byteLength, markerOffset, family: state.family, generation: state.generation, ip: state.ip });
      document.getElementById('ammo-restore-btn').disabled = false;
      setAmmoStatus(`Updated ${state.family} at 0x${address.toString(16).toUpperCase()} · readback verified`);
      log(`Ammo values set at 0x${address.toString(16).toUpperCase()} and verified`, 'ok');
      toast('Ammo values verified', 'ok');
    } catch (error) {
      setAmmoStatus(error.message || 'Ammo write failed.', true);
      log(`Ammo write failed: ${error.message}`, 'err');
      toast('Ammo write not confirmed', 'err');
    }
  }

  async function restoreAmmoCandidate() {
    const saved = ammoRestoreStack[ammoRestoreStack.length - 1];
    if (!saved || saved.ip !== ip || saved.generation !== connectionGeneration) {
      setAmmoStatus('No ammo restore point for this console session.', true);
      return;
    }
    try {
      const current = (await ps3getmem(saved.address, saved.byteLength)).toUpperCase();
      ensureOperationSession(saved.generation);
      if (current !== saved.writtenHex) throw new Error('Ammo slot changed since the write; refusing to overwrite it');
      const marker = RtmMemoryFeatures.bytesFromHex(await ps3getmem(saved.address + saved.markerOffset, 2));
      ensureOperationSession(saved.generation);
      const expectedMarker = saved.family === 'Special' ? [0x80, 0x80] : [0x80, 0xA6];
      if (marker.length !== 2 || marker.some((byte, index) => byte !== expectedMarker[index])) {
        throw new Error('Ammo slot marker changed since the write; refusing to overwrite it');
      }
      await verifiedMemorySequence([[saved.address, saved.originalHex]]);
      ammoRestoreStack.pop();
      document.getElementById('ammo-restore-btn').disabled = ammoRestoreStack.length === 0;
      setAmmoStatus(`${saved.family || 'Ammo'} values restored · readback verified${ammoRestoreStack.length ? ` · ${ammoRestoreStack.length} earlier change(s) can also be restored` : ''}`);
      log('Last ammo change restored and verified', 'ok');
      toast('Ammo values restored', 'ok');
    } catch (error) {
      setAmmoStatus(error.message || 'Ammo restore failed.', true);
      log(`Ammo restore failed: ${error.message}`, 'err');
      toast('Ammo restore not confirmed', 'err');
    }
  }

  const SCREENSHOT_PROFILES = {
    sd: { label: '256 × 191 · low resolution', address: 0x34118000, width: 256, height: 191, rowStride: 0xC00, byteLength: 0xBF000 },
    'hd-a': { label: '1024 × 624 · buffer A', address: 0x341BF000, width: 1024, height: 624 },
    'hd-b': { label: '1024 × 624 · buffer B', address: 0x3442F000, width: 1024, height: 624 },
    'hd-1280': { label: '1280 × 720 · high resolution', address: 0xCDE10000, width: 1280, height: 720 },
    'hd-512': { label: '512 × 312 · compact buffer', address: 0xCF060000, width: 512, height: 312 }
  };
  const SCREENSHOT_CHUNK = 0x10000;
  const DESTINY_PAUSE_SITE = Object.freeze({
    address: 0x6155BC,
    runningHex: '41820010',
    pausedHex: '41820000'
  });
  // Older app versions may have saved recovery for this source-unverified byte.
  const LEGACY_SCREENSHOT_PAUSE_ADDRESS = 0x614BFF;
  let pauseOperationBusy = false;

  async function readDestinyPauseInstruction(generation = connectionGeneration) {
    const currentHex = (await ps3getmem(DESTINY_PAUSE_SITE.address, 4)).toUpperCase();
    ensureOperationSession(generation);
    if (!/^[0-9A-F]{8}$/.test(currentHex)) throw new Error('Could not read the complete pause instruction');
    if (![DESTINY_PAUSE_SITE.runningHex, DESTINY_PAUSE_SITE.pausedHex].includes(currentHex)) {
      throw new Error(`Pause instruction at 0x6155BC changed to ${currentHex}; no write was attempted`);
    }
    return currentHex;
  }

  async function setDestinyGamePaused(paused) {
    const badge = document.getElementById('status-badge');
    if (!ip || !badge?.classList.contains('connected')) { toast('Not connected', 'err'); return; }
    if (pauseOperationBusy) {
      toast('Another pause operation is in progress', 'err');
      return;
    }
    if (window.rtmFramebufferBusy) {
      toast('Restore screenshot pause first', 'err');
      return;
    }
    pauseOperationBusy = true;
    const generation = connectionGeneration;
    const targetHex = paused ? DESTINY_PAUSE_SITE.pausedHex : DESTINY_PAUSE_SITE.runningHex;
    const label = paused ? 'Pause Game' : 'Resume Game';
    try {
      const currentHex = await readDestinyPauseInstruction(generation);
      if (currentHex !== targetHex) {
        await verifiedConditionalMemoryValue(DESTINY_PAUSE_SITE.address.toString(16), currentHex, targetHex);
        ensureOperationSession(generation);
      }
      log(`${label} instruction verified at 0x6155BC`, 'ok');
      toast(`${label} verified`, 'ok');
    } catch (error) {
      log(`${label} was not applied: ${error.message}`, 'err');
      toast(`${label} not confirmed`, 'err');
    } finally {
      pauseOperationBusy = false;
    }
  }
  window.setDestinyGamePaused = setDestinyGamePaused;

  async function restoreScreenshotPause() {
    const saved = screenshotPauseRestore;
    const sameSession = saved && saved.ip === ip && saved.generation === connectionGeneration;
    const recoveredSession = saved && saved.pendingRecovery && saved.ip === ip;
    if (!saved || (!sameSession && !recoveredSession) || !document.getElementById('status-badge')?.classList.contains('connected')) {
      setScreenshotStatus('No saved screenshot pause state can be restored in this console session.', true);
      return;
    }
    if (pauseOperationBusy) {
      setScreenshotStatus('Another pause operation is still in progress.', true);
      return;
    }
    pauseOperationBusy = true;
    const targetIp = ip;
    const generation = connectionGeneration;
    try {
      const legacyRecovery = saved.address === undefined || saved.address === null;
      const address = legacyRecovery ? LEGACY_SCREENSHOT_PAUSE_ADDRESS : Number(saved.address);
      const originalHex = String(saved.originalHex || '').toUpperCase();
      const patchedHex = legacyRecovery ? '00' : String(saved.patchedHex || '').toUpperCase();
      const knownRecovery = legacyRecovery
        ? address === LEGACY_SCREENSHOT_PAUSE_ADDRESS && /^[0-9A-F]{2}$/.test(originalHex)
        : address === DESTINY_PAUSE_SITE.address && originalHex === DESTINY_PAUSE_SITE.runningHex && patchedHex === DESTINY_PAUSE_SITE.pausedHex;
      if (!knownRecovery) throw new Error('Saved pause recovery data is not a recognized patch');
      const currentHex = (await ps3getmem(address, legacyRecovery ? 1 : 4)).toUpperCase();
      if (generation !== connectionGeneration || targetIp !== ip) throw new Error('Console session changed during pause recovery');
      if (currentHex !== originalHex && currentHex !== patchedHex) {
        throw new Error(`Pause site changed to ${currentHex}; refusing to overwrite the new value`);
      }
      if (currentHex !== originalHex) {
        await verifiedConditionalMemoryValue(address.toString(16), patchedHex, originalHex);
        if (generation !== connectionGeneration || targetIp !== ip) throw new Error('Console session changed during pause recovery');
      }
      ensureOperationSession(generation);
      saveScreenshotPauseRestore(null);
      window.rtmFramebufferBusy = false;
      document.getElementById('screenshot-resume-btn').disabled = true;
      setScreenshotStatus(legacyRecovery ? 'Legacy screenshot pause byte restored and verified.' : 'Original pause instruction restored and verified.');
      log('Framebuffer capture pause state restored and verified', 'ok');
    } catch (error) {
      const message = String(error?.message || error);
      const prefix = message.startsWith('Could not clear pause recovery data:')
        ? 'Pause instruction is restored, but its recovery record could not be cleared'
        : 'Could not restore the saved pause state';
      setScreenshotStatus(`${prefix}: ${message}`, true);
      log(`Framebuffer pause recovery failed: ${error.message}`, 'err');
    } finally {
      pauseOperationBusy = false;
    }
  }

  function setScreenshotStatus(message, failed = false) {
    const status = document.getElementById('screenshot-status');
    if (!status) return;
    status.textContent = message;
    status.style.color = failed ? 'var(--danger)' : 'var(--muted)';
  }

  function renderScreenshotGallery() {
    const gallery = document.getElementById('screenshot-gallery');
    if (!gallery) return;
    gallery.replaceChildren();
    for (const [profileId, profile] of Object.entries(SCREENSHOT_PROFILES)) {
      const capture = screenshotCaptures.get(profileId);
      const stretched = screenshotStretchModes.has(profileId);
      const card = document.createElement('article');
      card.className = 'screenshot-card';
      const heading = document.createElement('div');
      heading.className = 'screenshot-card-heading';
      const title = document.createElement('h3');
      title.textContent = profile.label;
      heading.appendChild(title);
      card.appendChild(heading);

      const frame = document.createElement('div');
      frame.className = 'screenshot-frame';
      if (capture) {
        const preview = document.createElement('img');
        preview.src = capture.objectUrl;
        preview.alt = `Destiny framebuffer · ${profile.label}`;
        preview.style.objectFit = stretched ? 'fill' : 'contain';
        frame.appendChild(preview);
      } else {
        const empty = document.createElement('span');
        empty.className = 'screenshot-placeholder';
        empty.textContent = 'No capture yet';
        frame.appendChild(empty);
      }
      card.appendChild(frame);

      const actions = document.createElement('div');
      actions.className = 'screenshot-card-actions';
      const download = document.createElement('a');
      download.textContent = 'Download PNG';
      download.setAttribute('aria-label', `Download ${profile.label} PNG`);
      download.hidden = !capture;
      if (capture) {
        download.href = capture.objectUrl;
        download.download = capture.downloadName;
      }
      actions.appendChild(download);

      const downloadJpeg = document.createElement('button');
      downloadJpeg.type = 'button';
      downloadJpeg.id = `screenshot-jpeg-${profileId}`;
      downloadJpeg.textContent = 'Download JPEG';
      downloadJpeg.setAttribute('aria-label', `Download ${profile.label} JPEG`);
      downloadJpeg.disabled = !capture;
      downloadJpeg.addEventListener('click', () => downloadDestinyScreenshotJpeg(profileId));
      actions.appendChild(downloadJpeg);

      const copy = document.createElement('button');
      copy.type = 'button';
      copy.id = `screenshot-copy-${profileId}`;
      copy.textContent = 'Copy PNG';
      copy.setAttribute('aria-label', `Copy ${profile.label} PNG`);
      copy.disabled = !capture;
      copy.addEventListener('click', () => copyDestinyScreenshot(profileId));
      actions.appendChild(copy);

      const view = document.createElement('button');
      view.type = 'button';
      view.textContent = stretched ? 'Fit' : 'Stretch';
      view.setAttribute('aria-label', `${stretched ? 'Fit' : 'Stretch'} ${profile.label} preview`);
      view.addEventListener('click', () => {
        if (stretched) screenshotStretchModes.delete(profileId);
        else screenshotStretchModes.add(profileId);
        renderScreenshotGallery();
      });
      actions.appendChild(view);
      card.appendChild(actions);
      gallery.appendChild(card);
    }
  }

  async function downloadDestinyScreenshotJpeg(profileId) {
    if (!SCREENSHOT_PROFILES[profileId]) { toast('Choose a valid screenshot profile', 'err'); return; }
    const capture = screenshotCaptures.get(profileId);
    if (!capture) { toast('Capture this profile first', 'err'); return; }
    const button = document.getElementById(`screenshot-jpeg-${profileId}`);
    if (button) { button.disabled = true; button.textContent = 'Preparing…'; }
    try {
      const jpeg = await RtmScreenshotTools.pngToJpeg(capture.blob);
      const downloadUrl = URL.createObjectURL(jpeg);
      try {
        const link = document.createElement('a');
        link.href = downloadUrl;
        link.download = capture.downloadName.replace(/\.png$/i, '.jpg');
        link.hidden = true;
        document.body.appendChild(link);
        link.click();
        link.remove();
      } finally {
        window.setTimeout(() => URL.revokeObjectURL(downloadUrl), 1000);
      }
      setScreenshotStatus(`${SCREENSHOT_PROFILES[profileId].width} × ${SCREENSHOT_PROFILES[profileId].height} JPEG ready`);
      toast('JPEG downloaded', 'ok');
    } catch (error) {
      setScreenshotStatus(`JPEG export failed: ${error.message}`, true);
      log(`Framebuffer JPEG export failed: ${error.message}`, 'err');
      toast('Could not export JPEG', 'err');
    } finally {
      const currentButton = document.getElementById(`screenshot-jpeg-${profileId}`);
      if (currentButton) { currentButton.disabled = !screenshotCaptures.get(profileId); currentButton.textContent = 'Download JPEG'; }
    }
  }

  async function copyDestinyScreenshot(profileId) {
    if (!SCREENSHOT_PROFILES[profileId]) { toast('Choose a valid screenshot profile', 'err'); return; }
    const capture = screenshotCaptures.get(profileId);
    if (!capture) { toast('Capture this profile first', 'err'); return; }
    const button = document.getElementById(`screenshot-copy-${profileId}`);
    if (button) button.disabled = true;
    try {
      await RtmScreenshotTools.copyPngToClipboard(capture.blob);
      setScreenshotStatus(`${SCREENSHOT_PROFILES[profileId].width} × ${SCREENSHOT_PROFILES[profileId].height} PNG copied to clipboard`);
      toast('Screenshot copied', 'ok');
    } catch (error) {
      setScreenshotStatus(`Clipboard copy unavailable: ${error.message}`, true);
      log(`Framebuffer clipboard copy failed: ${error.message}`, 'err');
      toast('Could not copy screenshot', 'err');
    } finally {
      if (button) button.disabled = !screenshotCaptures.get(profileId);
    }
  }

  document.addEventListener('DOMContentLoaded', () => {
    renderScreenshotGallery();
    if (!screenshotPauseRestore) return;
    const resumeButton = document.getElementById('screenshot-resume-btn');
    if (resumeButton) resumeButton.disabled = false;
    setScreenshotStatus(`A previous capture may have left the game paused. Connect to ${screenshotPauseRestore.ip} and restore its saved pause state.`, true);
  });

  async function captureDestinyScreenshot() {
    if (!ip || !document.getElementById('status-badge')?.classList.contains('connected')) { toast('Not connected', 'err'); return; }
    if (screenshotPauseRestore) { setScreenshotStatus('Restore the saved pause state before another capture.', true); return; }
    const profileId = document.getElementById('screenshot-profile').value;
    const profile = SCREENSHOT_PROFILES[profileId];
    if (!profile) return;
    const pauseRequested = document.getElementById('screenshot-pause').checked;
    if (pauseRequested && !await RtmDialogs.confirm('Pause the game briefly while reading the framebuffer? The original pause instruction will be restored after capture.')) return;
    if (pauseRequested && pauseOperationBusy) {
      setScreenshotStatus('Another pause operation is still in progress.', true);
      return;
    }
    const generation = connectionGeneration;
    const targetIp = ip;
    const button = document.getElementById('screenshot-capture-btn');
    if (button) button.disabled = true;
    const progress = document.getElementById('screenshot-progress');
    const progressText = document.getElementById('screenshot-progress-text');
    const progressFill = document.getElementById('screenshot-progress-fill');
    progress.style.display = '';
    const byteLength = profile.byteLength || profile.width * profile.height * 4;
    const snapshot = new Uint8Array(byteLength);
    let originalPauseHex = null;
    let pauseWriteAttempted = false;
    let captureError = null;
    let imageReady = false;

    if (pauseRequested) {
      pauseOperationBusy = true;
      window.rtmFramebufferBusy = true;
    }
    try {
      setScreenshotStatus(`Preparing ${profile.label} capture…`);
      if (pauseRequested) {
        originalPauseHex = await readDestinyPauseInstruction(generation);
        if (originalPauseHex === DESTINY_PAUSE_SITE.runningHex) {
          await verifiedConditionalMemoryValue(
            DESTINY_PAUSE_SITE.address.toString(16),
            DESTINY_PAUSE_SITE.runningHex,
            DESTINY_PAUSE_SITE.pausedHex,
            () => {
              saveScreenshotPauseRestore({
                address: DESTINY_PAUSE_SITE.address,
                originalHex: originalPauseHex,
                patchedHex: DESTINY_PAUSE_SITE.pausedHex,
                generation,
                ip: targetIp,
                pendingRecovery: true
              });
              pauseWriteAttempted = true;
            }
          );
          ensureOperationSession(generation);
        }
      }

      const chunks = Math.ceil(byteLength / SCREENSHOT_CHUNK);
      for (let offset = 0; offset < byteLength; offset += SCREENSHOT_CHUNK) {
        ensureOperationSession(generation);
        const length = Math.min(SCREENSHOT_CHUNK, byteLength - offset);
        const address = profile.address + offset;
        progressText.textContent = `Framebuffer ${Math.floor(offset / SCREENSHOT_CHUNK) + 1}/${chunks} · 0x${address.toString(16).toUpperCase()}`;
        progressFill.style.width = `${Math.round((offset + length) / byteLength * 100)}%`;
        setScreenshotStatus(`Reading ${profile.width} × ${profile.height} · ${Math.round((offset + length) / byteLength * 100)}%`);
        const bytes = RtmMemoryFeatures.bytesFromHex(await ps3getmem(address, length));
        ensureOperationSession(generation);
        if (bytes.length !== length) throw new Error(`Framebuffer read was short at 0x${address.toString(16).toUpperCase()}`);
        snapshot.set(bytes, offset);
      }

      const rgba = RtmMemoryFeatures.framebufferToRgba(snapshot, profile.width, profile.height, profile.rowStride || profile.width * 4);
      const canvas = document.createElement('canvas');
      canvas.width = profile.width;
      canvas.height = profile.height;
      const context = canvas.getContext('2d');
      if (!context) throw new Error('Canvas capture is unavailable in this view');
      const image = context.createImageData(profile.width, profile.height);
      image.data.set(rgba);
      context.putImageData(image, 0, 0);
      const blob = await new Promise((resolve, reject) => canvas.toBlob(result => result ? resolve(result) : reject(new Error('Could not encode the PNG')), 'image/png'));
      const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
      screenshotCaptures.set(profileId, blob, `destiny-${profileId}-${profile.width}x${profile.height}-${timestamp}.png`);
      renderScreenshotGallery();
      imageReady = true;
      setScreenshotStatus(`Captured ${profile.width} × ${profile.height} · PNG ready`);
      log(`Destiny framebuffer captured at ${profile.width} × ${profile.height}`, 'ok');
      toast('Screenshot captured', 'ok');
    } catch (error) {
      captureError = error;
      setScreenshotStatus(`Capture failed: ${error.message}`, true);
      log(`Framebuffer capture failed: ${error.message}`, 'err');
      toast('Screenshot capture failed', 'err');
    } finally {
      if (pauseRequested && pauseWriteAttempted && originalPauseHex) {
        try {
          const currentHex = await readDestinyPauseInstruction(generation);
          if (currentHex !== originalPauseHex && currentHex !== DESTINY_PAUSE_SITE.pausedHex) {
            throw new Error(`Pause instruction changed to ${currentHex}; refusing to overwrite the new value`);
          }
          if (currentHex !== originalPauseHex) {
            await verifiedConditionalMemoryValue(
              DESTINY_PAUSE_SITE.address.toString(16),
              DESTINY_PAUSE_SITE.pausedHex,
              originalPauseHex
            );
          }
          ensureOperationSession(generation);
          const resumeButton = document.getElementById('screenshot-resume-btn');
          if (resumeButton) resumeButton.disabled = true;
          saveScreenshotPauseRestore(null);
          window.rtmFramebufferBusy = false;
        } catch (error) {
          window.rtmFramebufferBusy = true;
          const resumeButton = document.getElementById('screenshot-resume-btn');
          if (resumeButton) resumeButton.disabled = false;
          const recoveryMessage = String(error?.message || error).startsWith('Could not clear pause recovery data:')
            ? `Capture ${imageReady ? 'finished' : 'failed'} and the game pause was restored, but its recovery record could not be cleared. Use Restore Pause to retry.`
            : `Capture ${imageReady ? 'finished' : 'failed'}, but pause recovery needs attention. Use Restore Pause.`;
          setScreenshotStatus(recoveryMessage, true);
          log(`Framebuffer pause recovery needs manual restore: ${error.message}`, 'err');
        }
      } else if (pauseRequested) {
        window.rtmFramebufferBusy = false;
      }
      progress.style.display = 'none';
      if (button) button.disabled = false;
      if (pauseRequested) pauseOperationBusy = false;
    }
    if (captureError && screenshotPauseRestore) toast('Screenshot issue · game pause recovery needs attention', 'err');
  }

  window.addEventListener('beforeunload', () => screenshotCaptures.clear(), { once: true });

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
      await verifiedMemoryValue(addrHex,valHex);
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
      const response = await consoleTransport.request(`http://${ip}/getcid.ps3mapi`);
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
      log('CID update request acknowledged; resulting CID not read back', '');
      toast('CID update request acknowledged', '');
    } catch(e) {
      log('Failed to set CID', 'err');
      toast('Failed to set CID', 'err');
    }
  }

