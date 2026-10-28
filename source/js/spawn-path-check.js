// Read-only fingerprint for the BLES01857 01.24 authored-member path and the
// Blackrock executor hook site. It installs no executor and enables
// no spawn action.
(function (root) {
  'use strict';

  const SIGNATURES = Object.freeze([
    Object.freeze({
      name: 'Authored-member state apply',
      address: 0x00257AAC,
      expected: '7C0802A648B40F61F821FF31F80100E0607B0000609F00004BFB7D952C030000418203A43C80018F'
    }),
    Object.freeze({
      name: 'Authored-member creation gate',
      address: 0x002575F8,
      expected: '7C0802A648B41425F821FF71F80100A0607F0000886400062C03000041820054887F014C2C030000'
    }),
    Object.freeze({
      name: 'Authored-member per-frame update',
      address: 0x00256E30,
      expected: '7C0802A648B41BD9F821FF51F80100C0607A00004BFB8A152C030000418204B83BE0000063440000'
    }),
    Object.freeze({
      name: 'Blackrock executor hook-site instruction / original BLES callee 0x003510F0',
      address: 0x00354074,
      expected: '4BFFD07D',
      installedExpected: '494E9F8D'
    }),
    Object.freeze({
      name: 'BLES object creation entry / output handle and descriptor ABI',
      address: 0x0013ED38,
      expected: '7C0802A648C59CC9F821FE21F80101F03FA001A2607E0000609A00003BE0FFFF83BDD9D4807D03C42C83000A2C030005'
    }),
    Object.freeze({
      name: 'BLES object manager allocation / registration continuation',
      address: 0x0013ED90,
      expected: '807D03C457FB04FE5464103A306300017C9D201478840020907D03C493E403C8'
    }),
    Object.freeze({
      name: 'BLES entry descriptor / ELFv1 TOC 0x01891798',
      address: 0x01849568,
      expected: '0001238C01891798'
    })
  ]);

  const READ_HEX = /^(?:[0-9A-F]{2})+$/;
  let busy = false;

  function addressText(address) {
    return `0x${address.toString(16).toUpperCase().padStart(8, '0')}`;
  }

  function unreadableRows(reason) {
    return SIGNATURES.map(signature => ({
      ...signature,
      observed: '',
      state: 'unreadable',
      detail: reason
    }));
  }

  function renderRows(output, rows) {
    output.replaceChildren();
    for (const row of rows) {
      const item = document.createElement('div');
      item.className = 'spawn-path-check-result';
      item.dataset.state = row.state;

      const heading = document.createElement('strong');
      heading.className = 'spawn-path-check-result-title';
      heading.dataset.state = row.state;
      heading.textContent = `${row.state.toUpperCase()} · ${row.name} · ${addressText(row.address)}`;

      const expected = document.createElement('code');
      expected.textContent = `Expected (${row.expected.length / 2} bytes): ${row.expected}`;

      const observed = document.createElement('code');
      observed.textContent = row.observed ? `Read:     ${row.observed}` : `Read:     unavailable${row.detail ? ` (${row.detail})` : ''}`;

      item.append(heading, expected, observed);
      output.append(item);
    }
    output.hidden = false;
  }

  async function inspectSpawnPath() {
    const button = document.getElementById('spawn-path-check-btn');
    const status = document.getElementById('spawn-path-check-status');
    const output = document.getElementById('spawn-path-check-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    output.replaceChildren();
    output.hidden = true;
    status.dataset.state = 'pending';
    status.textContent = 'Checking the BLES01857 01.24 profile, authored-member path, executor hook site, object-creation routine, and ELFv1 TOC…';

    let rows = [];
    try {
      if (!api) throw new Error('Read-only session memory API is unavailable');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before checking the spawn-path code');

      const before = await api.verify(owner);
      if (before.profile !== 'BLES01857 01.24 signature profile' || before.anchors !== root.RtmBuildCompatibility.anchorCount) {
        throw new Error('The five-anchor BLES01857 01.24 profile did not verify');
      }

      rows = [];
      for (const signature of SIGNATURES) {
        try {
          const expectedBytes = signature.expected.length / 2;
          const raw = await api.read(signature.address, expectedBytes, owner, before.capability);
          const observed = String(raw || '').replace(/\s/g, '').toUpperCase();
          if (!READ_HEX.test(observed) || observed.length !== expectedBytes * 2) {
            rows.push({ ...signature, observed, state: 'unreadable', detail: 'short or invalid memory response' });
          } else {
            rows.push({ ...signature, observed, state: observed === signature.expected ? 'match' : 'mismatch', detail: '' });
          }
        } catch (error) {
          rows.push({ ...signature, observed: '', state: 'unreadable', detail: String(error?.message || error) });
        }
      }

      let finalProfileError = '';
      try {
        const after = await api.verify(owner);
        if (after.profile !== 'BLES01857 01.24 signature profile' || after.anchors !== root.RtmBuildCompatibility.anchorCount) {
          throw new Error('The final five-anchor BLES01857 01.24 profile did not verify');
        }
        if (api.session() !== owner) throw new Error('Console session changed during the code check');
      } catch (error) {
        finalProfileError = String(error?.message || error);
      }

      if (finalProfileError) {
        rows = rows.map(row => ({
          ...row,
          state: 'unreadable',
          detail: `final profile/session recheck failed: ${finalProfileError}`
        }));
        renderRows(output, rows);
        status.dataset.state = 'unreadable';
        status.textContent = 'Code check is unreadable because the final profile/session recheck failed. No executor was installed and no spawn operation was attempted.';
        return;
      }

      renderRows(output, rows);
      if (rows.some(row => row.state === 'mismatch')) {
        status.dataset.state = 'mismatch';
        status.textContent = 'One or more signatures mismatch. This fingerprints code only and does not enable or perform spawning.';
      } else if (rows.some(row => row.state === 'unreadable')) {
        status.dataset.state = 'unreadable';
        status.textContent = 'One or more signatures could not be read. This fingerprints code only and does not enable or perform spawning.';
      } else {
        status.dataset.state = 'matched';
        status.textContent = 'All seven signatures match, including Blackrock’s hook-site instruction, BLES object-creation routine, and TOC. This fingerprints code only; it does not verify that a selected Blackrock tag resolves to the intended BLES class, validate descriptor/placement behavior, install the executor, or spawn anything.';
      }
    } catch (error) {
      rows = unreadableRows(String(error?.message || error));
      renderRows(output, rows);
      status.dataset.state = 'unreadable';
      status.textContent = `Spawn-path code check is unreadable: ${String(error?.message || error)}. No executor was installed and no spawn operation was attempted.`;
    } finally {
      busy = false;
      button.disabled = false;
    }
  }

  root.rtmSpawnPathSignatures = SIGNATURES;
  root.inspectSpawnPathCode = inspectSpawnPath;
})(window);
