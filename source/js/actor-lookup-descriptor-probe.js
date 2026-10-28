// Read-only BLES 01.24 actor-lookup descriptor probe. It never follows the
// storage pointer or enumerates actor records.
(function (root) {
  'use strict';

  const CODE_ADDRESS = 0x00255FA8;
  const CODE_SIGNATURE = '7C0802A648B42A5DF821FF21F80100F0607E00003C6001A23FE001A23063E16033FF1138809E01743BA00001548404FE80A3000C7C8429D6806300087F432014';
  const DESCRIPTOR_ADDRESS = 0x01A1E160;
  const DESCRIPTOR_BYTES = 0x14;
  const EXPECTED_CAPACITY = 40;
  const EXPECTED_STRIDE = 0x28D0;
  const HEX_64 = /^(?:[0-9A-F]{2}){64}$/;
  let busy = false;

  function hexValue(value) {
    return `0x${(value >>> 0).toString(16).toUpperCase().padStart(8, '0')}`;
  }

  function parseDescriptor(hex) {
    const clean = String(hex || '').replace(/\s/g, '').toUpperCase();
    if (!/^(?:[0-9A-F]{2}){20}$/.test(clean)) throw new Error('Short or invalid actor-pool descriptor read.');
    const bytes = new Uint8Array(DESCRIPTOR_BYTES);
    for (let index = 0; index < bytes.length; index++) bytes[index] = parseInt(clean.slice(index * 2, index * 2 + 2), 16);
    const view = new DataView(bytes.buffer);
    return {
      raw: clean,
      word0: view.getUint32(0, false),
      word4: view.getUint32(4, false),
      storage: view.getUint32(8, false),
      stride: view.getUint32(12, false),
      capacity: view.getUint32(16, false)
    };
  }

  function render(output, before, after, signatureMatch) {
    const pointerOK = after.storage === 0 ||
      (after.storage >= 0x10000 && after.storage < 0x40000000 && (after.storage & 0x03) === 0);
    const strideOK = after.stride === 0 || after.stride === EXPECTED_STRIDE;
    const capacityOK = after.capacity === 0 || after.capacity === EXPECTED_CAPACITY;
    const initialized = after.storage !== 0;
    const spanBytes = after.capacity * after.stride;
    const spanEnd = after.storage + spanBytes;
    const spanOK = !initialized || (
      after.stride === EXPECTED_STRIDE && after.capacity === EXPECTED_CAPACITY &&
      spanEnd > after.storage && spanEnd <= 0x40000000
    );
    const lines = [
      `BLES code: MATCH · ${hexValue(CODE_ADDRESS)} · ${CODE_SIGNATURE.length / 2} bytes`,
      `Descriptor: ${hexValue(DESCRIPTOR_ADDRESS)} · 20 bytes`,
      `Word +0: ${hexValue(after.word0)} (raw)`,
      `Word +4: ${hexValue(after.word4)} (raw)`,
      `Storage pointer +8: ${hexValue(after.storage)}${after.storage === 0 ? ' (null)' : pointerOK ? ' (bounded; not followed)' : ' (outside bounded range; not followed)'}`,
      `Record stride +0xC: ${hexValue(after.stride)}${after.stride === 0 ? ' (zero)' : strideOK ? ' (matches BLES initializer)' : ' (unexpected for BLES 01.24)'}`,
      `Capacity +0x10: ${after.capacity}${capacityOK ? after.capacity === EXPECTED_CAPACITY ? ' (matches BLES initializer)' : ' (zero)' : ' (unexpected for BLES 01.24)'}`,
      `Expected backing span: ${hexValue(spanBytes)} bytes${spanOK ? '' : ' (out of bounds; not followed)'}`,
      'Storage records read: 0',
      'Record semantics, loaded-enemy correlation, and spawn behavior: unresolved'
    ];
    output.textContent = lines.join('\n');
    output.hidden = false;
    if (!signatureMatch || before.raw !== after.raw) throw new Error('The actor descriptor or code signature changed during the sample.');
    if (!pointerOK) throw new Error('The storage pointer is outside the bounded user-memory range; it was not followed.');
    if (!strideOK) throw new Error('The descriptor stride does not match the BLES 01.24 actor-pool initializer.');
    if (!capacityOK) throw new Error('The descriptor capacity does not match the BLES 01.24 actor-pool initializer.');
    if (!spanOK) throw new Error('The descriptor backing span exceeds the bounded user-memory range; it was not followed.');
  }

  async function inspectActorLookupDescriptor() {
    const button = document.getElementById('actor-lookup-descriptor-read-btn');
    const clearButton = document.getElementById('actor-lookup-descriptor-clear-btn');
    const status = document.getElementById('actor-lookup-descriptor-status');
    const output = document.getElementById('actor-lookup-descriptor-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    if (clearButton) clearButton.disabled = true;
    output.textContent = '';
    output.hidden = true;
    status.dataset.state = 'pending';
    status.textContent = 'Checking the BLES profile, actor-path code, and 20-byte actor-pool descriptor…';

    try {
      if (!api) throw new Error('The session-bound read-only memory API is unavailable.');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before reading the actor-lookup descriptor.');

      const initialProfile = await api.verify(owner);
      if (initialProfile.profile !== 'BLES01857 01.24 signature profile' || initialProfile.anchors !== root.RtmBuildCompatibility.anchorCount) {
        throw new Error('The five-anchor BLES01857 01.24 profile did not verify.');
      }
      const codeBefore = String(await api.read(CODE_ADDRESS, CODE_SIGNATURE.length / 2, owner, initialProfile.capability) || '')
        .replace(/\s/g, '').toUpperCase();
      if (!HEX_64.test(codeBefore) || codeBefore !== CODE_SIGNATURE) {
        throw new Error(`Actor-path code signature mismatch at ${hexValue(CODE_ADDRESS)}.`);
      }
      const descriptorBefore = parseDescriptor(await api.read(DESCRIPTOR_ADDRESS, DESCRIPTOR_BYTES, owner, initialProfile.capability));

      const finalProfile = await api.verify(owner);
      if (finalProfile.profile !== 'BLES01857 01.24 signature profile' || finalProfile.anchors !== root.RtmBuildCompatibility.anchorCount || api.session() !== owner) {
        throw new Error('The final BLES profile or console-session recheck failed.');
      }
      const codeAfter = String(await api.read(CODE_ADDRESS, CODE_SIGNATURE.length / 2, owner, finalProfile.capability) || '')
        .replace(/\s/g, '').toUpperCase();
      if (codeAfter !== CODE_SIGNATURE) throw new Error('The actor-path code signature changed during the sample.');
      const descriptorAfter = parseDescriptor(await api.read(DESCRIPTOR_ADDRESS, DESCRIPTOR_BYTES, owner, finalProfile.capability));
      if (api.session() !== owner) throw new Error('The console session changed during the final descriptor read.');

      render(output, descriptorBefore, descriptorAfter, codeAfter === CODE_SIGNATURE);
      status.dataset.state = descriptorAfter.storage && descriptorAfter.stride ? 'matched' : 'warning';
      status.textContent = descriptorAfter.storage && descriptorAfter.stride && descriptorAfter.capacity
        ? 'The BLES 40-slot actor-pool descriptor is stable. Only its 20-byte descriptor was read; no storage records were read.'
        : 'The BLES actor-pool descriptor is stable but not initialized. No storage records were read.';
    } catch (error) {
      output.textContent = '';
      output.hidden = true;
      status.dataset.state = 'error';
      status.textContent = `Actor-lookup descriptor probe refused: ${String(error?.message || error).slice(0, 220)}`;
    } finally {
      busy = false;
      button.disabled = false;
      if (clearButton) clearButton.disabled = false;
    }
  }

  function clearProbe() {
    const output = document.getElementById('actor-lookup-descriptor-output');
    const status = document.getElementById('actor-lookup-descriptor-status');
    if (output) {
      output.textContent = '';
      output.hidden = true;
    }
    if (status) {
      status.textContent = 'No actor-pool descriptor sample taken.';
      delete status.dataset.state;
    }
  }

  root.inspectActorLookupDescriptor = inspectActorLookupDescriptor;
  function initialize() {
    document.getElementById('actor-lookup-descriptor-clear-btn')?.addEventListener('click', clearProbe);
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', initialize, { once: true });
  else initialize();
})(window);
