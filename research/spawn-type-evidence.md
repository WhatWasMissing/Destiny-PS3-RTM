# Spawn object-type evidence

## What the guard checks

Blackrock's generated BLES 01.24 operation-1 program contains this sequence in
`research/spawn-program-words.txt`:

| Program offset | PPC word | Meaning |
| --- | --- | --- |
| `+0x08D8` | `0x880C0008` | Load a byte from returned object `+0x08`. |
| `+0x08DC` | `0x80DE0024` | Load the requested expected type from packet `+0x24`. |
| `+0x08E0` | `0x7C003040` | Compare the loaded values. |

The surrounding branch skips this check when the expected type is `-1` encoded
as `0xFFFFFFFF`. This establishes the meaning of the packet guard and the
returned object's byte, not the meaning of the external catalog column.

## BLES 01.24 object byte

The hash-verified BLES01857 01.24 EBOOT is
`E7F100B38BBAF6C23F71B390FDB103D31B427187C38DCFBD47E09C94C4A44206`. In the
constructor at `0x0013ED38`, the instructions at `0x0013EE1C` and
`0x0013EE24` load a byte from definition `+0x4E` and store it at created sObject
`+0x08`. The EBOOT has a contiguous ordered list of 25 object-type names from
`0x01303778` through `0x013038D0`, including `biped`, `creature`, `emitter`,
and `system`. The sequence agrees with the local D1 Reimagined type list, where
ordinal 9 is `biped`; the BLES string sequence alone does not prove each string
is indexed by the byte copied from definition `+0x4E`.

## Blackrock catalog correlation and limit

The Blackrock release data has `type=9` for both `815BA133` (`Vex minotaur`)
and `80A6A00E` (`Player guardian`). This is consistent with a broad object-kind
value and shows that type 9 is not an enemy flag. The matching BLES object byte
and Blackrock guard compare make the catalog-to-guard mapping plausible, but
no direct BLES dataflow or live-console read proves that `spawnable.tsv`'s
`type` is the exact value expected at packet `+0x24`.

The RTM therefore preserves a numeric catalog type as metadata and offers a
separate explicit action to copy it into the expected-type field. The action
and UI label it unverified; selecting a catalog row copies only the tag. A
wrong guard value may reject a spawn. This evidence does not establish that a
tag resolves on a live BLES console, that it creates an object, or that the
result is hostile.
