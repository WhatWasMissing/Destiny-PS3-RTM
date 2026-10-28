(function (root) {
  'use strict';

  // Behavior and address map ported from the user's local Overwalls Tool and
  // SPRX Retrofit sources. The implementation uses Destiny RTM's verified
  // transport and keeps the source's 32-bit activity loading as an option.
  const SCENARIO_KEY = 'overwalls_scenario_positions';
  const TELEPORT_MODE_KEY = 'overwalls_teleport_mode';
  const ACTIVITY_METHOD_KEY = 'overwalls_activity_method';
  const REP_BACKUP_KEY = 'overwalls_split_rep_backup';
  const CONSUMABLE_BACKUP_KEY = 'overwalls_consumable_backup';
  const PLATE_BACKUP_KEY = 'overwalls_plate_hook_backup';
  const PHYSICS_BACKUP_KEY = 'overwalls_physics_hook_backup';
  const WARPRIEST_BACKUP_KEY = 'overwalls_warpriest_secondary_backup';
  const WARPRIEST_PRIMARY_ADDRESS = '00444CA4';
  const WARPRIEST_SECONDARY_ADDRESS = '01891AE0';
  const WARPRIEST_PRIMARY_ON = 'C0420348';
  const WARPRIEST_PRIMARY_OFF = 'C04100F0';
  const WARPRIEST_SECONDARY_ON = '3F800000';
  const PHYSICS_ADDRESS = 0x010D8EC0;
  const PHYSICS_BYTES = '9421FFB0F9E10010FA010018FA210020FA4100287DE000263C6001906063EBB08203001482240140825000007C119000418200084800000C90830000480000A089E300072C0F000140820094820300007C04800041820088DA010030DA210038C2100140C2230040EE10882AD2040140C2100144C2230044EE10882AD2040144C2100148C2230048EE10882AD2040148CA010030CA21003889E300062C0F00014082003CC21001B0C223004CEE10882AD20401B0C21001B4C2230050EE10882AD20401B4C21001B8C2230054EE10882AD20401B8CA010030CA2100387DEFF120E9E10010EA010018EA210020EA410028306401403C80010A382100504B0EB15C';
  const SPRX_PATH = '/dev_hdd0/tmp/Destiny_Flyhack.sprx';
  const FLY_MAIN_ADDRESS = 0x010A2818;
  const FLY_MAIN_VALUE = '4E800420';
  const FLY_TEST_ADDRESS = 0x01F0A040;
  const FLY_TEST_VALUE = 'FlyHackTestString';
  const PLATE_ADDRESS = 0x010FB514;
  const PLATE_BYTES = '9421FFD0F9E10010FA010018FA210020FA4100287E4000263063FFF43DE0019061EFEBB08A0F00E42C100000820F00E88223001C7C918000E9E10010EA010018EA2100203821003041820008418600107E4FF120EA41FFF84B9060447E4FF120EA41FFF84B90603C';
  const PLATE_BRANCH_ADDRESS = 0x00A015AC;
  const PLATE_BRANCH_ORIGINAL = '3063FFF44BFFFB15';
  const PLATE_BRANCH_VALUE = '486F9F68';
  const PLATE_FLAG_ADDRESS = 0x0190EC98;

  const towerPositions = [
    ['Shaxx', 7.612, 43.23, -5.5],
    ['Hangar', 55.0390129, 19.16361, 7.069035],
    ['Dead Orbit', 70.58417, 56.0182343, -4.60912657],
    ['North Tower', -59.39258, 22.4960556, -0.123155147],
    ['New Monarchy', -77.2933044, 15.4192839, -5.33833361],
    ['Future War Cult', 87.48229, 65.49484, 7.31086636],
    ['Hangar out', 62.69, -12.3, 3.63],
    ['Postmaster', -8.175, -13.444, 0.876],
    ['North out', -68.4, -6.68, 1.21],
    ['Rahool', 30.05, -18.25, 1.2],
    ['Gunsmith', 39.95226, 13.2089071, 3.67647],
    ['Eva Levante', -69, 41, -5.3],
    ['Speaker', -101.9, -2.03, -1.378],
    ['Quartermaster', 111.38, 48.51, -1.8],
    ['Crucible farming spot', 75.17019, 72.3170242, 2.307823],
    ['Valus', -744.75, 16.81, -37.7],
    ['Engram room', -586.1367, 1080, 4.6],
    ['Echo Chamber arena', 570.32, 79.57, -2.34],
    ['Off map', 0, 10000, 0]
  ];
  const activityPositions = [
    ['SRL', 125], ['Private Tower · random 2–7', 'randomTower'], ['Rumble', 138],
    ['Taniks · regular', 326], ['Echo Chamber', 89], ['VoG · 31', 14],
    ['Cerberus Vae III', 81], ['King’s Fall', 158], ['Crota’s End', 16],
    ['Winter’s Run', 88], ['Reef', 39], ['Challenge of Elders', 330]
  ];
  const farmProfiles = {
    normal: { match: 63675, tower: 50000, gunsmith: 15000, shaxx: 15000, load: 7500, faction: 28300 },
    slow: { match: 105075, tower: 100000, gunsmith: 23300, shaxx: 22000, load: 19975, faction: 47775 },
    speed: { match: 13278, tower: 3417, gunsmith: 11500, shaxx: 11500, load: 4000, faction: 23054 },
    newchar: { match: 13318, tower: 3514, gunsmith: 11500, shaxx: 6500, load: 9000, faction: 23054 }
  };

  let job = null;
  let scenario = [];
  let flySliderTimer = null;
  const scenarioLibrary = Array.isArray(root.DESTINY_RTM_SCENARIO_LIBRARY) ? root.DESTINY_RTM_SCENARIO_LIBRARY : [];
  try {
    const saved = JSON.parse(localStorage.getItem(SCENARIO_KEY) || '[]');
    if (Array.isArray(saved)) scenario = saved.filter(validScenarioPosition).slice(0, 500);
  } catch (_) { scenario = []; }

  function html(value) {
    return String(value).replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
  }
  function report(message, type) {
    const status = document.getElementById('ow-job-status');
    if (status) status.textContent = String(message);
    toast(String(message), type || '');
    log(html(message), type || '');
  }
  function flyStatus(message, type) {
    const status = document.getElementById('ow-fly-status');
    if (status) status.textContent = String(message);
    if (type) log(html(message), type);
  }
  function hexU32(value) { return (Number(value) >>> 0).toString(16).padStart(8, '0').toUpperCase(); }
  function hexAddress(value) { return Number(value).toString(16).toUpperCase().padStart(8, '0'); }
  function normalizeAddress(value) {
    if (typeof value !== 'string') return hexAddress(value);
    const raw = value.trim().replace(/^0x/i, '');
    if (!/^[0-9a-f]{1,8}$/i.test(raw)) throw new Error('Invalid memory address');
    return raw.toUpperCase().padStart(8, '0');
  }
  function signedHex(value) { return hexU32(Number(value)); }
  function readFloat(hex) {
    const bytes = new Uint8Array(hex.match(/.{2}/g).map(byte => parseInt(byte, 16)));
    return new DataView(bytes.buffer).getFloat32(0, false);
  }
  function finiteFloat(value, label) {
    const number = Number(value);
    if (!Number.isFinite(number) || !Number.isFinite(Math.fround(number))) throw new Error(`${label} must be a finite 32-bit value`);
    return number;
  }
  function requireConnected() {
    if (!ip) throw new Error('Connect to the Destiny process first');
  }
  function checkContext(context) {
    if (context) {
      ensureOperationSession(context.generation);
      if (context.signal.aborted) throw new DOMException('Operation stopped', 'AbortError');
    } else requireConnected();
  }
  async function readHex(address, length, context) {
    checkContext(context);
    const result = (await ps3getmem(address, length)).replace(/\s/g, '').toUpperCase();
    checkContext(context);
    if (!new RegExp(`^[0-9A-F]{${length * 2}}$`).test(result)) throw new Error(`Short read at 0x${hexAddress(address)}`);
    return result;
  }
  async function writeHex(address, value, context) {
    checkContext(context);
    const hex = String(value).replace(/\s/g, '').toUpperCase();
    if (!/^(?:[0-9A-F]{2})+$/.test(hex)) throw new Error('Memory value must contain whole bytes');
    await verifiedMemoryValue(hexAddress(address), hex);
    checkContext(context);
    return hex;
  }
  async function writePairs(pairs, context) {
    checkContext(context);
    const normalized = pairs.map(([address, value]) => [normalizeAddress(address), String(value).replace(/\s/g, '').toUpperCase()]);
    const result = await verifiedMemorySequence(normalized);
    checkContext(context);
    return result;
  }
  async function writePairsIfEqual(pairs, preconditions, context) {
    checkContext(context);
    const normalized = pairs.map(([address, value]) => [normalizeAddress(address), String(value).replace(/\s/g, '').toUpperCase()]);
    const expected = preconditions.map(([address, value]) => [normalizeAddress(address), String(value).replace(/\s/g, '').toUpperCase()]);
    const result = await verifiedConditionalMemorySequence(normalized, expected);
    checkContext(context);
    return result;
  }
  async function readU32(address, context) { return parseInt(await readHex(address, 4, context), 16) >>> 0; }
  async function readFloatAt(address, context) { return readFloat(await readHex(address, 4, context)); }
  function makeContext(signal) { return { signal, generation: connectionGeneration, actionMs: 0 }; }
  function newContext() { requireConnected(); return makeContext(new AbortController().signal); }
  async function action(context, callback) {
    checkContext(context);
    const start = performance.now();
    const value = await callback();
    context.actionMs += performance.now() - start;
    checkContext(context);
    return value;
  }
  function wait(ms, context) {
    checkContext(context);
    const duration = Math.max(0, Math.floor(Number(ms) || 0));
    if (!duration) return Promise.resolve();
    const deadline = Date.now() + duration;
    return new Promise((resolve, reject) => {
      let timer;
      const cleanup = () => { clearTimeout(timer); context.signal.removeEventListener('abort', aborted); };
      const aborted = () => { cleanup(); reject(new DOMException('Operation stopped', 'AbortError')); };
      const tick = () => {
        try { checkContext(context); }
        catch (error) { cleanup(); reject(error); return; }
        const remaining = deadline - Date.now();
        if (remaining <= 0) { cleanup(); resolve(); return; }
        timer = setTimeout(tick, Math.min(200, remaining));
      };
      context.signal.addEventListener('abort', aborted, { once: true });
      timer = setTimeout(tick, Math.min(200, duration));
    });
  }
  async function runJob(label, worker) {
    if (job) { toast(`Stop “${job.label}” before starting another routine`, 'err'); return; }
    requireConnected();
    const controller = new AbortController();
    const context = makeContext(controller.signal);
    job = { label, controller, context };
    const status = document.getElementById('ow-job-status');
    if (status) status.textContent = `${label} · running`;
    log(`${html(label)} started`, '');
    try {
      await worker(context);
      checkContext(context);
      if (status) status.textContent = `${label} · complete`;
      toast(`${label} complete`, 'ok');
      log(`${html(label)} complete`, 'ok');
    } catch (error) {
      const stopped = error.name === 'AbortError' || controller.signal.aborted || context.generation !== connectionGeneration;
      const message = stopped ? `${label} stopped` : `${label} stopped: ${error.message}`;
      if (status) status.textContent = message;
      log(html(message), stopped ? '' : 'err');
      if (!stopped) toast(`${label} failed`, 'err');
    } finally {
      if (job && job.controller === controller) job = null;
    }
  }
  function stopJob() {
    if (!job) { toast('No routine is running', ''); return; }
    job.controller.abort();
    const status = document.getElementById('ow-job-status');
    if (status) status.textContent = `${job.label} · stopping after the current console action`;
  }

  async function teleport(coords, context, activationMode) {
    context = context || newContext();
    const [x, y, z] = coords.map((value, index) => finiteFloat(value, ['X', 'Y', 'Z'][index]));
    checkContext(context);
    const pointer = await readU32(0x0190EBB0, context);
    if (pointer < 0x10000 || pointer > 0x7FFFFFFF) throw new Error(`Invalid player pointer: 0x${hexAddress(pointer)}`);
    await writeHex(pointer + 0x140, floatToHex(x) + floatToHex(y) + floatToHex(z), context);
    const mode = activationMode || document.getElementById('ow-teleport-mode')?.value || localStorage.getItem(TELEPORT_MODE_KEY) || 'external';
    if (mode === 'external') await writeHex(pointer + 0x1B8, floatToHex(1), context);
    else if (mode === 'internal') await writeHex(0x01FFF028, floatToHex(1), context);
    return { x, y, z, pointer, mode };
  }
  async function teleportTarget(coords, context) {
    context = context || newContext();
    const player = await readU32(0x0190EBB0, context);
    const target = await readU32(0x0190EBB4, context);
    if (player < 0x10000 || player > 0x7FFFFFFF || target < 0x10000 || target > 0x7FFFFFFF || target === player) throw new Error('The OPK target is unavailable or is the player pointer');
    const [x, y, z] = coords.map((value, index) => finiteFloat(value, ['X', 'Y', 'Z'][index]));
    await writeHex(target + 0x140, floatToHex(x) + floatToHex(y) + floatToHex(z), context);
    await writeHex(target + 0x1B8, floatToHex(1), context);
  }
  function currentCoords() {
    return ['x', 'y', 'z'].map(axis => finiteFloat(document.getElementById(`tp-${axis}`).value, axis.toUpperCase()));
  }
  function saveOptions() {
    const teleportSelect = document.getElementById('ow-teleport-mode');
    const activitySelect = document.getElementById('ow-activity-method');
    if (teleportSelect) localStorage.setItem(TELEPORT_MODE_KEY, teleportSelect.value);
    if (activitySelect) localStorage.setItem(ACTIVITY_METHOD_KEY, activitySelect.value);
  }
  async function towerGo(index) {
    const entry = towerPositions[index];
    if (!entry) return;
    try {
      const result = await teleport(entry.slice(1));
      report(`${entry[0]} position bytes verified · movement not observed (${result.mode} activation)`, 'ok');
    } catch (error) { report(`${entry[0]}: ${error.message}`, 'err'); }
  }
  async function activityLoad(index) {
    let value = index;
    if (index === 'randomTower') value = 2 + Math.floor(Math.random() * 6);
    const method = document.getElementById('ow-activity-method')?.value || 'current';
    try {
      await loadActivityValue(value, method);
      report(`Activity ${value} (${ALL_ACTIVITIES[value - 1] || 'unknown'}) · index bytes verified; load completion not observed`, 'ok');
    } catch (error) { report(`Activity ${value}: ${error.message}`, 'err'); }
  }
  async function loadActivityValue(index, method, context) {
    context = context || newContext();
    requireConnected();
    const value = Number(index);
    if (!Number.isInteger(value) || value < 1 || value > 65535) throw new Error('Activity index must be from 1 to 65535');
    checkContext(context);
    if (method === 'overwalls') {
      await writeHex(0x30656E6C, hexU32(value), context);
      const readback = parseInt(await readHex(0x30656E6E, 2, context), 16);
      if (readback !== value) throw new Error(`Overwalls activity readback mismatch (${readback})`);
    } else {
      for (const address of ACT_ADDRS) await writeHex(address, value.toString(16).padStart(4, '0'), context);
    }
  }
  async function readActivity() {
    try {
      const context = newContext();
      // Match the local Activity Loader's ReadUInt16 site, also used by the
      // main RTM's Read Current action. The first site is a write destination.
      const value = parseInt(await readHex(ACT_ADDRS[1], 2, context), 16);
      const name = ALL_ACTIVITIES[value - 1] || `Unknown activity ${value}`;
      const input = document.getElementById('ow-activity-index');
      if (input) input.value = value;
      report(`Current activity: ${value} · ${name}`, 'ok');
    } catch (error) { report(`Read activity failed: ${error.message}`, 'err'); }
  }
  async function readCoordinates() {
    try {
      const context = newContext();
      const pointer = await readU32(0x0190EBB0, context);
      if (pointer < 0x10000 || pointer > 0x7FFFFFFF) throw new Error('Player pointer is invalid');
      const values = [];
      for (const offset of [0x140, 0x144, 0x148]) values.push(readFloat(await readHex(pointer + offset, 4, context)));
      if (!values.every(Number.isFinite)) throw new Error('Coordinate read returned an invalid float');
      ['x', 'y', 'z'].forEach((axis, index) => { document.getElementById(`tp-${axis}`).value = values[index].toFixed(3); });
      report(`Current coordinates · X ${values[0].toFixed(3)}, Y ${values[1].toFixed(3)}, Z ${values[2].toFixed(3)}`, 'ok');
    } catch (error) { report(`Read coordinates failed: ${error.message}`, 'err'); }
  }
  async function pdr() {
    try {
      const context = newContext();
      const pointer = await readU32(0x0190EBB0, context);
      if (pointer < 0x10000 || pointer > 0x7FFFFFFF) throw new Error('Player pointer is invalid');
      await writeHex(pointer + 0x148, '47C35000', context);
      await writeHex(pointer + 0x1B0, floatToHex(1), context);
      report('PDR position bytes verified · movement not observed', 'ok');
    } catch (error) { report(`PDR failed: ${error.message}`, 'err'); }
  }
  async function pdrStaticOff() {
    try {
      const context = newContext();
      await teleport([0, 10000, 0], context);
      await writeHex(0x002EDAC4, floatToHex(1), context);
      report('PDR + Static Actors off · memory bytes verified; movement not observed', 'ok');
    } catch (error) { report(`PDR + Static Actors off failed: ${error.message}`, 'err'); }
  }
  async function aboveYou() {
    try {
      const context = newContext();
      const pointer = await readU32(0x0190EBB0, context);
      if (pointer < 0x10000 || pointer > 0x7FFFFFFF) throw new Error('Player pointer is invalid');
      const z = readFloat(await readHex(pointer + 0x148, 4, context));
      if (!Number.isFinite(z)) throw new Error('Current Z coordinate is invalid');
      await writeHex(pointer + 0x148, floatToHex(z + 40), context);
      await writeHex(pointer + 0x1B8, floatToHex(1), context);
      report('Z offset +40 verified · movement not observed', 'ok');
    } catch (error) { report(`Above-you action failed: ${error.message}`, 'err'); }
  }
  async function setOPKPreset(text) {
    try {
      const context = newContext();
      const values = text.split(',').map((value, index) => finiteFloat(value.trim(), ['X', 'Y', 'Z'][index]));
      await writeHex(0x0190EBF0, values.map(floatToHex).join(''), context);
      document.getElementById('ow-opk-offset').value = values[0];
      report(`OPK offsets verified · ${values.join(', ')}`, 'ok');
    } catch (error) { report(`OPK preset failed: ${error.message}`, 'err'); }
  }
  async function runTargetTeleport() {
    try {
      await teleportTarget(currentCoords(), newContext());
      report('OPK target position and activation verified · movement not observed', 'ok');
    } catch (error) { report(`OPK target teleport failed: ${error.message}`, 'err'); }
  }

  function validScenarioPosition(item) {
    return !!item && typeof item.name === 'string' && item.name.trim().length > 0 && item.name.length <= 200 &&
      ['x', 'y', 'z'].every(axis => Number.isFinite(Number(item[axis])) && Number.isFinite(Math.fround(Number(item[axis])))) &&
      (item.id === undefined || typeof item.id === 'string' && item.id.length <= 100) &&
      (item.legacyName === undefined || typeof item.legacyName === 'string' && item.legacyName.length <= 100);
  }
  function persistScenario() {
    try { localStorage.setItem(SCENARIO_KEY, JSON.stringify(scenario)); return true; }
    catch (error) { toast(`Could not save scenario: ${error.message}`, 'err'); return false; }
  }
  function renderScenario() {
    const container = document.getElementById('ow-scenario-list');
    if (!container) return;
    if (!scenario.length) { container.innerHTML = '<div class="ow-empty">No scenario loaded. Import an Overwalls XML file or add a position.</div>'; return; }
    container.innerHTML = scenario.map((item, index) => `<div class="ow-scenario-item">
      <button class="cheat-btn" type="button" onclick="owScenarioGo(${index})"><span class="cheat-name">${html(item.name)}</span><span class="cheat-desc">X ${Number(item.x)} · Y ${Number(item.y)} · Z ${Number(item.z)}</span></button>
      <div class="ow-scenario-actions"><button type="button" title="Edit position" onclick="owEditScenario(${index})">Edit</button><button type="button" title="Delete position" onclick="owDeleteScenario(${index})">×</button></div>
    </div>`).join('');
  }
  function parseScenarioXML(text) {
    if (text.length > 1000000) throw new Error('Scenario file exceeds 1 MB');
    if (/<!\s*(?:DOCTYPE|ENTITY)/i.test(text)) throw new Error('Scenario XML cannot declare a document type or entity');
    const parsed = new DOMParser().parseFromString(text, 'application/xml');
    if (parsed.querySelector('parsererror')) throw new Error('Scenario XML could not be parsed');
    const rootNode = parsed.documentElement;
    if (!rootNode || !['ArrayOfButtonData', 'ButtonDataList'].includes(rootNode.localName)) throw new Error('Unsupported scenario XML root');
    const nodes = Array.from(rootNode.children).filter(node => node.localName === 'ButtonData');
    if (nodes.length > 500) throw new Error('Scenario contains more than 500 positions');
    const items = nodes.map((node, index) => {
      const child = name => Array.from(node.children).find(element => element.localName === name)?.textContent || '';
      const legacyName = child('Name') || `button${index + 1}`;
      const name = child('Text') || legacyName;
      const item = { id: `ow-${index + 1}`, legacyName, name,
        x: Number(child('X')), y: Number(child('Y')), z: Number(child('Z')) };
      if (!validScenarioPosition(item)) throw new Error(`Invalid scenario position ${index + 1}`);
      return item;
    });
    if (!items.length) throw new Error('Scenario has no ButtonData positions');
    return items;
  }
  function importScenario() {
    const input = document.getElementById('ow-scenario-file');
    input.value = '';
    input.click();
  }
  function renderScenarioLibrary() {
    const select = document.getElementById('ow-scenario-library');
    if (!select) return;
    select.innerHTML = '<option value="">Choose an archived scenario…</option>' + scenarioLibrary.map(item =>
      `<option value="${html(item.id)}">${html(item.name)} · ${Array.isArray(item.positions) ? item.positions.length : 0}</option>`
    ).join('');
  }
  async function loadScenarioLibrary() {
    const selectedId = document.getElementById('ow-scenario-library')?.value;
    const entry = scenarioLibrary.find(item => item?.id === selectedId);
    if (!entry) { report('Choose an archived scenario first', 'err'); return; }
    let imported;
    try {
      if (!Array.isArray(entry.positions) || !entry.positions.length || entry.positions.length > 500) throw new Error('Scenario library entry has an invalid position list');
      imported = entry.positions.map((position, index) => {
        const item = { id: `${entry.id}-${index + 1}`, legacyName: position.legacyName, name: position.name,
          x: Number(position.x), y: Number(position.y), z: Number(position.z) };
        if (!validScenarioPosition(item)) throw new Error(`Invalid scenario position ${index + 1}`);
        return item;
      });
    } catch (error) { report(`Scenario load failed: ${error.message}`, 'err'); return; }
    if (scenario.length && !await RtmDialogs.confirm(`Replace the current Overwalls scenario with “${entry.name}” (${imported.length} positions)?`)) return;
    const previous = scenario;
    scenario = imported;
    if (!persistScenario()) { scenario = previous; renderScenario(); return; }
    renderScenario();
    report(`Loaded ${entry.name} from the built-in archive · ${imported.length} positions`, 'ok');
  }
  async function importScenarioFile(file) {
    try {
      const imported = parseScenarioXML(await file.text());
      if (!await RtmDialogs.confirm(`Replace the current Overwalls scenario with ${imported.length} positions?`)) return;
      scenario = imported;
      persistScenario(); renderScenario();
      report(`Imported ${imported.length} Overwalls scenario positions`, 'ok');
    } catch (error) { report(`Scenario import failed: ${error.message}`, 'err'); }
  }
  function exportScenario() {
    if (!scenario.length) { toast('There are no scenario positions to export', 'err'); return; }
    const xml = `<?xml version="1.0" encoding="utf-8"?>\n<ArrayOfButtonData xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:xsd="http://www.w3.org/2001/XMLSchema">\n${scenario.map((item, index) => `  <ButtonData><Name>${html(item.legacyName || `button${index + 1}`)}</Name><Text>${html(item.name)}</Text><X>${Number(item.x)}</X><Y>${Number(item.y)}</Y><Z>${Number(item.z)}</Z></ButtonData>`).join('\n')}\n</ArrayOfButtonData>`;
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob([xml], { type: 'application/xml;charset=utf-8' }));
    link.download = 'overwalls-scenario.xml'; link.click();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }
  async function readCurrentPlayerCoordsForScenario() {
    const context = newContext();
    const memory = root.rtmActivitySessionMemory;
    if (!memory || typeof memory.verify !== 'function' || typeof memory.read !== 'function') {
      throw new Error('The BLES 01.24 read-only memory capability is unavailable');
    }
    const owner = memory.session();
    const verified = await memory.verify(owner);
    checkContext(context);
    const pointer = parseInt(await memory.read(0x0190EBB0, 4, owner, verified.capability), 16) >>> 0;
    if (pointer < 0x10000 || pointer + 0x14C > 0x40000000) {
      throw new Error(`The player coordinate pointer is outside BLES user memory: 0x${hexAddress(pointer)}`);
    }
    const values = await memory.read(pointer + 0x140, 12, owner, verified.capability);
    const pointerAfter = parseInt(await memory.read(0x0190EBB0, 4, owner, verified.capability), 16) >>> 0;
    checkContext(context);
    if (pointerAfter !== pointer) throw new Error('The local player changed while coordinates were read; try again');
    const coordinates = [0, 8, 16].map(offset => finiteFloat(readFloat(values.slice(offset, offset + 8)), 'Coordinate'));
    return coordinates;
  }

  function readCurrentPositionDialogAction() {
    return {
      label: 'Read current position',
      async run({ setField, setStatus }) {
        setStatus('Checking the BLES 01.24 profile and reading the player position…', 'pending');
        const [x, y, z] = await readCurrentPlayerCoordsForScenario();
        setField('x', x);
        setField('y', y);
        setField('z', z);
        setStatus(`Current position captured · X ${x.toFixed(3)} · Y ${y.toFixed(3)} · Z ${z.toFixed(3)}`, 'ok');
      }
    };
  }

  async function editScenario(index) {
    const current = scenario[index];
    if (!current) return;
    const result = await RtmDialogs.fields({ title: 'Edit scenario position', secondaryActions: [readCurrentPositionDialogAction()], fields: [
      { name: 'name', label: 'Shortcut name', value: current.name },
      { name: 'x', label: 'X coordinate', type: 'number', value: current.x },
      { name: 'y', label: 'Y coordinate', type: 'number', value: current.y },
      { name: 'z', label: 'Z coordinate', type: 'number', value: current.z }
    ] });
    if (!result) return;
    const updated = { ...current, name: result.name.trim(), x: Number(result.x), y: Number(result.y), z: Number(result.z) };
    if (!validScenarioPosition(updated)) { toast('Enter a name and finite coordinates', 'err'); return; }
    scenario[index] = updated; persistScenario(); renderScenario();
  }
  async function addScenarioPosition() {
    const result = await RtmDialogs.fields({ title: 'Add scenario position', secondaryActions: [readCurrentPositionDialogAction()], fields: [
      { name: 'name', label: 'Shortcut name', value: `Position ${scenario.length + 1}` },
      { name: 'x', label: 'X coordinate', type: 'number', value: 0 },
      { name: 'y', label: 'Y coordinate', type: 'number', value: 0 },
      { name: 'z', label: 'Z coordinate', type: 'number', value: 0 }
    ] });
    if (!result) return;
    const item = { id: `ow-${Date.now()}`, legacyName: `button${scenario.length + 1}`, name: result.name.trim(), x: Number(result.x), y: Number(result.y), z: Number(result.z) };
    if (!validScenarioPosition(item)) { toast('Enter a name and finite coordinates', 'err'); return; }
    scenario.push(item); persistScenario(); renderScenario();
  }
  async function deleteScenario(index) {
    if (!scenario[index] || !await RtmDialogs.confirm(`Delete “${scenario[index].name}” from this scenario?`)) return;
    scenario.splice(index, 1); persistScenario(); renderScenario();
  }
  async function clearScenario() {
    if (!scenario.length || !await RtmDialogs.confirm('Clear all Overwalls scenario positions?')) return;
    scenario = []; persistScenario(); renderScenario();
  }
  async function scenarioGo(index) {
    const item = scenario[index];
    if (!item) return;
    try { const result = await teleport([item.x, item.y, item.z]); report(`${item.name} bytes verified · movement not observed (${result.mode})`, 'ok'); }
    catch (error) { report(`${item.name}: ${error.message}`, 'err'); }
  }

  function setFarmStatus(message) {
    const status = document.getElementById('ow-job-status');
    if (status) status.textContent = message;
  }
  async function runTotems(context) {
    let count = 0;
    while (true) {
      await action(context, () => teleport([-1290.157, -673.6193, 25.49], context));
      await action(context, () => teleport([-1292.213, -640.3, 30.9], context));
      count++;
      setFarmStatus(`AFK Totems · ${count} rounds · waiting 5 seconds`);
      await wait(5000, context);
    }
  }
  const totemRoutes = [
    ['Mid + Right', [-1228.034, -640.0425, 30.679], [-1229.482, -662.675, 34.734]],
    ['Right + Mid', [-1229.482, -662.675, 34.734], [-1228.034, -640.0425, 30.679]],
    ['Mid + Left', [-1228.034, -640.0425, 30.679], [-1230.881, -620.74, 44.19]],
    ['Right + Left', [-1229.482, -662.675, 34.734], [-1230.881, -620.74, 44.19]],
    ['Left + Mid', [-1230.881, -620.74, 44.19], [-1228.034, -640.0425, 30.679]],
    ['Left + Right', [-1230.881, -620.74, 44.19], [-1229.482, -662.675, 34.734]]
  ];
  function runTotemRoute(index) {
    const route = totemRoutes[index];
    if (!route) return;
    return runJob(`Totems route · ${route[0]}`, async context => {
      await action(context, () => teleport(route[1], context));
      await wait(300, context);
      await action(context, () => teleport(route[2], context));
    });
  }
  async function runRaid(key, context) {
    const offMap = [0, 20000.81, -37.7];
    const setStatic = value => writeHex(0x002EDAC4, floatToHex(value ? 0 : 1), context);
    const setBullets = value => writeHex(0x0051E7E0, floatToHex(value ? 0 : 1), context);
    const setInternal = async (mode) => {
      await requireFlyhackReady(context);
      const offset = Number(document.getElementById('ow-opk-offset').value) + 10;
      await writeHex(0x0190EBB7, mode === 'off' ? '00' : '01', context);
      await writeHex(0x0190EBF0, floatToHex(mode === 'void' ? 1e8 : offset), context);
    };
    if (key === 'archon') {
      await action(context, () => teleport([-586.1367, 1080, 4.6], context)); await wait(500, context);
      await action(context, () => teleport([-615.6052, 1090.871, 7.58137], context));
      await setBullets(true); await setInternal('relative'); await wait(3700, context);
      await action(context, () => teleport(offMap, context)); await wait(2500, context);
      await setBullets(false); await wait(4000, context); await setInternal('off');
    } else if (key === 'valus') {
      await action(context, () => teleport([-744.75, 16.81, -37.7], context));
      await setStatic(true); await setInternal('void'); await wait(5650, context);
      await setInternal('relative'); await wait(8000, context); await setStatic(false);
      await setInternal('off'); await action(context, () => teleport(offMap, context));
    } else if (key === 'bullets') {
      await action(context, () => teleport(offMap, context));
      await writeHex(0x0051E7E0, floatToHex(1), context); await wait(2500, context);
      await writeHex(0x002EDAC4, floatToHex(1), context);
      await writeHex(0x0051E7E0, floatToHex(1), context);
    } else if (key === 'echo') {
      await setStatic(true); await action(context, () => teleport([570.32, 79.57, -2.34], context));
      await wait(2500, context); await action(context, () => teleport([0, 10000, 0], context)); await setStatic(false);
    }
  }
  async function startRaid(key) {
    const labels = { archon: 'Taken Archon Priest · kill + wipe', valus: 'Valus · kill + wipe', bullets: 'Go bullets + wipe', echo: 'Echo Chamber · kill + wipe' };
    await runJob(labels[key] || 'Overwalls raid routine', context => runRaid(key, context));
  }

  async function farm(context) {
    const profileName = document.getElementById('ow-farm-profile').value;
    const profile = farmProfiles[profileName] || farmProfiles.normal;
    const faction = document.getElementById('ow-farm-faction').value;
    const towerSelect = Number(document.getElementById('ow-farm-tower').value);
    const cycles = Number(document.getElementById('ow-farm-cycles').value);
    if (!Number.isInteger(cycles) || cycles < 0 || cycles > 99) throw new Error('Cycles must be 0 to 99');
    await wait(2000, context);
    let j = 0, matchCount = 12, completedCycles = 0;
    const expedited = profileName === 'speed' || profileName === 'newchar';
    while (cycles === 0 || completedCycles < cycles) {
      if (profileName === 'newchar') matchCount = 12;
      for (let i = 0; i < matchCount; i++) {
        context.actionMs = 0;
        await action(context, () => loadActivityValue(138, document.getElementById('ow-activity-method').value, context));
        if (expedited) {
          await wait(13678, context); await action(context, () => writeHex(0x30586864, floatToHex(10), context));
          await wait(2878, context); await action(context, () => writeHex(0x30586864, floatToHex(10), context));
          await wait(7627, context);
          await action(context, async () => { await writeHex(0x30586864, '3F800000', context); await writeHex(0x30586868, 'FFFFFFFF', context); });
        }
        setFarmStatus(`Crucible farm · cycle ${completedCycles + 1} · match ${i + 1}/${matchCount}`);
        await wait(profile.match - context.actionMs, context);
      }
      context.actionMs = 0;
      let tower = towerSelect === 8 ? 2 + Math.floor(Math.random() * 6) : towerSelect;
      await action(context, () => loadActivityValue(tower, document.getElementById('ow-activity-method').value, context));
      if (expedited) {
        await wait(28428, context); await action(context, () => writeHex(0x30586864, floatToHex(10), context));
        await wait(2928, context); await action(context, () => writeHex(0x30586864, floatToHex(10), context));
        await wait(12128, context); await action(context, () => writeHex(0x30586864, floatToHex(1), context));
      }
      setFarmStatus(`Crucible farm · cycle ${completedCycles + 1} · tower ${tower}`);
      await wait(profile.tower - context.actionMs, context);

      context.actionMs = 0; await action(context, () => teleport([39.95226, 13.2089071, 3.67647], context));
      await wait(profile.gunsmith - context.actionMs, context);
      context.actionMs = 0; await action(context, () => teleport([7.42159128, 44.33166, -5.510729], context));
      await wait(profile.shaxx - context.actionMs, context);

      const approach = faction === 'new-monarchy' ? [-59.39258, 22.4960556, -0.123155147] : [55.0390129, 19.16361, 7.069035];
      context.actionMs = 0; await action(context, () => teleport(approach, context));
      await wait(profile.load - context.actionMs, context);
      const destination = faction === 'future-war-cult' ? [87.48229, 65.49484, 7.31086636] :
        faction === 'new-monarchy' ? [-77.2933044, 15.4192839, -5.33833361] : [70.58417, 56.0182343, -4.60912657];
      context.actionMs = 0;
      await action(context, () => teleport(destination, context)); await action(context, () => teleport(destination, context));
      await wait(profile.faction - context.actionMs, context);
      completedCycles++;
      j++;
      if (j === 2) { matchCount = 11; j = -1; } else matchCount = 12;
    }
  }
  function startFarm() { return runJob('Crucible farm', farm); }

  async function readAscii(address, length, context) {
    const hex = await readHex(address, length, context);
    let value = '';
    for (let index = 0; index < hex.length; index += 2) {
      const byte = parseInt(hex.slice(index, index + 2), 16);
      if (!byte) break;
      value += String.fromCharCode(byte);
    }
    return value;
  }
  async function writeAsciiContext(address, value, context) {
    const hex = Array.from(String(value), character => (character.charCodeAt(0) & 0xFF).toString(16).padStart(2, '0')).join('') + '00';
    const padded = hex.padEnd(Math.ceil(hex.length / 8) * 8, '0');
    for (let offset = 0; offset < padded.length; offset += 8) {
      await writeHex(address + offset / 2, padded.slice(offset, offset + 8), context);
    }
  }
  function int32FromHex(hex) {
    const bytes = new Uint8Array(hex.match(/.{2}/g).map(byte => parseInt(byte, 16)));
    return new DataView(bytes.buffer).getInt32(0, false);
  }
  async function readCharacterValues() {
    try {
      const context = newContext();
      const [name, glimmer, silver, marks] = await Promise.all([
        readAscii(0x32486DC4, 32, context), readHex(0x600E66B0, 4, context), readHex(0x600E66D0, 4, context), readHex(0x600E66F0, 4, context)
      ]);
      checkContext(context);
      document.getElementById('ow-stat-name').value = name;
      document.getElementById('ow-stat-glimmer').value = int32FromHex(glimmer);
      document.getElementById('ow-stat-silver').value = int32FromHex(silver);
      document.getElementById('ow-stat-marks').value = int32FromHex(marks);
      report('Character values read from local memory', 'ok');
    } catch (error) { report(`Character read failed: ${error.message}`, 'err'); }
  }
  async function writeCharacterValues() {
    try {
      const context = newContext();
      requireConnected();
      const inputMap = [['ow-stat-glimmer', 0x600E66B0], ['ow-stat-silver', 0x600E66D0], ['ow-stat-marks', 0x600E66F0]];
      const pairs = [];
      for (const [id, address] of inputMap) {
        const input = document.getElementById(id).value.trim();
        if (!input) continue;
        if (!/^-?\d+$/.test(input) || Number(input) < -2147483648 || Number(input) > 2147483647) throw new Error(`${id.replace('ow-stat-', '')} must fit a signed 32-bit integer`);
        pairs.push([hexAddress(address), signedHex(Number(input))]);
      }
      const name = document.getElementById('ow-stat-name').value.slice(0, 31);
      if (name) {
        checkContext(context);
        await writeAsciiContext(0x32486DC4, name, context);
        checkContext(context);
      }
      if (pairs.length) await writePairs(pairs, context);
      if (pairs.length || name) await writeHex(0x600E66C0, '0366', context);
      report('Entered character memory values verified · server persistence not observed', 'ok');
    } catch (error) { report(`Character write failed: ${error.message}`, 'err'); }
  }

  function readBackup(key) {
    try { return JSON.parse(localStorage.getItem(key) || 'null'); } catch (_) { return null; }
  }
  function writeBackup(key, value) {
    try { localStorage.setItem(key, JSON.stringify(value)); } catch (error) { throw new Error(`Could not save restoration bytes: ${error.message}`); }
  }
  async function enableSplitRep() {
    try {
      const context = newContext();
      requireConnected();
      if (readBackup(REP_BACKUP_KEY)) throw new Error('A split-rep snapshot already exists. Restore it before enabling again.');
      const quantity = Number(document.getElementById('ow-rep-quantity').value);
      if (!Number.isInteger(quantity) || quantity < 1 || quantity > 255) throw new Error('Rep quantity must be from 1 to 255');
      const snapshot = { ip, first: await readHex(0x600EAE80, 4, context), second: await readHex(0x600EAE92, 2, context), quantity };
      writeBackup(REP_BACKUP_KEY, snapshot);
      await writeHex(0x600EAE80, hexU32(363724800), context);
      await writeHex(0x600EAE92, '00' + quantity.toString(16).padStart(2, '0'), context);
      report('Split-rep values verified; original bytes saved for restoration', 'ok');
    } catch (error) { report(`Split-rep update failed: ${error.message}`, 'err'); }
  }
  async function restoreSplitRep() {
    try {
      const context = newContext();
      const snapshot = readBackup(REP_BACKUP_KEY);
      if (!snapshot || snapshot.ip !== ip) throw new Error('No split-rep snapshot exists for this console');
      const currentFirst = await readHex(0x600EAE80, 4, context);
      const currentSecond = await readHex(0x600EAE92, 2, context);
      const expectedFirst = hexU32(363724800);
      const expectedSecond = `00${Number(snapshot.quantity).toString(16).padStart(2, '0').toUpperCase()}`;
      if (![snapshot.first, expectedFirst].includes(currentFirst) || ![snapshot.second, expectedSecond].includes(currentSecond)) {
        throw new Error('Split-rep bytes changed; saved originals were left untouched');
      }
      if (currentFirst !== snapshot.first) await writeHex(0x600EAE80, snapshot.first, context);
      if (currentSecond !== snapshot.second) await writeHex(0x600EAE92, snapshot.second, context);
      localStorage.removeItem(REP_BACKUP_KEY); report('Split-rep original bytes restored and verified', 'ok');
    } catch (error) { report(`Split-rep restore failed: ${error.message}`, 'err'); }
  }
  function random16() { return Math.floor(Math.random() * 65535); }
  async function randomizeConsumables() {
    try {
      const context = newContext();
      requireConnected();
      if (readBackup(CONSUMABLE_BACKUP_KEY)) throw new Error('A consumable snapshot already exists. Restore it before randomizing again.');
      const original = [], changed = [];
      for (let index = 0; index < 20; index++) {
        checkContext(context);
        const base = 0x600EAE60 + index * 32;
        original.push([hexAddress(base), await readHex(base, 2, context)]);
        original.push([hexAddress(base + 16), await readHex(base + 16, 4, context)]);
        changed.push([hexAddress(base), random16().toString(16).padStart(4, '0').toUpperCase()]);
        changed.push([hexAddress(base + 16), hexU32(random16())]);
      }
      const snapshot = { ip, original, changed: [] };
      writeBackup(CONSUMABLE_BACKUP_KEY, snapshot);
      for (const [address, value] of changed) {
        snapshot.changed.push([address, value]);
        writeBackup(CONSUMABLE_BACKUP_KEY, snapshot);
        await writeHex(parseInt(address, 16), value, context);
      }
      report('Randomized 20 consumable entries; original memory bytes saved for restoration', 'ok');
    } catch (error) { report(`Consumable randomization failed: ${error.message}`, 'err'); }
  }
  async function restoreConsumables() {
    try {
      const context = newContext();
      const snapshot = readBackup(CONSUMABLE_BACKUP_KEY);
      if (!snapshot || snapshot.ip !== ip) throw new Error('No consumable snapshot exists for this console');
      const originalsByAddress = new Map(snapshot.original);
      const restore = [];
      for (const [address, expected] of snapshot.changed) {
        const current = await readHex(parseInt(address, 16), expected.length / 2, context);
        const original = originalsByAddress.get(address);
        if (current !== expected && current !== original) throw new Error(`Memory at 0x${address} changed; saved originals were left untouched`);
        if (current !== original) restore.push([address, original]);
      }
      if (restore.length) await writePairs(restore, context);
      localStorage.removeItem(CONSUMABLE_BACKUP_KEY); report('Consumable original bytes restored and verified', 'ok');
    } catch (error) { report(`Consumable restore failed: ${error.message}`, 'err'); }
  }

  const allPresetIds = [3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 21, 22];
  const usualPresetIds = [3, 6, 7, 10, 5, 9, 11, 13, 19];
  const cheatNames = {
    3: 'Instant Respawn', 4: 'Immune Warpriest', 5: 'No Recoil', 6: 'Unlimited Abilities',
    7: 'Unlimited Ammo', 8: 'No Collision', 9: '1 Hit Kill', 10: 'Rapid Fire',
    11: 'No Target', 12: 'God Mode', 13: 'Stop Bullets', 14: 'No Gravity',
    15: 'No Pushbacks · scuffed', 16: 'No Floors', 17: 'Static Actors',
    19: 'Unlimited Sparrow', 21: 'Super Speed', 22: 'Variable Buff'
  };
  function u32Pair(address, on, off) { return [[address, hexU32(on)], [address, hexU32(off)]]; }
  const cheatPresets = {
    3: u32Pair(7455560, 1610612736, 2478768176),
    4: { on: [[4476068, hexU32(3225551688)], [25762528, hexU32(1065353216)]], off: [[4476068, hexU32(3225485552)]] },
    5: [[5265116, floatToHex(0)], [5266724, floatToHex(0)], [3202116, floatToHex(0)], [5265116, floatToHex(1)], [5266724, floatToHex(1)], [3202116, floatToHex(1)]],
    6: [[2891188, hexU32(1610612736)], [13416936, hexU32(811794433)], [2897660, hexU32(3284336644)], [2891188, hexU32(3493527964)], [13416936, hexU32(811859967)], [2897660, hexU32(3284336640)]],
    7: [[3213548, hexU32(947922703)], [3212628, hexU32(950019855)], [13604268, '60000000FC1EF8003FC0461C63DE3C0093DF0110'], [3213548, hexU32(1673789440)], [3212628, hexU32(1673854976)], [13604268, '63DD0000408200083BA00001807F001CD3DF0110']],
    8: [[22005392, floatToHex(0)], [22005392, floatToHex(1)]],
    9: [[4449830, '547A'], [4449830, '3F80']],
    10: u32Pair(3175824, 1240736768, 1065353216),
    11: [[1599516, 'C0000000'], [1610212, 'C0000000'], [1599516, '3F800000'], [1610212, '3F800000']],
    12: u32Pair(4476064, 3223389256, 3223389016),
    13: [[5367776, floatToHex(0)], [5367776, floatToHex(1)]],
    14: [[21212424, floatToHex(-1)], [21212424, floatToHex(1)]],
    15: [[20485984, 'C0A000000000000000000000'], [20485984, '3F8000003F8000003F800000']],
    16: [[23630960, floatToHex(-1)], [23630960, floatToHex(1)]],
    17: [[3070660, floatToHex(0)], [3070660, floatToHex(1)]],
    19: u32Pair(5285632, 1610612736, 3491954732),
    21: [[1742528, '04'], [1742528, '00']],
    22: u32Pair(386580, 1077870596, 1065353216)
  };
  function cheatValues(id, enabled) {
    const definition = cheatPresets[id];
    if (!definition) throw new Error('Unknown source cheat');
    if (!Array.isArray(definition)) return definition[enabled ? 'on' : 'off'];
    const split = definition.length / 2;
    return definition.slice(enabled ? 0 : split, enabled ? split : undefined);
  }
  function readWarpriestSnapshot() {
    let raw;
    try { raw = localStorage.getItem(WARPRIEST_BACKUP_KEY); }
    catch (error) { throw new Error(`Could not read Warpriest recovery data: ${error.message}`); }
    if (raw === null) return null;
    let snapshot;
    try { snapshot = JSON.parse(raw); }
    catch (_) { throw new Error('Warpriest recovery data is unreadable; it was left untouched'); }
    if (!snapshot || snapshot.version !== 1 || typeof snapshot.ip !== 'string' ||
        !/^[0-9A-F]{8}$/.test(snapshot.playerPointer) || !/^[0-9A-F]{8}$/.test(snapshot.originalHex) ||
        snapshot.originalHex === WARPRIEST_SECONDARY_ON) {
      throw new Error('Warpriest recovery data is invalid; it was left untouched');
    }
    return snapshot;
  }
  async function readWarpriestPlayerAnchor(context) {
    const anchor = await readHex(0x0190EBB0, 4, context);
    const pointer = parseInt(anchor, 16) >>> 0;
    if (pointer < 0x10000 || pointer > 0x7FFFFFFF) throw new Error('A live player pointer is required to bind Warpriest recovery to this game session');
    return anchor;
  }
  async function setWarpriestImmunity(enabled, context) {
    checkContext(context);
    const primary = await readHex(parseInt(WARPRIEST_PRIMARY_ADDRESS, 16), 4, context);
    const secondary = await readHex(parseInt(WARPRIEST_SECONDARY_ADDRESS, 16), 4, context);
    checkContext(context);
    if (![WARPRIEST_PRIMARY_ON, WARPRIEST_PRIMARY_OFF].includes(primary)) {
      throw new Error(`Warpriest instruction is unknown (${primary}); no write was attempted`);
    }
    let snapshot = readWarpriestSnapshot();
    if (snapshot && snapshot.ip !== ip) throw new Error('Warpriest recovery data belongs to another console; reconnect to that console first');
    if (!enabled && snapshot && primary === WARPRIEST_PRIMARY_OFF && secondary === snapshot.originalHex) {
      localStorage.removeItem(WARPRIEST_BACKUP_KEY);
      return 'Warpriest bytes already match the saved original; recovery record cleared without writing';
    }
    let playerAnchor = null;
    if (snapshot) {
      playerAnchor = await readWarpriestPlayerAnchor(context);
      if (snapshot.playerPointer !== playerAnchor) {
        if (primary === WARPRIEST_PRIMARY_OFF && secondary === snapshot.originalHex) {
          localStorage.removeItem(WARPRIEST_BACKUP_KEY);
          snapshot = null;
          playerAnchor = null;
        } else {
          throw new Error('The live player pointer changed since enable; saved recovery data was left untouched');
        }
      }
    }

    if (enabled) {
      if (!snapshot && primary === WARPRIEST_PRIMARY_ON && secondary === WARPRIEST_SECONDARY_ON) {
        return 'Immune Warpriest is already active; its prior secondary value is unknown, so no recovery snapshot was fabricated';
      }
      if (!snapshot && secondary === WARPRIEST_SECONDARY_ON) {
        throw new Error('The secondary word already has the target value but no original snapshot exists; refusing an un-restorable enable');
      }
      if (snapshot && ![snapshot.originalHex, WARPRIEST_SECONDARY_ON].includes(secondary)) {
        throw new Error('The secondary word changed since enable; saved recovery data was left untouched');
      }
      if (!snapshot) {
        playerAnchor = await readWarpriestPlayerAnchor(context);
        snapshot = { version: 1, ip, playerPointer: playerAnchor, originalHex: secondary };
        writeBackup(WARPRIEST_BACKUP_KEY, snapshot);
      }
      const pairs = [];
      if (primary !== WARPRIEST_PRIMARY_ON) pairs.push([WARPRIEST_PRIMARY_ADDRESS, WARPRIEST_PRIMARY_ON]);
      if (secondary !== WARPRIEST_SECONDARY_ON) pairs.push([WARPRIEST_SECONDARY_ADDRESS, WARPRIEST_SECONDARY_ON]);
      if (pairs.length) await writePairsIfEqual(pairs, [
        [WARPRIEST_PRIMARY_ADDRESS, primary], [WARPRIEST_SECONDARY_ADDRESS, secondary],
        ['0190EBB0', playerAnchor]
      ], context);
      return pairs.length
        ? 'Immune Warpriest enabled; original secondary bytes are saved for guarded restoration'
        : 'Immune Warpriest already enabled; recovery snapshot retained';
    }

    if (!snapshot) {
      if (primary !== WARPRIEST_PRIMARY_OFF) await writePairsIfEqual(
        [[WARPRIEST_PRIMARY_ADDRESS, WARPRIEST_PRIMARY_OFF]],
        [[WARPRIEST_PRIMARY_ADDRESS, primary]], context
      );
      return 'Warpriest primary instruction disabled; secondary word left unchanged because no original snapshot exists';
    }
    if (![snapshot.originalHex, WARPRIEST_SECONDARY_ON].includes(secondary)) {
      throw new Error('The secondary word changed since enable; saved recovery data was left untouched');
    }
    const pairs = [];
    if (primary !== WARPRIEST_PRIMARY_OFF) pairs.push([WARPRIEST_PRIMARY_ADDRESS, WARPRIEST_PRIMARY_OFF]);
    if (secondary !== snapshot.originalHex) pairs.push([WARPRIEST_SECONDARY_ADDRESS, snapshot.originalHex]);
    if (pairs.length) await writePairsIfEqual(pairs, [
      [WARPRIEST_PRIMARY_ADDRESS, primary], [WARPRIEST_SECONDARY_ADDRESS, secondary],
      ['0190EBB0', playerAnchor]
    ], context);
    localStorage.removeItem(WARPRIEST_BACKUP_KEY);
    return 'Immune Warpriest disabled; saved secondary bytes restored and verified';
  }
  async function applyCheatToggle(id, enabled) {
    try {
      const context = newContext();
      if (Number(id) === 4) {
        report(await setWarpriestImmunity(Boolean(enabled), context), 'ok');
        return;
      }
      const pairs = cheatValues(Number(id), Boolean(enabled));
      await writePairs(pairs.map(([address, value]) => [hexAddress(address), value]), context);
      if (Number(id) === 9 && typeof root.syncDamagePatchState === 'function') {
        root.syncDamagePatchState(enabled ? '547A' : '3F80', 'Overwalls One-Hit toggle');
      }
      report(`${cheatNames[id] || 'Source cheat'} ${enabled ? 'enabled' : 'disabled'} · memory bytes verified`, 'ok');
    } catch (error) { report(`Cheat toggle not confirmed: ${error.message}`, 'err'); }
  }
  async function applyCheatPreset(kind) {
    try {
      const context = newContext();
      requireConnected();
      const ids = kind === 'usual' ? usualPresetIds : allPresetIds;
      const enabled = kind !== 'off';
      const pairs = [];
      for (const id of ids) if (id !== 4) pairs.push(...cheatValues(id, enabled));
      if (pairs.length) await writePairs(pairs.map(([address, value]) => [hexAddress(address), value]), context);
      let warpriestStatus = '';
      if (ids.includes(4)) warpriestStatus = ` · ${await setWarpriestImmunity(enabled, context)}`;
      if (ids.includes(9) && typeof root.syncDamagePatchState === 'function') {
        root.syncDamagePatchState(enabled ? '547A' : '3F80', `Overwalls ${kind} preset`);
      }
      report(`${kind === 'usual' ? 'Usual' : kind === 'all' ? 'All' : 'Disable all'} Overwalls cheat preset verified${warpriestStatus}`, 'ok');
    } catch (error) { report(`Cheat preset not confirmed: ${error.message}`, 'err'); }
  }
  async function setSkipCutscene() {
    try { await writeHex(0x30586868, floatToHex(1e11), newContext()); report('Skip-cutscene value verified; game response not observed', 'ok'); }
    catch (error) { report(`Skip-cutscene write failed: ${error.message}`, 'err'); }
  }
  async function startActivitySweep() {
    const start = Number(document.getElementById('ow-sweep-start').value);
    const delay = Number(document.getElementById('ow-sweep-delay').value);
    if (!Number.isInteger(start) || start < 1 || start > 65535 || !Number.isInteger(delay) || delay < 100 || delay > 10000) {
      toast('Use an activity index of 1–65535 and a delay of 100–10000 ms', 'err'); return;
    }
    await runJob('Activity ID sweep', async context => {
      for (let index = start; index <= 65535; index++) {
        await wait(delay, context);
        await action(context, () => loadActivityValue(index, document.getElementById('ow-activity-method').value, context));
        if ((index - start) % 25 === 0 || index === 65535) setFarmStatus(`Activity ID sweep · ${index}/65535`);
      }
    });
  }
  async function readMemoryDump() {
    try {
      const context = newContext();
      requireConnected();
      const input = document.getElementById('ow-dump-address').value.trim().replace(/^0x/i, '');
      if (!/^[0-9a-f]{1,8}$/i.test(input)) throw new Error('Enter a 1–8 digit hex address');
      const address = parseInt(input, 16);
      if (address <= 0 || address + 4096 > 0xFFFFFFFF) throw new Error('Address is out of range');
      let hex = '';
      for (let offset = 0; offset < 4096; offset += 512) hex += await readHex(address + offset, 512, context);
      document.getElementById('ow-memory-dump').value = hex.match(/.{1,64}/g).join('\n');
      report(`Read 4096 bytes at 0x${hexAddress(address)} in eight 512-byte chunks`, 'ok');
    } catch (error) { report(`Memory read failed: ${error.message}`, 'err'); }
  }

  async function flyhackReady(context) {
    const main = await readHex(FLY_MAIN_ADDRESS, 4, context);
    const test = await ps3readString(FLY_TEST_ADDRESS, FLY_TEST_VALUE.length + 1);
    checkContext(context);
    return { main, test, ready: main === FLY_MAIN_VALUE && test === FLY_TEST_VALUE };
  }
  async function requireFlyhackReady(context) {
    const result = await flyhackReady(context);
    if (!result.ready) throw new Error(`SPRX signatures are not ready (hook ${result.main}; test string ${result.test || 'not found'})`);
    return result;
  }
  async function checkFlyhack() {
    try {
      const context = newContext();
      requireConnected();
      const result = await flyhackReady(context);
      const message = result.ready ? 'SPRX ready · both runtime signatures verified' : `SPRX not ready · hook ${result.main === FLY_MAIN_VALUE ? 'found' : 'not found'}, test string ${result.test === FLY_TEST_VALUE ? 'found' : 'not found'}`;
      flyStatus(message, result.ready ? 'ok' : 'err'); toast(message, result.ready ? 'ok' : 'err');
    } catch (error) { flyStatus(`SPRX check failed: ${error.message}`, 'err'); toast('SPRX check failed', 'err'); }
  }
  async function loadFlyhack() {
    try {
      const context = newContext();
      requireConnected();
      if ((await flyhackReady(context)).ready) { flyStatus('SPRX is already loaded and verified', 'ok'); return; }
      if (!root.RtmHost || typeof root.RtmHost.loadModule !== 'function') throw new Error('Module loading is unavailable in this hosting environment');
      if (typeof root.ensureDestinyWriteProfile !== 'function') throw new Error('Destiny build verification is unavailable; module load refused');
      await root.ensureDestinyWriteProfile();
      const pid = parseInt(String(GAME_PID).replace(/^0x/i, ''), 16);
      checkContext(context);
      const response = await root.RtmHost.loadModule(ip, String(pid), SPRX_PATH);
      checkContext(context);
      flyStatus(`Module-load request returned · ${String(response).slice(0, 140)} · checking signatures`, '');
      const deadline = Date.now() + 10000;
      while (Date.now() < deadline) {
        await wait(500, context);
        const state = await flyhackReady(context);
        if (state.ready) { flyStatus('SPRX loaded · both runtime signatures verified', 'ok'); toast('Flyhack SPRX ready', 'ok'); return; }
      }
      throw new Error('Module-load response returned, but runtime signatures did not appear within 10 seconds');
    } catch (error) { flyStatus(`SPRX load not confirmed: ${error.message}`, 'err'); toast('Flyhack load not confirmed', 'err'); }
  }
  async function setFlyEnabled(enabled) {
    try {
      const context = newContext();
      await requireFlyhackReady(context);
      await writeHex(0x01F0A00C, enabled ? '01' : '00', context);
      flyStatus(`Flyhack ${enabled ? 'enabled' : 'disabled'} · byte verified`, 'ok');
    } catch (error) { flyStatus(`Flyhack state not confirmed: ${error.message}`, 'err'); }
  }
  function syncFlyRange(field, value) {
    const pairs = { velocity: ['ow-fly-velocity', 'ow-fly-velocity-range'], distance: ['ow-fly-distance', 'ow-fly-distance-range'], deadzone: ['ow-fly-deadzone', 'ow-fly-deadzone-range'] };
    const ids = pairs[field];
    if (!ids) return;
    document.getElementById(ids[0]).value = value;
    document.getElementById(ids[1]).value = value;
  }
  function flySliderPreview(field, value) {
    syncFlyRange(field, value);
    if (flySliderTimer) clearTimeout(flySliderTimer);
    flySliderTimer = setTimeout(() => applyFlySettings().catch(() => {}), 250);
  }
  function flySliderCommit(field) {
    const ids = { velocity: 'ow-fly-velocity-range', distance: 'ow-fly-distance-range', deadzone: 'ow-fly-deadzone-range' };
    if (!ids[field]) return;
    if (flySliderTimer) clearTimeout(flySliderTimer);
    flySliderTimer = null;
    return applyFlySettings();
  }
  async function applyFlySettings() {
    try {
      const context = newContext();
      await requireFlyhackReady(context);
      const velocity = finiteFloat(document.getElementById('ow-fly-velocity').value, 'Velocity');
      const distance = finiteFloat(document.getElementById('ow-fly-distance').value, 'Teleport displacement') * 0.1;
      const deadzone = finiteFloat(document.getElementById('ow-fly-deadzone').value, 'Axial deadzone') * 0.1;
      const mode = document.getElementById('ow-fly-mode').value === '1' ? '01' : '00';
      await writePairs([
        [hexAddress(0x01F0A00D), mode], [hexAddress(0x01F0A000), floatToHex(velocity)],
        [hexAddress(0x01F0A004), floatToHex(distance)], [hexAddress(0x01F0A008), floatToHex(deadzone)]
      ], context);
      flyStatus(`Fly settings verified · ${mode === '01' ? 'teleport' : 'velocity'} mode`, 'ok');
    } catch (error) { flyStatus(`Fly settings not confirmed: ${error.message}`, 'err'); }
  }
  async function internalMode(mode) {
    try {
      const context = newContext();
      await requireFlyhackReady(context);
      const input = finiteFloat(document.getElementById('ow-opk-offset').value, 'OPK offset');
      await writeHex(0x0190EBB7, mode === 'off' ? '00' : '01', context);
      await writeHex(0x0190EBF0, floatToHex(mode === 'void' ? 1e8 : input + 10), context);
      flyStatus(`Internal ${mode} mode bytes verified`, 'ok');
    } catch (error) { flyStatus(`Internal mode failed: ${error.message}`, 'err'); }
  }
  async function toggleWarpriestStart() {
    try {
      const context = newContext();
      const current = await readHex(0x0190EC94, 1, context);
      const next = current === '00' ? '01' : '00';
      await writeHex(0x0190EC94, next, context);
      report(`Warpriest start flag ${next === '01' ? 'enabled' : 'disabled'} · byte verified`, 'ok');
    } catch (error) { report(`Warpriest flag failed: ${error.message}`, 'err'); }
  }

  async function installPlateHook() {
    try {
      const context = newContext();
      requireConnected();
      const branchPair = await readHex(PLATE_BRANCH_ADDRESS, 8, context);
      const currentBranch = branchPair.slice(0, 8);
      if (currentBranch === PLATE_BRANCH_VALUE) throw new Error('The source branch is already present; no owned restoration snapshot is available');
      if (branchPair !== PLATE_BRANCH_ORIGINAL) throw new Error(`Unsupported plate-hook instruction pair ${branchPair} at 0x${hexAddress(PLATE_BRANCH_ADDRESS)}`);
      const previous = readBackup(PLATE_BACKUP_KEY);
      if (previous?.ip === ip) throw new Error('A saved plate-hook snapshot exists. Restore it before installing again.');
      const [body, flag] = await Promise.all([
        readHex(PLATE_ADDRESS, PLATE_BYTES.length / 2, context),
        readHex(PLATE_FLAG_ADDRESS, 4, context)
      ]);
      if (body !== '00'.repeat(PLATE_BYTES.length / 2)) throw new Error('Plate-hook code cave is not empty; no bytes were changed');
      if (flag !== '00000000') throw new Error(`Plate-hook state flag is already in use (${flag}); no bytes were changed`);
      const snapshot = {
        ip, branch: currentBranch,
        body,
        flag
      };
      writeBackup(PLATE_BACKUP_KEY, snapshot);
      await writeHex(PLATE_ADDRESS, PLATE_BYTES, context);
      await writeHex(PLATE_FLAG_ADDRESS, '00000001', context);
      await writeHex(PLATE_BRANCH_ADDRESS, PLATE_BRANCH_VALUE, context);
      report('Overwalls plate hook installed and read back; the in-game effect is not observed', 'ok');
    } catch (error) { report(`Plate hook not confirmed: ${error.message}`, 'err'); }
  }
  async function restorePlateHook() {
    try {
      const context = newContext();
      const snapshot = readBackup(PLATE_BACKUP_KEY);
      if (!snapshot || snapshot.ip !== ip) throw new Error('No plate-hook snapshot exists for this console');
      const [branch, body, flag] = await Promise.all([
        readHex(PLATE_BRANCH_ADDRESS, 4, context),
        readHex(PLATE_ADDRESS, PLATE_BYTES.length / 2, context),
        readHex(PLATE_FLAG_ADDRESS, 4, context)
      ]);
      if (![snapshot.branch, PLATE_BRANCH_VALUE].includes(branch) || ![snapshot.body, PLATE_BYTES].includes(body) ||
          ![snapshot.flag, '00000001'].includes(flag)) throw new Error('Plate-hook bytes changed; saved originals were left untouched');
      if (branch !== snapshot.branch) await writeHex(PLATE_BRANCH_ADDRESS, snapshot.branch, context);
      if (body !== snapshot.body) await writeHex(PLATE_ADDRESS, snapshot.body, context);
      if (flag !== snapshot.flag) await writeHex(PLATE_FLAG_ADDRESS, snapshot.flag, context);
      localStorage.removeItem(PLATE_BACKUP_KEY);
      report('Saved pre-hook bytes restored and verified', 'ok');
    } catch (error) { report(`Plate-hook restore failed: ${error.message}`, 'err'); }
  }
  async function installPhysicsHook() {
    try {
      const context = newContext();
      requireConnected();
      const branch = await readHex(0x001C4110, 4, context);
      if (!['30640140', '48F14DB0'].includes(branch)) throw new Error(`Unsupported hook instruction ${branch} at 0x001C4110`);
      const previous = readBackup(PHYSICS_BACKUP_KEY);
      if (previous?.ip === ip) throw new Error('A saved physics-hook snapshot exists. Restore it before applying again.');
      if (branch === '48F14DB0' && await readHex(PHYSICS_ADDRESS, PHYSICS_BYTES.length / 2, context) === PHYSICS_BYTES) {
        throw new Error('Overwalls physics hook is already active; no owned snapshot is available');
      }
      const snapshot = {
        ip, branch,
        body: await readHex(PHYSICS_ADDRESS, PHYSICS_BYTES.length / 2, context),
        enabled: await readHex(0x0190EBB6, 1, context),
        table: await readHex(0x0190EBC4, 4, context),
        offset: await readHex(0x0190EBF0, 4, context),
        scale: await readHex(0x0190EC04, 4, context)
      };
      writeBackup(PHYSICS_BACKUP_KEY, snapshot);
      if (branch === '48F14DB0') await writeHex(0x001C4110, '30640140', context);
      await writeHex(PHYSICS_ADDRESS, PHYSICS_BYTES, context);
      await writeHex(0x0190EBB6, '01', context);
      await writeHex(0x0190EBC4, '306E8D58', context);
      await writeHex(0x0190EBF0, floatToHex(10), context);
      await writeHex(0x0190EC04, floatToHex(1), context);
      await writeHex(0x001C4110, '48F14DB0', context);
      report('Overwalls physics hook installed and read back; source-build compatibility is not independently known', 'ok');
    } catch (error) { report(`Physics hook not confirmed: ${error.message}`, 'err'); }
  }
  async function restorePhysicsHook() {
    try {
      const context = newContext();
      const snapshot = readBackup(PHYSICS_BACKUP_KEY);
      if (!snapshot || snapshot.ip !== ip) throw new Error('No physics-hook snapshot exists for this console');
      const currentBranch = await readHex(0x001C4110, 4, context);
      const currentBody = await readHex(PHYSICS_ADDRESS, PHYSICS_BYTES.length / 2, context);
      const appPrefix = '3C6001906063EBB0F9C3FFD8F9E3FFD0DA03FFE0DA23FFE87DE0002681C30014C2040140C22E0000FC10884040820028C2040144C22E0004FC10884040820018C2040148C22E0008FC10884040820008908300007DEFF120E9C3FFD8E9E3FFD0CA03FFE0CA23FFE8306401403C80010A4B0EB1E4';
      const appHookBody = appPrefix.padEnd(PHYSICS_BYTES.length, '0');
      const [enabled, table, offset, scale] = await Promise.all([
        readHex(0x0190EBB6, 1, context), readHex(0x0190EBC4, 4, context),
        readHex(0x0190EBF0, 4, context), readHex(0x0190EC04, 4, context)
      ]);
      if (![snapshot.branch, '48F14DB0', '30640140'].includes(currentBranch) ||
          ![snapshot.body, PHYSICS_BYTES, appHookBody].includes(currentBody) ||
          ![snapshot.enabled, '01'].includes(enabled) || ![snapshot.table, '306E8D58'].includes(table) ||
          ![snapshot.offset, floatToHex(10)].includes(offset) || ![snapshot.scale, floatToHex(1)].includes(scale)) {
        throw new Error('Physics-hook bytes changed; saved originals were left untouched');
      }
      if (currentBranch !== '30640140') await writeHex(0x001C4110, '30640140', context);
      if (currentBody !== snapshot.body) await writeHex(PHYSICS_ADDRESS, snapshot.body, context);
      if (enabled !== snapshot.enabled) await writeHex(0x0190EBB6, snapshot.enabled, context);
      if (table !== snapshot.table) await writeHex(0x0190EBC4, snapshot.table, context);
      if (offset !== snapshot.offset) await writeHex(0x0190EBF0, snapshot.offset, context);
      if (scale !== snapshot.scale) await writeHex(0x0190EC04, snapshot.scale, context);
      if (currentBranch !== snapshot.branch) await writeHex(0x001C4110, snapshot.branch, context);
      localStorage.removeItem(PHYSICS_BACKUP_KEY);
      report('Saved pre-hook bytes restored and verified', 'ok');
    } catch (error) { report(`Physics-hook restore failed: ${error.message}`, 'err'); }
  }

  function setActivityInput(value) { document.getElementById('ow-activity-index').value = value; }
  function loadFarmOptions() {
    const teleportMode = localStorage.getItem(TELEPORT_MODE_KEY);
    const activityMethod = localStorage.getItem(ACTIVITY_METHOD_KEY);
    if (teleportMode && document.getElementById('ow-teleport-mode')) document.getElementById('ow-teleport-mode').value = teleportMode;
    if (activityMethod && document.getElementById('ow-activity-method')) document.getElementById('ow-activity-method').value = activityMethod;
    const tower = document.getElementById('ow-tower-shortcuts');
    if (tower) tower.innerHTML = towerPositions.map((entry, index) => `<button class="cheat-btn" type="button" onclick="owTowerGo(${index})"><span class="cheat-name">${html(entry[0])}</span><span class="cheat-desc">X ${entry[1]} · Y ${entry[2]} · Z ${entry[3]}</span></button>`).join('');
    const activities = document.getElementById('ow-activity-shortcuts');
    if (activities) activities.innerHTML = activityPositions.map(entry => `<button class="cheat-btn" type="button" onclick="owActivityLoad('${entry[1]}')"><span class="cheat-name">${html(entry[0])}</span><span class="cheat-desc">${entry[1] === 'randomTower' ? 'Random index 2–7' : `Activity ${entry[1]}`}</span></button>`).join('');
    const toggles = document.getElementById('ow-cheat-controls');
    if (toggles) toggles.innerHTML = Object.entries(cheatNames).map(([id, name]) => `<div class="ow-toggle-card"><strong>${html(name)}</strong><div><button type="button" onclick="owApplyCheatToggle(${id},true)">Enable</button><button type="button" onclick="owApplyCheatToggle(${id},false)">Disable</button></div></div>`).join('');
    renderScenarioLibrary();
    renderScenario();
  }

  Object.assign(root, {
    owSaveOptions: saveOptions, owTowerGo: towerGo, owActivityLoad: activityLoad,
    owLoadActivityFromInput: () => activityLoad(Number(document.getElementById('ow-activity-index').value)),
    owReadActivity: readActivity, owReadCoordinates: readCoordinates, owPDR: pdr, owPDRStaticOff: pdrStaticOff, owAboveYou: aboveYou,
    owSetOPKPreset: setOPKPreset, owTeleportTarget: runTargetTeleport,
    owImportScenario: importScenario, owExportScenario: exportScenario, owLoadScenarioLibrary: loadScenarioLibrary, owAddScenarioPosition: addScenarioPosition,
    owClearScenario: clearScenario, owScenarioGo: scenarioGo, owEditScenario: editScenario, owDeleteScenario: deleteScenario,
    owRunRaid: startRaid, owRunTotems: () => runJob('AFK Totems', runTotems), owRunTotemRoute: runTotemRoute, owStopJob: stopJob, owStartFarm: startFarm,
    owReadCharacterValues: readCharacterValues, owWriteCharacterValues: writeCharacterValues,
    owEnableSplitRep: enableSplitRep, owRestoreSplitRep: restoreSplitRep,
    owRandomizeConsumables: randomizeConsumables, owRestoreConsumables: restoreConsumables,
    owApplyCheatToggle: applyCheatToggle, owApplyCheatPreset: applyCheatPreset, owSetSkipCutscene: setSkipCutscene,
    owStartActivitySweep: startActivitySweep, owReadMemoryDump: readMemoryDump,
    owCheckFlyhack: checkFlyhack, owLoadFlyhack: loadFlyhack,
    owSetFlyEnabled: setFlyEnabled, owApplyFlySettings: applyFlySettings,
    owFlySliderPreview: flySliderPreview, owFlySliderCommit: flySliderCommit,
    owInternalMode: internalMode, owSetWarpriestStart: toggleWarpriestStart,
    owInstallPlateHook: installPlateHook, owRestorePlateHook: restorePlateHook,
    owInstallPhysicsHook: installPhysicsHook, owRestorePhysicsHook: restorePhysicsHook
  });

  document.addEventListener('DOMContentLoaded', () => {
    loadFarmOptions();
    const input = document.getElementById('ow-scenario-file');
    if (input) input.addEventListener('change', () => { if (input.files?.[0]) importScenarioFile(input.files[0]); });
  });
})(globalThis);
