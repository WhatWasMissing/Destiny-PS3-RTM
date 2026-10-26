(function () {
  'use strict';
  function download(name, content) {
    const url = URL.createObjectURL(new Blob([JSON.stringify(content, null, 2)], { type: 'application/json' }));
    const link = document.createElement('a'); link.href = url; link.download = name; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  document.addEventListener('DOMContentLoaded', () => setTimeout(() => {
    const section = document.createElement('section');
    section.className = 'section'; section.id = 'sec-maintenance';
    const panel = document.createElement('div'); panel.className = 'panel';
    const heading = document.createElement('div'); heading.className = 'panel-header'; heading.textContent = 'Backups & diagnostics';
    panel.append(heading);
    const controls = document.createElement('div'); controls.className = 'connect-row'; controls.style.flexWrap = 'wrap';
    function action(label, handler) {
      const button = document.createElement('button'); button.className = 'btn-connect'; button.textContent = label;
      button.onclick = async () => { try { await handler(); } catch (error) { toast(error.message, 'err'); } };
      controls.append(button);
    }
    action('Export settings', () => download('destiny-rtm-settings.json', RtmBackups.capture(localStorage)));
    const input = document.createElement('input'); input.type = 'file'; input.accept = '.json,application/json'; input.hidden = true;
    action('Restore settings', () => input.click());
    input.onchange = async () => {
      try {
        const file = input.files[0]; if (!file) return;
        if (file.size > 5000000) throw new Error('Backup exceeds the 5 MB limit');
        const bundle = RtmBackups.validate(JSON.parse(await file.text()));
        if (!confirm('Replace the settings included in this backup? The page will reload. Console actions will not run.')) return;
        download('destiny-rtm-before-restore.json', RtmBackups.capture(localStorage));
        RtmBackups.restore(localStorage, bundle);
        location.reload();
      } catch (error) { toast(error.message, 'err'); }
      finally { input.value = ''; }
    };
    action('Export diagnostics', () => download('destiny-rtm-diagnostics.json', {
      format: 'destiny-rtm-diagnostics', version: 1, exportedAt: new Date().toISOString(),
      requests: consoleTransport.diagnostics()
    }));
    panel.append(controls, input);
    const note = document.createElement('p'); note.style.cssText = 'padding:0 1rem 1rem;color:var(--muted);font-size:13px';
    note.textContent = 'Back up positions, folders, macros, pins and preferences. Active console states are excluded. Diagnostics omit IP addresses, memory values and notification text.';
    panel.append(note); section.append(panel);
    document.getElementById('sec-custom-offset').before(section);
    RTM_TAB_GROUPS.tools.push(section.id); applyRTMTab();
  }, 0));
})();
