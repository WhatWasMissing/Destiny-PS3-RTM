// Read-only availability scan for the exact regions used by Blackrock's
// BLES01857 01.24 command-handler bootstrap. This never reserves or writes.
(function (root) {
  'use strict';

  const PROFILE_NAME = 'BLES01857 01.24 signature profile';
  const MEMORY_START = 0x10000;
  const MEMORY_END = 0x40000000;
  const MAX_READ = 0x600;
  const CAPABILITY_READS = 192;
  const CAPABILITY_BYTES = 0x8000;
  const REGIONS = Object.freeze([
    Object.freeze({ name: 'Executor hook stub', address: 0x0183E000, size: 0x2000 }),
    Object.freeze({ name: 'Command handler and queue', address: 0x01C74000, size: 0xC000 }),
    Object.freeze({ name: 'Adjacent bootstrap reserve', address: 0x0183D200, size: 0x0E00 })
  ]);
  let busy = false;

  function hex32(value) {
    return `0x${(value >>> 0).toString(16).toUpperCase().padStart(8, '0')}`;
  }

  function checkedAddress(address, size) {
    if (!Number.isSafeInteger(address) || !Number.isSafeInteger(size) ||
        size < 1 || size > MAX_READ || address < MEMORY_START ||
        address >= MEMORY_END || address + size > MEMORY_END) {
      throw new Error('Derived scratch range is outside the bounded PS3 user-memory range');
    }
    return address;
  }

  function bytesFromHex(hex) {
    const bytes = new Uint8Array(hex.length / 2);
    for (let index = 0; index < bytes.length; index++) {
      bytes[index] = Number.parseInt(hex.slice(index * 2, index * 2 + 2), 16);
    }
    return bytes;
  }

  async function inspectSpawnExecutorRegions() {
    const button = document.getElementById('spawn-region-preflight-btn');
    const status = document.getElementById('spawn-region-preflight-status');
    const output = document.getElementById('spawn-region-preflight-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    output.hidden = true;
    output.textContent = '';
    status.dataset.state = 'pending';
    status.textContent = 'Verifying BLES 01.24, then reading Blackrock’s exact scratch ranges in bounded chunks…';

    const results = [];
    try {
      if (!api) throw new Error('Read-only session memory API is unavailable');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before checking executor regions');

      let profile = await api.verify(owner);
      if (profile.profile !== PROFILE_NAME || profile.anchors !== root.RtmBuildCompatibility.anchorCount) {
        throw new Error('The five-anchor BLES01857 01.24 profile did not verify');
      }

      let capability = profile.capability;
      let capabilityReads = 0;
      let capabilityBytes = 0;
      let blockedByOccupiedRange = false;

      for (const region of REGIONS) {
        const end = region.address + region.size;
        if (region.address < MEMORY_START || end > MEMORY_END || end <= region.address) {
          throw new Error(`${region.name} has an invalid address range`);
        }

        let offset = 0;
        let firstNonzero = null;
        while (offset < region.size) {
          if (capabilityReads >= CAPABILITY_READS || capabilityBytes >= CAPABILITY_BYTES) {
            profile = await api.verify(owner);
            if (profile.profile !== PROFILE_NAME || profile.anchors !== root.RtmBuildCompatibility.anchorCount) {
              throw new Error('BLES 01.24 profile failed during the region scan');
            }
            capability = profile.capability;
            capabilityReads = 0;
            capabilityBytes = 0;
          }

          const size = Math.min(MAX_READ, region.size - offset, CAPABILITY_BYTES - capabilityBytes);
          if (size < 1) throw new Error('Executor-region scan budget could not advance');
          const address = checkedAddress(region.address + offset, size);
          const bytes = bytesFromHex(await api.read(address, size, owner, capability));
          capabilityReads++;
          capabilityBytes += size;

          const nonzeroIndex = bytes.findIndex(byte => byte !== 0);
          if (nonzeroIndex >= 0) {
            firstNonzero = {
              address: hex32(address + nonzeroIndex),
              value: `0x${bytes[nonzeroIndex].toString(16).toUpperCase().padStart(2, '0')}`
            };
            offset += size;
            break;
          }
          offset += size;
        }

        const clear = firstNonzero === null && offset === region.size;
        results.push({
          name: region.name,
          start: hex32(region.address),
          size_bytes: region.size,
          checked_bytes: offset,
          state: clear ? 'all-zero' : 'occupied',
          first_nonzero: firstNonzero
        });
        if (!clear) {
          blockedByOccupiedRange = true;
          break;
        }
      }

      const finalProfile = await api.verify(owner);
      if (api.session() !== owner || finalProfile.profile !== PROFILE_NAME || finalProfile.anchors !== root.RtmBuildCompatibility.anchorCount) {
        throw new Error('The final BLES build/session recheck failed; discard this sample');
      }

      const fullyChecked = results.length === REGIONS.length && results.every(item => item.state === 'all-zero');
      const result = {
        build_profile: PROFILE_NAME,
        regions: results,
        all_checked_regions_zero: fullyChecked,
        stopped_on_occupied_region: blockedByOccupiedRange,
        method: 'Sequential read-only scan, logical reads up to 0x600 bytes split into PS3MAPI GETMEM requests of at most 0x100 bytes, with at most 0x8000 bytes per fresh capability.',
        limitation: 'A zero result is only a momentary availability check. It does not reserve the ranges, validate the executor stub, or guarantee they remain unused before installation.'
      };
      output.textContent = JSON.stringify(result, null, 2);
      output.hidden = false;
      status.dataset.state = fullyChecked ? 'matched' : 'warning';
      status.textContent = fullyChecked
        ? 'All three Blackrock scratch ranges read as zero. This does not reserve them or install the hook.'
        : 'A scratch range is occupied. Blackrock’s zero-only installer would refuse to install here.';
    } catch (error) {
      if (results.length) {
        output.textContent = JSON.stringify({
          build_profile: PROFILE_NAME,
          regions: results,
          all_checked_regions_zero: false,
          error: String(error?.message || error),
          limitation: 'Partial results cannot be used to decide whether installation is safe.'
        }, null, 2);
        output.hidden = false;
      }
      status.dataset.state = 'error';
      status.textContent = `Executor-region check refused or incomplete: ${String(error?.message || error)}`;
    } finally {
      busy = false;
      button.disabled = false;
    }
  }

  root.rtmSpawnExecutorRegions = REGIONS;
  root.inspectSpawnExecutorRegions = inspectSpawnExecutorRegions;
})(window);
