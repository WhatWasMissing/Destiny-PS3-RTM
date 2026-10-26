(function (root) {
  'use strict';

  const PROFILE_NAME = 'BLES01857 01.24 signature profile';
  const DEFAULT_CACHE_MS = 5000;
  const SIGNATURES = Object.freeze([
    Object.freeze({
      address: '00444B64',
      hex: '3C600044C03601283CC03F803B4349EC3061034462C4000063850000C05A0000'
    }),
    Object.freeze({
      address: '001FB3C8',
      hex: '8084001C908300004E800020'
    }),
    Object.freeze({
      address: '002FF2D0',
      hex: '7C0802A648A99751F821FF81F8010090'
    })
  ]);

  function asHex(value) {
    if (typeof value === 'string') return value.replace(/\s/g, '').toUpperCase();
    if (value instanceof ArrayBuffer) value = new Uint8Array(value);
    if (ArrayBuffer.isView(value)) {
      return Array.from(new Uint8Array(value.buffer, value.byteOffset, value.byteLength),
        byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
    }
    throw new Error('Build signature read returned an unsupported value');
  }

  function create(options = {}) {
    if (typeof options.read !== 'function' || typeof options.session !== 'function') {
      throw new TypeError('Build compatibility requires session and read functions');
    }
    const now = typeof options.now === 'function' ? options.now : Date.now;
    const cacheMs = Number.isFinite(options.cacheMs) && options.cacheMs >= 0
      ? options.cacheMs : DEFAULT_CACHE_MS;
    let verifiedSession = '';
    let verifiedUntil = 0;
    let pendingSession = '';
    let pending = null;

    async function ensure(patches = []) {
      const owner = String(options.session() || '');
      if (!owner) throw new Error('Connect to a console before writing memory');
      for (const patch of patches) {
        const address = Number(patch[0]);
        const length = String(patch[1]).replace(/\s/g, '').length / 2;
        const end = address + length;
        for (const signature of SIGNATURES) {
          const start = parseInt(signature.address, 16);
          const signatureEnd = start + signature.hex.length / 2;
          if (address < signatureEnd && start < end) {
            throw new Error(`Write overlaps protected 01.24 code signature at 0x${signature.address}; preserving the build check`);
          }
        }
      }
      if (owner === verifiedSession && now() < verifiedUntil) {
        return { status: 'verified', profile: PROFILE_NAME, cached: true };
      }
      if (pending && pendingSession === owner) return pending;

      const current = (async () => {
        for (const signature of SIGNATURES) {
          if (String(options.session() || '') !== owner) {
            throw new Error('Console session changed during build verification');
          }
          let observed;
          try {
            observed = asHex(await options.read(signature.address, signature.hex.length / 2));
          } catch (error) {
            throw new Error(`Cannot verify Destiny 01.24 at 0x${signature.address}: ${error.message || error}`);
          }
          if (observed !== signature.hex) {
            throw new Error(`Unsupported Destiny build: 01.24 signature mismatch at 0x${signature.address}; memory write refused`);
          }
        }
        if (String(options.session() || '') !== owner) {
          throw new Error('Console session changed during build verification');
        }
        verifiedSession = owner;
        verifiedUntil = now() + cacheMs;
        return { status: 'verified', profile: PROFILE_NAME, cached: false };
      })();

      pendingSession = owner;
      pending = current;
      try {
        return await current;
      } finally {
        if (pending === current) {
          pending = null;
          pendingSession = '';
        }
      }
    }

    function reset() {
      verifiedSession = '';
      verifiedUntil = 0;
    }

    return { ensure, reset };
  }

  root.RtmBuildCompatibility = { create, signatures: SIGNATURES, profileName: PROFILE_NAME };
  if (typeof module !== 'undefined') module.exports = root.RtmBuildCompatibility;
})(globalThis);
