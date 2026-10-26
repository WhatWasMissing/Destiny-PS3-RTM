(function (root) {
  'use strict';
  const keys = new Set(['rtm_saved_ip', 'rtm_saved_teleports', 'rtm_teleport_folders',
    'rtm_pinned_cheats', 'macros', 'cheatAddresses', 'henMode', 'rtm_accent_color',
    'rtm_appearance', 'rtm_auto_reconnect_safe_v1', 'rtm_active_tab', 'rtm_ui_mode']);
  function allowed(key) { return keys.has(key) || /^destinyRtmSectionVisible_sec-[a-z0-9-]+$/.test(key); }
  function validate(bundle) {
    if (!bundle || bundle.format !== 'destiny-rtm-settings' || bundle.version !== 1 ||
        !bundle.settings || typeof bundle.settings !== 'object' || Array.isArray(bundle.settings)) {
      throw new Error('Not a supported Destiny RTM settings backup');
    }
    const entries = Object.entries(bundle.settings);
    if (entries.length > 100) throw new Error('Too many settings');
    for (const [key, value] of entries) {
      if (!allowed(key) || typeof value !== 'string' || value.length > 1000000) throw new Error('Invalid setting: ' + key);
      if (['rtm_saved_teleports', 'rtm_teleport_folders', 'rtm_pinned_cheats', 'macros'].includes(key)) {
        const data = JSON.parse(value);
        if (!Array.isArray(data) || data.length > 10000) throw new Error('Invalid list: ' + key);
        if (key === 'rtm_saved_teleports') data.forEach(position => {
          if (!position || typeof position.name !== 'string' || position.name.length > 200 ||
              !['x','y','z'].every(axis => String(position[axis]).trim() !== '' && Number.isFinite(Number(position[axis])))) {
            throw new Error('Invalid teleport coordinates');
          }
        });
        if (key === 'rtm_teleport_folders') data.forEach(folder => {
          if (!folder || !/^[a-zA-Z0-9_-]{1,100}$/.test(folder.id) || typeof folder.name !== 'string') throw new Error('Invalid teleport folder');
        });
        if (key === 'rtm_pinned_cheats' && !data.every(id => typeof id === 'string' && /^[a-zA-Z0-9_-]{1,100}$/.test(id))) throw new Error('Invalid pinned control');
        if (key === 'macros') data.forEach(macro => {
          if (!macro || typeof macro.name !== 'string' || macro.name.length > 200 ||
              !Array.isArray(macro.cheatIds) || macro.cheatIds.length > 500 ||
              !macro.cheatIds.every(id => typeof id === 'string' && /^[a-zA-Z0-9_-]{1,100}$/.test(id))) throw new Error('Invalid macro');
        });
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
  root.RtmBackups = { capture, validate, restore };
  if (typeof module !== 'undefined') module.exports = root.RtmBackups;
})(globalThis);
