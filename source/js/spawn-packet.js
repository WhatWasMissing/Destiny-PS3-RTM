(function (root, factory) {
  'use strict';

  const api = factory();
  if (typeof module === 'object' && module && module.exports) module.exports = api;
  if (root) root.DESTINY_RTM_SPAWN_PACKET = api;
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
  'use strict';

  // FUN_140436020 builds a 0x130-byte native vector with a four-byte zero
  // prefix. FUN_14043c180 copies vector[4..0x12f] into the slot after its
  // status word, so the actual slot payload is 0x12c bytes.
  const BODY_LENGTH = 0x12c;
  const NATIVE_VECTOR_LENGTH = 0x130;
  const OPERATION_SPAWN = 1;
  const POSE_FLOAT_COUNT = 9;
  // Blackrock's packet writer treats pose[1] as spacing and rejects pose[2]
  // unless it is positive; its UI persistence key names pose[2] as scale.
  const SPACING_POSE_INDEX = 1;
  const SPAWN_SCALE_POSE_INDEX = 2;

  // These are offsets from the start of the 0x12c-byte copied slot payload.
  // The payload starts four bytes into the native slot, so bodyOffset = slotOffset - 4.
  const BODY_OFFSETS = Object.freeze({
    operation: 0x00,       // slot +0x04
    sequence: 0x04,        // slot +0x08
    expectedType: 0x20,    // slot +0x24
    tag: 0x8c,             // slot +0x90
    placementMode94: 0x90, // slot +0x94, accepted values 0..2
    placementMode98: 0x94, // slot +0x98, accepted values 0..2
    count: 0x98,           // slot +0x9c, accepted values 1..32
    placementModeA0: 0x9c, // slot +0xa0, accepted values 0..1
    pose: 0xa0             // slot +0xa4, nine big-endian Float32 values
  });

  // Keep the native slot-relative coordinates visible for callsite review.
  const SLOT_OFFSETS = Object.freeze({
    operation: 0x04,
    sequence: 0x08,
    expectedType: 0x24,
    tag: 0x90,
    placementMode94: 0x94,
    placementMode98: 0x98,
    count: 0x9c,
    placementModeA0: 0xa0,
    pose: 0xa4
  });

  const RESERVED_RANGES = Object.freeze([
    Object.freeze([0x08, 0x20]),
    Object.freeze([0x24, 0x8c]),
    Object.freeze([0xc4, BODY_LENGTH])
  ]);

  function requireRecord(value) {
    if (!value || typeof value !== 'object' || Array.isArray(value)) {
      throw new TypeError('Operation-1 packet fields must be an object');
    }
    return value;
  }

  function requireUint32(value, name) {
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) {
      throw new RangeError(`${name} must be an unsigned 32-bit integer`);
    }
    return value;
  }

  function isSourceValidExpectedTypeInput(value) {
    // The native check is `0xff < (int)param_2[1]`: all int32-negative inputs
    // pass, as do unsigned bit patterns with bit 31 set (the cast makes them
    // negative). Positive signed values above 0xff are rejected.
    if (!Number.isInteger(value) || value < -0x80000000 || value > 0xffffffff) return false;
    return value <= 0xff || value >= 0x80000000;
  }

  function isSourceValidEncodedExpectedType(value) {
    // The native writer canonicalizes every negative input to this one wire value.
    return value === 0xffffffff || (Number.isInteger(value) && value >= 0 && value <= 0xff);
  }

  function requireExpectedType(value) {
    if (!isSourceValidExpectedTypeInput(value)) {
      throw new RangeError('expectedType must be an int32-negative value or an integer from 0 through 255');
    }
    return value < 0 || value >= 0x80000000 ? 0xffffffff : value;
  }

  function isSourceValidTag(value) {
    if (!Number.isInteger(value) || value < 0 || value > 0xffffffff || value === 0 || value === 0xffffffff) {
      return false;
    }

    // Mirrors FUN_140436020: tags must be negative as signed int32, and the
    // 14-bit page field at tag bits 13..26 must be at least 0x400.
    const signedTag = value | 0;
    const pageField = (value >>> 13) & 0x3fff;
    return signedTag < 0 && pageField >= 0x400;
  }

  function requireEntityTag(value) {
    const tag = requireUint32(value, 'tag');
    if (!isSourceValidTag(tag)) {
      throw new RangeError('tag must match the source entity-tag shape (nonzero signed tag, page field 0x400..0x3fff)');
    }
    return tag;
  }

  function requireBoundedInteger(value, name, min, max) {
    if (!Number.isInteger(value) || value < min || value > max) {
      throw new RangeError(`${name} must be an integer from ${min} through ${max}`);
    }
    return value;
  }

  function requirePose(value) {
    if ((!Array.isArray(value) && !ArrayBuffer.isView(value)) || value.length !== POSE_FLOAT_COUNT) {
      throw new TypeError(`pose must contain exactly ${POSE_FLOAT_COUNT} Float32 values`);
    }

    const pose = Array.from(value);
    for (let i = 0; i < pose.length; i++) {
      if (typeof pose[i] !== 'number' || !Number.isFinite(pose[i]) || !Number.isFinite(Math.fround(pose[i]))) {
        throw new RangeError(`pose[${i}] must be finite and representable as Float32`);
      }
    }
    if (pose[SPACING_POSE_INDEX] < 0) {
      throw new RangeError(`pose[${SPACING_POSE_INDEX}] is the multi-count spacing multiplier and cannot be negative`);
    }
    if (!(Math.fround(pose[SPAWN_SCALE_POSE_INDEX]) > 0)) {
      throw new RangeError(`pose[${SPAWN_SCALE_POSE_INDEX}] is the spawn scale and must be positive as Float32`);
    }
    return pose;
  }

  function setU32(view, offset, value) {
    view.setUint32(offset, value, false);
  }

  function buildOperation1(fields) {
    const input = requireRecord(fields);
    const sequence = requireUint32(input.sequence, 'sequence');
    const expectedType = requireExpectedType(input.expectedType);
    const tag = requireEntityTag(input.tag);

    // FUN_140436020 validates these source fields before writing the body:
    // slot+0x94 <= 2, slot+0x98 <= 2, slot+0x9c in 1..32, slot+0xa0 <= 1.
    const placementMode94 = requireBoundedInteger(input.placementMode94, 'placementMode94', 0, 2);
    const placementMode98 = requireBoundedInteger(input.placementMode98, 'placementMode98', 0, 2);
    const count = requireBoundedInteger(input.count, 'count', 1, 32);
    const placementModeA0 = requireBoundedInteger(input.placementModeA0, 'placementModeA0', 0, 1);
    const pose = requirePose(input.pose);

    // The protocol body is zero-filled first. Only fields confirmed by the
    // native packet writer are populated; gaps and the remaining tail stay zero.
    const body = new Uint8Array(BODY_LENGTH);
    const view = new DataView(body.buffer, body.byteOffset, body.byteLength);
    setU32(view, BODY_OFFSETS.operation, OPERATION_SPAWN);
    setU32(view, BODY_OFFSETS.sequence, sequence);
    setU32(view, BODY_OFFSETS.expectedType, expectedType);
    setU32(view, BODY_OFFSETS.tag, tag);
    setU32(view, BODY_OFFSETS.placementMode94, placementMode94);
    setU32(view, BODY_OFFSETS.placementMode98, placementMode98);
    setU32(view, BODY_OFFSETS.count, count);
    setU32(view, BODY_OFFSETS.placementModeA0, placementModeA0);
    for (let i = 0; i < pose.length; i++) {
      view.setFloat32(BODY_OFFSETS.pose + i * 4, pose[i], false);
    }
    return body;
  }

  function buildNativeOperation1Vector(fields) {
    const body = buildOperation1(fields);
    const vector = new Uint8Array(NATIVE_VECTOR_LENGTH);
    vector.set(body, 4);
    return vector;
  }

  function asByteView(input) {
    if (input instanceof ArrayBuffer) return new Uint8Array(input);
    if (ArrayBuffer.isView(input)) {
      return new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
    }
    throw new TypeError('packet body must be an ArrayBuffer or typed-array view');
  }

  function decodeOperation1(input) {
    const bytes = asByteView(input);
    if (bytes.byteLength !== BODY_LENGTH) {
      throw new RangeError(`operation-1 body must be exactly 0x${BODY_LENGTH.toString(16)} bytes`);
    }

    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const pose = [];
    for (let i = 0; i < POSE_FLOAT_COUNT; i++) {
      pose.push(view.getFloat32(BODY_OFFSETS.pose + i * 4, false));
    }

    const reservedNonzeroOffsets = [];
    for (const [start, end] of RESERVED_RANGES) {
      for (let i = start; i < end; i++) {
        if (bytes[i] !== 0) reservedNonzeroOffsets.push(i);
      }
    }

    return {
      bodyLength: bytes.byteLength,
      operation: view.getUint32(BODY_OFFSETS.operation, false),
      isOperation1: view.getUint32(BODY_OFFSETS.operation, false) === OPERATION_SPAWN,
      sequence: view.getUint32(BODY_OFFSETS.sequence, false),
      expectedType: view.getUint32(BODY_OFFSETS.expectedType, false),
      expectedTypeValid: isSourceValidEncodedExpectedType(view.getUint32(BODY_OFFSETS.expectedType, false)),
      tag: view.getUint32(BODY_OFFSETS.tag, false),
      tagValid: isSourceValidTag(view.getUint32(BODY_OFFSETS.tag, false)),
      placementMode94: view.getUint32(BODY_OFFSETS.placementMode94, false),
      placementMode98: view.getUint32(BODY_OFFSETS.placementMode98, false),
      count: view.getUint32(BODY_OFFSETS.count, false),
      placementModeA0: view.getUint32(BODY_OFFSETS.placementModeA0, false),
      pose,
      poseIsFinite: pose.every(Number.isFinite),
      spacingIsNonnegative: Number.isFinite(pose[SPACING_POSE_INDEX]) && pose[SPACING_POSE_INDEX] >= 0,
      scaleIsPositive: Number.isFinite(pose[SPAWN_SCALE_POSE_INDEX]) && pose[SPAWN_SCALE_POSE_INDEX] > 0,
      placementModesValid:
        view.getUint32(BODY_OFFSETS.placementMode94, false) <= 2 &&
        view.getUint32(BODY_OFFSETS.placementMode98, false) <= 2 &&
        view.getUint32(BODY_OFFSETS.placementModeA0, false) <= 1,
      countInRange: view.getUint32(BODY_OFFSETS.count, false) >= 1 && view.getUint32(BODY_OFFSETS.count, false) <= 32,
      reservedBytesAreZero: reservedNonzeroOffsets.length === 0,
      reservedNonzeroOffsets,
      offsets: { bodyRelative: BODY_OFFSETS, slotRelative: SLOT_OFFSETS }
    };
  }

  function decodeNativeOperation1Vector(input) {
    const bytes = asByteView(input);
    if (bytes.byteLength !== NATIVE_VECTOR_LENGTH) {
      throw new RangeError(`native operation-1 vector must be exactly 0x${NATIVE_VECTOR_LENGTH.toString(16)} bytes`);
    }
    const prefixIsZero = bytes[0] === 0 && bytes[1] === 0 && bytes[2] === 0 && bytes[3] === 0;
    const decoded = decodeOperation1(bytes.subarray(4));
    return { ...decoded, nativeVectorLength: bytes.byteLength, nativePrefixIsZero: prefixIsZero };
  }

  return Object.freeze({
    BODY_LENGTH,
    NATIVE_VECTOR_LENGTH,
    OPERATION_SPAWN,
    POSE_FLOAT_COUNT,
    SPACING_POSE_INDEX,
    SPAWN_SCALE_POSE_INDEX,
    BODY_OFFSETS,
    SLOT_OFFSETS,
    buildOperation1,
    buildNativeOperation1Vector,
    decodeOperation1,
    decodeNativeOperation1Vector
  });
});
