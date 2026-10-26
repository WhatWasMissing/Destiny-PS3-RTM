(function (root) {
  'use strict';
  const nativeFetch = root.fetch.bind(root);
  let config;
  async function hostConfig() {
    if (!config) config = (async () => {
      if (!['127.0.0.1', 'localhost', '[::1]'].includes(location.hostname)) return null;
      try {
        const response = await nativeFetch('/host-config', { cache: 'no-store', signal: AbortSignal.timeout(2000) });
        if (!response.ok || !response.headers.get('content-type')?.includes('application/json')) return null;
        const data = await response.json();
        return data.host === 'destiny-rtm' && typeof data.token === 'string' ? data : null;
      } catch (_) { return null; }
    })();
    return config;
  }
  root.RtmHost = {
    async fetch(input, options = {}) {
      const host = await hostConfig();
      if (options.signal?.aborted) throw new DOMException('Request cancelled', 'AbortError');
      if (!host) return nativeFetch(input, options);
      return nativeFetch('/console?url=' + encodeURIComponent(input), {
        ...options, headers: { 'X-RTM-Token': host.token }
      });
    },
    async loadModule(target, pid, path) {
      if (path !== '/dev_hdd0/tmp/Destiny_Flyhack.sprx' || !/^\d{1,10}$/.test(String(pid))) {
        throw new Error('Unsupported module request');
      }
      const host = await hostConfig();
      const command = `MODULE LOAD ${String(pid)} ${path}`;
      const response = host
        ? await nativeFetch('/module-load', { method: 'POST', headers: {
            'Content-Type': 'application/json', 'X-RTM-Token': host.token
          }, body: JSON.stringify({ target: String(target), pid: String(pid), path }) })
        : location.hostname === String(target)
          ? await nativeFetch(`/ps3mapi.ps3?${encodeURIComponent(command)}`, { cache: 'no-store' })
          : null;
      if (!response) throw new Error('Module loading requires the desktop host or an RTM page served by the target PS3');
      const body = await response.text();
      if (!response.ok) throw new Error(`Module-load request returned HTTP ${response.status}`);
      return body;
    }
  };
})(globalThis);
