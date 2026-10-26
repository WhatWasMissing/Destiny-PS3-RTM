(function (root) {
  'use strict';
  function createRunner({ session, write, read }) {
    let tail = Promise.resolve();
    function apply(pairs) {
      const owner = session();
      const normalized = pairs.map(([address, value]) => {
        const addr = String(address).replace(/^0x/i, '');
        const hex = String(value).replace(/\s/g, '').toUpperCase();
        if (!/^[0-9a-f]{1,8}$/i.test(addr) || !/^(?:[0-9A-F]{2})+$/.test(hex)) throw new Error('Invalid memory patch');
        return [parseInt(addr, 16), hex];
      });
      if (!normalized.length) return Promise.reject(new Error('Empty memory patch'));
      const check = () => { if (!owner || owner !== session()) throw new Error('Console session changed; operation cancelled'); };
      const run = async () => {
        let completed = 0;
        try {
          for (const [address, value] of normalized) {
            check(); await write(address, value); check(); completed++;
          }
        } catch (cause) {
          throw new Error(`Patch incomplete (${completed}/${normalized.length} writes acknowledged): ${cause.message}`, { cause });
        }
        for (const [address, expected] of normalized) {
          check();
          let actual;
          try { actual = await read(address, expected.length / 2); }
          catch (error) { check(); return { status: 'unverified', writes: completed, reason: error.message }; }
          check();
          if (String(actual).replace(/\s/g, '').toUpperCase() !== expected) {
            throw new Error(`Readback mismatch after ${completed} writes; console state is unknown`);
          }
        }
        return { status: 'verified', writes: completed };
      };
      const result = tail.then(run);
      tail = result.catch(() => {});
      return result;
    }
    return { apply };
  }
  root.RtmOperations = { createRunner };
  if (typeof module !== 'undefined') module.exports = root.RtmOperations;
})(globalThis);
