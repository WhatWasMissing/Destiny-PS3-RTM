(function (root) {
  'use strict';
  function createTransport(options = {}) {
    const fetchRequest = options.fetch || root.fetch.bind(root);
    const pending = new Set();
    const history = [];
    async function request(input, timeoutMs = 8000) {
      const url = new URL(input);
      if (!['http:', 'https:'].includes(url.protocol)) throw new Error('Unsupported console protocol');
      if (url.pathname.endsWith('.ps3mapi') &&
          /^\/(setmem|getmem|call)\./.test(url.pathname) && !url.searchParams.has('proc')) {
        url.searchParams.set('proc', options.processId || '0x1010200');
      }
      if (!Number.isFinite(timeoutMs) || timeoutMs <= 0) throw new Error('Invalid request timeout');
      const controller = new AbortController();
      pending.add(controller);
      let timedOut = false;
      const timer = setTimeout(() => { timedOut = true; controller.abort(); }, timeoutMs);
      const entry = { time: new Date().toISOString(), operation: url.pathname, outcome: 'pending' };
      history.push(entry);
      if (history.length > 200) history.shift();
      try {
        const response = await fetchRequest(url.href, { signal: controller.signal, cache: 'no-store' });
        if (response.type === 'opaque') throw new Error('Console response cannot be verified');
        if (!response.ok) throw new Error(`Console returned HTTP ${response.status}`);
        entry.outcome = 'http-accepted';
        return response;
      } catch (error) {
        entry.outcome = timedOut ? 'timeout' : controller.signal.aborted ? 'cancelled' : 'failed';
        entry.error = timedOut ? `Console request timed out after ${timeoutMs} ms` : String(error.message || error);
        throw new Error(entry.error, { cause: error });
      } finally {
        clearTimeout(timer);
        pending.delete(controller);
      }
    }
    return {
      request,
      cancelAll() { for (const controller of pending) controller.abort(); },
      diagnostics() { return history.map(({ time, operation, outcome }) => ({ time, operation, outcome })); }
    };
  }
  root.RtmTransport = { createTransport };
  if (typeof module !== 'undefined') module.exports = { createTransport };
})(globalThis);
