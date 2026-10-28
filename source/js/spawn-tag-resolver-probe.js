// Read-only BLES 01.24 probe for the packed-handle decode used by 0x0013B7E8.
// A successful lookup candidate does not prove that a content tag creates an actor.
(function (root) {
  'use strict';

  const HANDLE_TABLE_ROOT = 0x01896D50;
  const HANDLE_RESOLVER_ADDRESS = 0x0013B7E8;
  const HANDLE_RESOLVER_SIGNATURE = '3C80018980A300007CA66E7054A504FE54C774BE80846D5060E703FF7CE630388084000054C63032806300047C8430147884002080C400307CA531D680C40010808400347CA6281478A6002080C600087CC420387C8428107C641814786300204E800020';
  const CONSTRUCTOR_TYPE_COPY_ADDRESS = 0x0013EE1C;
  const CONSTRUCTOR_TYPE_COPY_SIGNATURE = '8879004E92F80060987800089AF8000B';
  const PROFILE_NAME = 'BLES01857 01.24 signature profile';
  const MEMORY_START = 0x10000;
  const MEMORY_END = 0x40000000;
  const MAX_READ = 0x600;
  let busy = false;

  function checkedAddress(address, size) {
    if (!Number.isSafeInteger(address) || !Number.isSafeInteger(size) || size < 1 || size > MAX_READ ||
        address < MEMORY_START || address >= MEMORY_END || address + size > MEMORY_END) {
      throw new Error('Derived pool address is outside the bounded PS3 user-memory range');
    }
    return address;
  }

  function parseHandle(text) {
    const value = String(text || '').trim().replace(/^0x/i, '');
    if (!/^[0-9a-f]{8}$/i.test(value)) throw new Error('Enter an eight-digit 32-bit tag, for example 815BA133');
    return Number.parseInt(value, 16) >>> 0;
  }

  function bytesFromHex(hex) {
    const normalized = String(hex || '').replace(/\s/g, '').toUpperCase();
    if (!normalized || normalized.length % 2 || !/^[0-9A-F]+$/.test(normalized)) {
      throw new Error('A BLES memory read returned malformed hexadecimal data');
    }
    const bytes = new Uint8Array(normalized.length / 2);
    for (let i = 0; i < bytes.length; i++) bytes[i] = Number.parseInt(normalized.slice(i * 2, i * 2 + 2), 16);
    return bytes;
  }

  function hexFromBytes(bytes) {
    return Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('').toUpperCase();
  }

  function readU32(bytes, offset = 0) {
    if (offset < 0 || offset + 4 > bytes.length) throw new Error('Short structured descriptor read');
    return (bytes[offset] * 0x1000000 + (bytes[offset + 1] << 16) + (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0;
  }

  function hex32(value) {
    return `0x${(value >>> 0).toString(16).toUpperCase().padStart(8, '0')}`;
  }

  async function inspectSpawnTagResolver() {
    const button = document.getElementById('spawn-tag-resolver-btn');
    const input = document.getElementById('spawn-tag-resolver-input');
    const status = document.getElementById('spawn-tag-resolver-status');
    const output = document.getElementById('spawn-tag-resolver-output');
    const api = root.rtmActivitySessionMemory;
    if (!button || !input || !status || !output || busy) return;

    busy = true;
    button.disabled = true;
    output.hidden = true;
    output.textContent = '';
    status.dataset.state = 'pending';
    status.textContent = 'Checking the BLES 01.24 profile and resolving one packed-handle candidate…';

    try {
      if (!api) throw new Error('Read-only session memory API is unavailable');
      const owner = api.session();
      if (!owner) throw new Error('Link the console and start Destiny before checking a tag');

      const before = await api.verify(owner);
      if (before.profile !== PROFILE_NAME || before.anchors !== root.RtmBuildCompatibility.anchorCount) {
        throw new Error('The five-anchor BLES01857 01.24 profile did not verify');
      }

      const resolverCodeBefore = String(await api.read(HANDLE_RESOLVER_ADDRESS, HANDLE_RESOLVER_SIGNATURE.length / 2, owner, before.capability) || '')
        .replace(/\s/g, '').toUpperCase();
      if (resolverCodeBefore !== HANDLE_RESOLVER_SIGNATURE) {
        throw new Error(`The BLES packed-handle resolver signature did not match at ${hex32(HANDLE_RESOLVER_ADDRESS)}`);
      }
      const typeCopyCodeBefore = String(await api.read(CONSTRUCTOR_TYPE_COPY_ADDRESS, CONSTRUCTOR_TYPE_COPY_SIGNATURE.length / 2, owner, before.capability) || '')
        .replace(/\s/g, '').toUpperCase();
      if (typeCopyCodeBefore !== CONSTRUCTOR_TYPE_COPY_SIGNATURE) {
        throw new Error(`The BLES constructor type-copy signature did not match at ${hex32(CONSTRUCTOR_TYPE_COPY_ADDRESS)}`);
      }

      async function read(address, length) {
        checkedAddress(address, length);
        return bytesFromHex(await api.read(address, length, owner, before.capability));
      }
      async function readWord(address) { return readU32(await read(address, 4)); }

      const handle = parseHandle(input.value);
      const signedShift = (handle | 0) >> 13;
      const bucketMask = ((signedShift >>> 18) | 0x3FF) >>> 0;
      const bucket = (signedShift & bucketMask) >>> 0;
      const slot = handle & 0x1FFF;
      if (bucket > 0x3FFF || slot > 0x1FFF) throw new Error('Decoded handle bucket or slot is out of range');

      const tableRoot = await readWord(HANDLE_TABLE_ROOT);
      if (!tableRoot) throw new Error('The BLES handle-table root is not initialized in this session');
      const descriptorAddress = checkedAddress(tableRoot + bucket * 0x40, 0x38);
      const descriptor = await read(descriptorAddress, 0x38);
      const storage = readU32(descriptor, 0x10);
      const stride = readU32(descriptor, 0x30);
      const pointerMask = readU32(descriptor, 0x34);
      if (!storage || !stride || stride > 0x100000) {
        throw new Error('The selected handle bucket has an uninitialized or unexpected descriptor');
      }

      const recordAddress = checkedAddress(storage + slot * stride, 0x0C);
      const recordWord = await readWord(recordAddress + 8);
      const pointerAdjustment = (recordWord & pointerMask) >>> 0;
      if (pointerAdjustment > recordAddress) throw new Error('Decoded object pointer underflows user memory');
      const descriptorOffset = 0; // Blackrock op=1 does not emit a write to descriptor +4.
      const resolvedPointer = checkedAddress(recordAddress - pointerAdjustment + descriptorOffset, 1);
      const definitionTypeAddress = checkedAddress(resolvedPointer + 0x4E, 1);
      const definitionTypeBytes = bytesFromHex(await api.read(definitionTypeAddress, 1, owner, before.capability));
      if (definitionTypeBytes.length !== 1) throw new Error('The resolved definition type byte was not readable');
      const definitionType = definitionTypeBytes[0];

      const expectedTypeInput = document.getElementById('spawn-expected-type');
      const expectedTypeText = String(expectedTypeInput?.value ?? '').trim();
      const expectedTypeValue = expectedTypeText ? Number(expectedTypeText) : null;
      const expectedTypeValid = Number.isInteger(expectedTypeValue) && expectedTypeValue >= -1 && expectedTypeValue <= 255;

      const rootAgain = await readWord(HANDLE_TABLE_ROOT);
      const descriptorAgain = await read(descriptorAddress, 0x38);
      const recordWordAgain = await readWord(recordAddress + 8);
      const definitionTypeAgain = bytesFromHex(await api.read(definitionTypeAddress, 1, owner, before.capability));
      const after = await api.verify(owner);
      if (api.session() !== owner || after.profile !== PROFILE_NAME || after.anchors !== root.RtmBuildCompatibility.anchorCount) {
        throw new Error('The final build/session recheck failed; discard this sample');
      }
      const resolverCodeAfter = String(await api.read(HANDLE_RESOLVER_ADDRESS, HANDLE_RESOLVER_SIGNATURE.length / 2, owner, after.capability) || '')
        .replace(/\s/g, '').toUpperCase();
      if (resolverCodeAfter !== HANDLE_RESOLVER_SIGNATURE) {
        throw new Error('The BLES packed-handle resolver code changed during the sample; discard this result');
      }
      const typeCopyCodeAfter = String(await api.read(CONSTRUCTOR_TYPE_COPY_ADDRESS, CONSTRUCTOR_TYPE_COPY_SIGNATURE.length / 2, owner, after.capability) || '')
        .replace(/\s/g, '').toUpperCase();
      if (typeCopyCodeAfter !== CONSTRUCTOR_TYPE_COPY_SIGNATURE) {
        throw new Error('The BLES constructor type-copy code changed during the sample; discard this result');
      }
      if (rootAgain !== tableRoot || hexFromBytes(descriptorAgain) !== hexFromBytes(descriptor) || recordWordAgain !== recordWord) {
        throw new Error('The handle table changed during this sequential sample; discard this result');
      }
      if (definitionTypeAgain.length !== 1 || definitionTypeAgain[0] !== definitionType) {
        throw new Error('The resolved definition type byte changed during this sequential sample; discard this result');
      }

      const result = {
        build_profile: PROFILE_NAME,
        resolver_function: {
          address: hex32(HANDLE_RESOLVER_ADDRESS),
          signature_bytes: HANDLE_RESOLVER_SIGNATURE.length / 2,
          signature_matches_before_and_after: true
        },
        candidate_handle: hex32(handle),
        decode: {
          signed_shift_13: hex32(signedShift),
          bucket_mask: hex32(bucketMask),
          bucket,
          slot
        },
        runtime_table: {
          root_pointer_address: hex32(HANDLE_TABLE_ROOT),
          root: hex32(tableRoot),
          bucket_descriptor: hex32(descriptorAddress),
          storage: hex32(storage),
          stride: hex32(stride),
          pointer_mask: hex32(pointerMask)
        },
        candidate_record: hex32(recordAddress),
        record_word_plus_8: hex32(recordWord),
        descriptor_offset_assumed: hex32(descriptorOffset),
        resolved_pointer_candidate: hex32(resolvedPointer),
        constructor_definition_type: {
          address: hex32(definitionTypeAddress),
          field_offset: '0x4E',
          value: definitionType,
          signature_address: hex32(CONSTRUCTOR_TYPE_COPY_ADDRESS),
          signature_bytes: CONSTRUCTOR_TYPE_COPY_SIGNATURE.length / 2,
          signature_matches_before_and_after: true,
          copied_to_created_object_offset: '0x08',
          expected_type_guard: {
            value: expectedTypeValid ? expectedTypeValue : null,
            enabled: expectedTypeValid && expectedTypeValue !== -1,
            matches: expectedTypeValid && expectedTypeValue !== -1 ? expectedTypeValue === definitionType : null
          }
        },
        resolution_rechecks_match: true,
        note: 'The BLES constructor copies definition +0x4E to created object +0x08, which Blackrock operation 1 checks against its optional expected-type guard. This sampled byte does not establish enemy identity, hostility, placement, or successful creation.'
      };
      output.textContent = JSON.stringify(result, null, 2);
      output.hidden = false;
      status.dataset.state = 'matched';
      status.textContent = 'Handle lookup and constructor type-byte sample completed. The value can inform the optional type guard; it does not prove this is a hostile or spawnable enemy.';
    } catch (error) {
      status.dataset.state = 'error';
      status.textContent = `Tag lookup refused or incomplete: ${String(error?.message || error)}`;
    } finally {
      busy = false;
      button.disabled = false;
    }
  }

  root.inspectSpawnTagResolver = inspectSpawnTagResolver;
})(window);
