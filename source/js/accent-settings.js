
  // ── Accent colour ───────────────────────────────────────────────────────
  (function() {
    const root = document.documentElement;
    function setAccent(color) {
      if (!/^#[0-9a-fA-F]{6}$/.test(color)) return;
      root.style.setProperty('--accent', color);
      try { localStorage.setItem('rtm_accent_color', color); } catch (e) {}
      const picker = document.getElementById('settings-accent');
      if (picker) picker.value = color;
    }
    window.setAccent = setAccent;
    try {
      const saved = localStorage.getItem('rtm_accent_color');
      if (saved) setAccent(saved);
    } catch (e) {}
  })();
