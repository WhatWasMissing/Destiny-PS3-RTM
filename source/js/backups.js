(function (root) {
  'use strict';
  const keys = new Set(['rtm_saved_ip', 'rtm_saved_teleports', 'rtm_teleport_folders',
    'overwalls_scenario_positions', 'overwalls_teleport_mode', 'overwalls_activity_method',
    'rtm_pinned_cheats', 'macros', 'cheatAddresses', 'henMode', 'rtm_accent_color',
    'rtm_appearance', 'rtm_auto_reconnect_safe_v1', 'rtm_active_tab', 'rtm_ui_mode',
    'destinyRtmToolSetupV1', 'destinyRtmCharacterUtilsVisibleV1', 'rtm_launch_iso_path']);
  function allowed(key) { return keys.has(key) || /^destinyRtmSectionVisible_sec-[a-z0-9-]+$/.test(key); }
  const coordinate = value => (typeof value === 'number' || typeof value === 'string' && value.trim() !== '') &&
    Number.isFinite(Number(value)) && Math.abs(Number(value)) <= 3.4028234663852886e38;
  function validate(bundle) {
    if (!bundle || bundle.format !== 'destiny-rtm-settings' || bundle.version !== 1 ||
        !bundle.settings || typeof bundle.settings !== 'object' || Array.isArray(bundle.settings)) {
      throw new Error('Not a supported Destiny RTM settings backup');
    }
    const entries = Object.entries(bundle.settings);
    if (entries.length > 100) throw new Error('Too many settings');
    for (const [key, value] of entries) {
      if (!allowed(key) || typeof value !== 'string' || value.length > 1000000) throw new Error('Invalid setting: ' + key);
      if (key === 'rtm_saved_ip' && value !== '' && (!/^(?:\d{1,3}\.){3}\d{1,3}$/.test(value) || value.split('.').some(part => Number(part) > 255))) throw new Error('Invalid saved console IP');
      if ((key === 'henMode' || key === 'rtm_auto_reconnect_safe_v1' || key === 'destinyRtmToolSetupV1' || key === 'destinyRtmCharacterUtilsVisibleV1' || key.startsWith('destinyRtmSectionVisible_')) && !['0','1'].includes(value)) throw new Error('Invalid switch setting');
      if (key === 'rtm_appearance' && !['light','dark'].includes(value)) throw new Error('Invalid appearance');
      if (key === 'rtm_ui_mode' && !['compact','detailed'].includes(value)) throw new Error('Invalid UI mode');
      if (key === 'rtm_launch_iso_path' && (value.length > 160 || value.includes('..') || !/^\/dev_hdd0\/PS3ISO\/[A-Za-z0-9][A-Za-z0-9 _().-]{0,124}\.iso$/i.test(value))) throw new Error('Invalid Destiny ISO path');
      if (key === 'rtm_active_tab' && !['home','mods','player','overwalls','tools','all'].includes(value)) throw new Error('Invalid tab');
      if (key === 'rtm_accent_color' && !/^#[0-9a-f]{6}$/i.test(value)) throw new Error('Invalid accent color');
      if (key === 'cheatAddresses') {
        const addresses = JSON.parse(value);
        if (!addresses || typeof addresses !== 'object' || Array.isArray(addresses) || Object.keys(addresses).length > 500 ||
            !Object.entries(addresses).every(([id, address]) => /^[a-zA-Z0-9_-]{1,100}$/.test(id) && typeof address === 'string' && /^(?:0x)?[0-9a-f]{1,8}$/i.test(address))) throw new Error('Invalid custom address mapping');
      }
      if (['rtm_saved_teleports', 'rtm_teleport_folders', 'rtm_pinned_cheats', 'macros'].includes(key)) {
        const data = JSON.parse(value);
        if (!Array.isArray(data) || data.length > 10000) throw new Error('Invalid list: ' + key);
        if (key === 'rtm_saved_teleports') data.forEach(position => {
          if (!position || typeof position.name !== 'string' || !position.name.trim() || position.name.length > 200 ||
              !['x','y','z'].every(axis => coordinate(position[axis]))) {
            throw new Error('Invalid teleport coordinates');
          }
        });
        if (key === 'rtm_teleport_folders') data.forEach(folder => {
          if (!folder || !/^[a-zA-Z0-9_-]{1,100}$/.test(folder.id) || typeof folder.name !== 'string' || !folder.name.trim() || folder.name.length > 200 ||
              (folder.system !== undefined && typeof folder.system !== 'boolean') || (folder.open !== undefined && typeof folder.open !== 'boolean')) throw new Error('Invalid teleport folder');
        });
        if (key === 'rtm_teleport_folders' && new Set(data.map(folder => folder.id)).size !== data.length) throw new Error('Duplicate teleport folder IDs');
        if (key === 'rtm_pinned_cheats' && !data.every(id => typeof id === 'string' && /^[a-zA-Z0-9_-]{1,100}$/.test(id))) throw new Error('Invalid pinned control');
        if (key === 'macros') data.forEach(macro => {
          if (!macro || typeof macro.name !== 'string' || macro.name.length > 200 ||
              !Array.isArray(macro.cheatIds) || macro.cheatIds.length > 500 ||
              !macro.cheatIds.every(id => typeof id === 'string' && /^[a-zA-Z0-9_-]{1,100}$/.test(id))) throw new Error('Invalid macro');
        });
      }
      if (key === 'overwalls_teleport_mode' && !['external','internal','position'].includes(value)) throw new Error('Invalid Overwalls teleport mode');
      if (key === 'overwalls_activity_method' && !['current','overwalls'].includes(value)) throw new Error('Invalid Overwalls activity method');
      if (key === 'overwalls_scenario_positions') {
        const data = JSON.parse(value);
        if (!Array.isArray(data) || data.length > 500 || !data.every(item => item &&
            typeof item.id === 'string' && item.id.length <= 100 &&
            typeof item.name === 'string' && item.name.trim() && item.name.length <= 200 &&
            ['x','y','z'].every(axis => coordinate(item[axis])) &&
            (item.legacyName === undefined || typeof item.legacyName === 'string' && item.legacyName.length <= 100))) {
          throw new Error('Invalid Overwalls scenario positions');
        }
      }
    }
    return bundle;
  }
  function capture(storage) {
    const settings = Object.create(null);
    for (let i = 0; i < storage.length; i++) {
      const key = storage.key(i);
      if (allowed(key)) settings[key] = storage.getItem(key);
    }
    return validate({ format: 'destiny-rtm-settings', version: 1, createdAt: new Date().toISOString(), settings });
  }
  function restore(storage, bundle) {
    validate(bundle);
    const previous = Object.entries(bundle.settings).map(([key]) => [key, storage.getItem(key)]);
    try { for (const [key, value] of Object.entries(bundle.settings)) storage.setItem(key, value); }
    catch (error) {
      for (const [key, value] of previous) {
        try { if (value === null) storage.removeItem(key); else storage.setItem(key, value); } catch (_) {}
      }
      throw new Error('Restore failed; previous settings were restored where storage permits', { cause: error });
    }
  }
  root.RtmBackups = { capture, validate, restore, coordinate };
  if (typeof module !== 'undefined') module.exports = root.RtmBackups;
})(globalThis);
