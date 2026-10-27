
  const PINNED_KEY = 'rtm_pinned_cheats';
  let pinnedCheats = [];
  try {
    const savedPins = localStorage.getItem(PINNED_KEY);
    if (savedPins) pinnedCheats = JSON.parse(savedPins);
  } catch (e) { pinnedCheats = []; }

  function savePins() {
    try { localStorage.setItem(PINNED_KEY, JSON.stringify(pinnedCheats)); } catch (e) {}
  }

  function pinSvg(pinned) {
    return pinned
      ? '<svg viewBox="0 0 24 24" width="14" height="14" fill="currentColor"><path d="M16 3l5 5-5 5v3l-4 4-4-4H4l5-5H4l5-5 4-4v3l3-3z"/></svg>'
      : '<svg viewBox="0 0 24 24" width="14" height="14" fill="none" stroke="currentColor" stroke-width="2"><path d="M16 3l5 5-5 5v3l-4 4-4-4H4l5-5H4l5-5 4-4v3l3-3z"/></svg>';
  }

  function togglePin(cheatId) {
    const index = pinnedCheats.indexOf(cheatId);
    const btn = document.getElementById(cheatId);
    if (index > -1) {
      pinnedCheats.splice(index, 1);
      if (btn) btn.classList.remove('is-pinned');
    } else {
      pinnedCheats.push(cheatId);
      if (btn) btn.classList.add('is-pinned');
    }
    savePins();

    // 3. Refresh UI
    refreshPinTriggers();
    renderPinnedCheats();
    toast(pinnedCheats.includes(cheatId) ? 'Cheat Pinned' : 'Cheat Unpinned', 'ok');
  }

  // Add a pin trigger to every pinnable cheat button (those with an id)
  function injectPinTriggers() {
    document.querySelectorAll('.cheat-btn[id]').forEach(btn => {
      if (btn.querySelector('.pin-trigger')) return; // already has one
      if (pinnedCheats.includes(btn.id)) btn.classList.add('is-pinned');

      const trigger = document.createElement('div');
      trigger.className = 'pin-trigger';
      trigger.innerHTML = pinSvg(pinnedCheats.includes(btn.id));
      trigger.addEventListener('click', (e) => {
        e.stopPropagation();
        togglePin(btn.id);
      });
      btn.appendChild(trigger);
    });
  }

  // Sync the pin icon on original buttons (and pinned clones)
  function refreshPinTriggers() {
    document.querySelectorAll('.cheat-btn[id]').forEach(btn => {
      const trigger = btn.querySelector('.pin-trigger');
      const pinned = pinnedCheats.includes(btn.id);
      btn.classList.toggle('is-pinned', pinned);
      if (trigger) trigger.innerHTML = pinSvg(pinned);
    });
  }

  // Build the pinned grid by cloning original buttons; clicking a clone
  // forwards the click to the real button so all state/logic stays in sync.
  function renderPinnedCheats() {
    const container = document.getElementById('pinned-grid');
    const quickAccessSection = document.getElementById('sec-quick-access');
    if (!container || !quickAccessSection) return;

    // Quick Access now contains BOTH Pinned Cheats and Macros.
    // Never hide the whole section just because the pinned list is empty,
    // otherwise the Macro controls disappear as well.
    quickAccessSection.style.display = '';
    container.innerHTML = '';

    if (pinnedCheats.length === 0) {
      container.innerHTML = '<div class="pinned-empty-state">No pinned cheats yet.</div>';
      if (typeof filterCheats === 'function') filterCheats();
      return;
    }

    pinnedCheats.forEach(id => {
      const original = document.getElementById(id);
      if (!original) return;

      const clone = original.cloneNode(true);
      clone.removeAttribute('id');
      clone.removeAttribute('onclick');
      clone.classList.add('is-pinned');
      clone.classList.toggle('active-cheat', original.classList.contains('active-cheat'));

      // Forward taps to the real button so existing handlers/state run normally
      clone.addEventListener('click', (e) => {
        if (e.target.closest('.pin-trigger')) return; // handled separately below
        original.click();
      });

      // The clone's pin trigger acts as an "unpin" control
      const trigger = clone.querySelector('.pin-trigger');
      if (trigger) {
        trigger.innerHTML = pinSvg(true);
        trigger.addEventListener('click', (e) => {
          e.stopPropagation();
          togglePin(id);
        });
      }

      container.appendChild(clone);
    });

    if (typeof filterCheats === 'function') filterCheats();
  }

  // Keep pinned clones visually in sync (e.g. active-cheat highlight) when
  // the original buttons change state.
  function observeCheatStates() {
    const grid = document.getElementById('pinned-grid');
    const activeStates = new WeakMap();
    const observer = new MutationObserver(mutations => {
      const changedButtons = new Set(mutations.map(mutation => mutation.target));
      changedButtons.forEach(original => {
        const active = original.classList.contains('active-cheat');
        const previous = activeStates.get(original);
        activeStates.set(original, active);
        if (previous !== undefined && previous !== active && typeof automationNotify === 'function') {
          const label = getCheatLabel(original);
          automationNotify(`Cheat ${active ? 'activated' : 'deactivated'}: ${label}`, 'cheat', {cheat:label, ip});
        }

        const index = pinnedCheats.indexOf(original.id);
        const clone = grid && index >= 0 ? grid.children[index] : null;
        if (clone) clone.classList.toggle('active-cheat', active);
      });
    });
    document.querySelectorAll('.cheat-btn[id]').forEach(btn => {
      activeStates.set(btn, btn.classList.contains('active-cheat'));
      observer.observe(btn, { attributes: true, attributeFilter: ['class'] });
    });
  }

  document.addEventListener('DOMContentLoaded', () => {
    injectPinTriggers();
    renderPinnedCheats();
    observeCheatStates();
  });

  // ── Cheat Search ─────────────────────────────────────────────────────────
  function filterCheats() {
    const query = document.getElementById('cheat-search').value.trim().toLowerCase();
    const cheatButtons = document.querySelectorAll('.cheat-btn');

    cheatButtons.forEach(btn => {
      const text = btn.textContent.toLowerCase();
      const matches = !query || text.includes(query);
      btn.classList.toggle('search-hidden', !matches);
    });

    // Hide whole sections if every cheat-btn inside them is hidden by search
    document.querySelectorAll('.section').forEach(sec => {
      const btns = sec.querySelectorAll('.cheat-btn');
      if (btns.length === 0) return; // leave non-cheat sections alone

      const anyVisible = Array.from(btns).some(b => !b.classList.contains('search-hidden'));

      // Quick Access contains macro controls as well as cheat buttons.
      // Keep it available unless an active search genuinely filters its cheat content.
      if (sec.id === 'sec-quick-access' && !query) {
        sec.classList.remove('search-hidden-section');
      } else {
        sec.classList.toggle('search-hidden-section', !anyVisible);
      }
    });
  }

  // ── Macros ───────────────────────────────────────────────────────────────
  let macros = [];
  try {
    macros = JSON.parse(localStorage.getItem('macros') || '[]');
  } catch (e) {
    macros = [];
  }

  function saveMacros() {
    try {
      localStorage.setItem('macros', JSON.stringify(macros));
    } catch (e) {}
  }

  function getCheatLabel(btn) {
    // First non-empty text node is the cheat's display name
    for (const node of btn.childNodes) {
      if (node.nodeType === Node.TEXT_NODE) {
        const t = node.textContent.trim();
        if (t) return t;
      }
    }
    return btn.id;
  }

  function openMacroModal() {
    const list = document.getElementById('macro-cheat-list');
    list.innerHTML = '';

    document.querySelectorAll('.cheat-btn[id]').forEach(btn => {
      const label = getCheatLabel(btn);
      const row = document.createElement('label');
      row.className = 'macro-cheat-row';

      const cb = document.createElement('input');
      cb.type = 'checkbox';
      cb.className = 'macro-cheat-checkbox';
      cb.value = btn.id;

      const span = document.createElement('span');
      span.textContent = label;

      row.appendChild(cb);
      row.appendChild(span);
      list.appendChild(row);
    });

    document.getElementById('macro-name-input').value = '';
    const overlay = document.getElementById('macro-overlay');
    overlay.style.display = 'flex';
    document.getElementById('macro-name-input').focus();
  }

  function closeMacroModal() {
    document.getElementById('macro-overlay').style.display = 'none';
  }

  function closeMacroModalOutside(e) {
    if (e.target === document.getElementById('macro-overlay')) closeMacroModal();
  }

  function saveMacro() {
    const name = document.getElementById('macro-name-input').value.trim();
    if (!name) {
      toast('Enter a macro name', 'err');
      return;
    }

    const cheatIds = Array.from(document.querySelectorAll('#macro-cheat-list input:checked'))
      .map(cb => cb.value);

    if (cheatIds.length === 0) {
      toast('Select at least one cheat', 'err');
      return;
    }

    macros.push({ name, cheatIds });
    saveMacros();
    renderMacros();
    closeMacroModal();
    toast(`Macro '${name}' saved`, 'ok');
  }

  function deleteMacro(index) {
    const m = macros[index];
    macros.splice(index, 1);
    saveMacros();
    renderMacros();
    if (m) toast(`Macro '${m.name}' deleted`, 'ok');
  }

  async function runMacro(index) {
    const macro = macros[index];
    if (!macro) return;

    log(`Running macro: ${macro.name}…`, 'ok');

    for (const id of macro.cheatIds) {
      const btn = document.getElementById(id);
      if (btn) {
        // Trigger the cheat exactly as if the user tapped it
        btn.click();

        // Slight delay between commands to prevent network flooding
        await sleep(WRITE_DELAY);
      }
    }
    toast(`Macro '${macro.name}' executed`, 'ok');
  }

  function renderMacros() {
    const grid = document.getElementById('macro-grid');
    if (!grid) return;

    // Remove existing macro buttons (keep the "+ Create Macro" button)
    grid.querySelectorAll('.macro-btn').forEach(el => el.remove());

    macros.forEach((macro, i) => {
      const btn = document.createElement('button');
      btn.className = 'cheat-btn macro-btn';
      btn.style.position = 'relative';
      btn.style.paddingRight = '40px';
      btn.innerHTML = '<span>' + tpEscape(macro.name) + '</span><span class="addr">' + macro.cheatIds.length + ' step' + (macro.cheatIds.length === 1 ? '' : 's') + '</span>';
      btn.addEventListener('click', (e) => {
        if (e.target.closest('.pin-trigger')) return;
        runMacro(i);
      });

      const del = document.createElement('div');
      del.className = 'pin-trigger';
      del.innerHTML = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 6h18M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2m2 0v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6h14z"/></svg>';
      del.title = 'Delete macro';
      del.addEventListener('click', (e) => {
        e.stopPropagation();
        deleteMacro(i);
      });
      btn.appendChild(del);

      grid.appendChild(btn);
    });
  }

  document.addEventListener('DOMContentLoaded', renderMacros);

  // ── Editable cheat addresses ──────────────────────────────────────────────
  function loadSavedAddresses() {
    let savedAddrs = {};
    try {
      savedAddrs = JSON.parse(localStorage.getItem('cheatAddresses') || '{}');
    } catch (e) {
      savedAddrs = {};
    }
    for (const [id, addr] of Object.entries(savedAddrs)) {
      const el = document.getElementById(`addr-${id}`);
      if (el) el.innerText = addr;
    }
  }

  function saveAddress(cheatId, newAddr) {
    const addr = newAddr.trim().toUpperCase();
    if (!/^(?:0X)?[0-9A-F]{1,8}$/.test(addr)) { toast('Enter a valid hexadecimal memory address', 'err'); return; }
    let savedAddrs = {};
    try {
      savedAddrs = JSON.parse(localStorage.getItem('cheatAddresses') || '{}');
    } catch (e) {
      savedAddrs = {};
    }
    savedAddrs[cheatId] = addr;
    try {
      localStorage.setItem('cheatAddresses', JSON.stringify(savedAddrs));
    } catch (e) { toast('Address could not be saved', 'err'); return; }

    const el = document.getElementById(`addr-${cheatId}`);
    if (el) el.innerText = addr;

    toast('Address Saved', 'ok');
  }

  // Trigger a cheat whose memory address is user-editable.
  // Uses the existing togFireFloat(addr, onFloat, offFloat, label, btn) implementation
  // so it gets full ip/state/log/toast/HEN-delay handling for free.
  function triggerDynamicCheat(cheatId) {
    const addr = document.getElementById(`addr-${cheatId}`).innerText.trim();
    const btn = document.getElementById(cheatId);
    togFire(addr, '3F800000', '00000000', 'Unlimited Sword Energy', btn);
  }

  document.addEventListener('DOMContentLoaded', loadSavedAddresses);
