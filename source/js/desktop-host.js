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
    }
  };
})(globalThis);
