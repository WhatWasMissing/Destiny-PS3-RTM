# Destiny EBOOT identity and address audit

The first ELF checked in this audit had a misleading `PATCH0024` filename: its
hash identifies the base-disc 01.00 build. A search across the local drives later
found the actual decrypted 01.24 update ELF on E:. This document separates
static patch-site evidence from live-process behavior.

## Executable identity

The base-disc files `D:\Soul rtm\BLES01857_PATCH0024_FRESH.ELF` and
`D:\Soul rtm\EBOOT.ELF` are identical at 24,853,536 bytes, SHA-256
`e14e1cedaf85a10ef08a3d4b86d86ff942f222cd27fde9a68d612947169846c6`.
Published validation data identifies that hash as BLES01857 **01.00**.

The matching local update executable is
`E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF`, 26,181,096
bytes, SHA-256
`e7f100b38bbaf6c23f71b390fdb103d31b427187c38dcfbd47e09c94c4a44206`.
Its hash and size match the published decrypted BLES01857 **01.24** EBOOT.
The paired SELF is under `ebootbinelfBAK\EBOOT.BIN`; the decrypted ELF hash
is the identity used for the comparisons below.

Primary build references:

- [D1 Reimagined PS3 update export validation](https://github.com/p3n1cillin/d1reimagined-source/blob/main/data/ps3_updates/EXPORT_VALIDATION.md)
- [Published SHA-256 list](https://raw.githubusercontent.com/p3n1cillin/d1reimagined-source/main/data/SHA256SUMS.txt)

## Candidate patch-site comparison

The 01.24 file is ELF64, big-endian PowerPC64. Its load segments map the game
image as follows:

| Segment | Virtual range | File backing |
|---|---|---|
| Executable/read-only | `0x00010000–0x0183D178` | Offset `0`, size `0x182D178` |
| Writable data + BSS | `0x01840000–0x01C72948` | Offset `0x01830000`, initialized size `0xC13A4`; remaining `0x3715A4` bytes are zero-filled BSS |

The table records bytes in the **01.24 ELF** at each candidate address,
alongside the source claim. These bytes can support or reject a static patch
location; they do not prove the live process has the same mapping or that a
write causes the named in-game effect.

| Feature | Local source claims | Bytes in 01.24 ELF | Static assessment |
|---|---|---|---|
| Brightness | C# writes Float32 `50`, `1`, and `-20` at `0x006101C0` (`42480000`, `3F800000`, `C1A00000`) | `0x006101C0`: `3F800000` (Float32 `1.0`) | Supports the address and the Normal preset as the 01.24 default. The app offers source presets with exact write readback; the in-game brightness effect is not established by ELF bytes alone. |
| FOV | 1.24 C# and web RTM sources write the Float32 field at `0x0070906C` | `0x0070906C`: `40000000` (Float32 `2.0`), followed by `40490E38` | Confirms the shared source address and its default value in the update ELF. A live effect check is still separate. |
| Flip View | 1.24 C# and web RTM sources use ON `40A0A000`, OFF `3F000000` at `0x00614C40` | `0x00614C40`: `3F000000` | Confirms the shared source address and OFF value in the update ELF. A live effect check is still separate. |
| Power Boost / Variable Buff | C# uses `0x0005E614`, ON `403F0000`, OFF `3F800000`; Overwalls and the app use ON `403F0004` at that address | `0x0005E614`: `3F800000` (Float32 `1.0`) | Supports the shared candidate address and OFF/default value. The two ON values differ slightly and the ELF default does not resolve which source value or gameplay label is intended. |
| One-Hit Enemies | Main RTM and 1.24 C# use ON halfword `447A`; Overwalls uses `547A`, both at `0x0043E626`; both use OFF `3F80` | `0x0043E626`: `3F80` (low halfword of `3C603F80` at `0x0043E624`) | Confirms the source patch site and OFF value in 01.24. The competing ON variants are both source-derived; the pristine ELF cannot determine which variant has the intended gameplay effect. |

For comparison, the same candidate sites in the **01.00** ELF do not match
several update-build values. At `0x006101C0` it contains `62A30000`; at
`0x0070906C` it contains `82CDA892`; and at `0x00614C40` it contains
`addic r3,r3,-0x1378`. These observations apply only to base 01.00 and are not
evidence about update 01.24.

## Brightness read and preset controls

The panel reads exactly four bytes at `0x006101C0` and displays the raw word and
its big-endian Float32 interpretation. It also exposes the local C# presets
(Bright `50`, Normal `1`, Inverted `-20`), writes them through the verified
memory path, and displays success only after exact readback. The 01.24 ELF stores
the source's Normal value (`3F800000`, `1.0`) at this address. A successful
readback confirms bytes, not the in-game brightness effect.

## Runtime-only addresses

| Address | Relation to the 01.24 ELF | Local source use |
|---|---|---|
| `0x0190EBB0` | In the zero-filled BSS of the writable segment | Overwalls reads a runtime pointer here; its coordinate fields are at pointer `+0x140`, `+0x144`, and `+0x148`, with activation data at `+0x1B8`. The pointer value itself must be read at runtime. |
| `0x600...` regions, including `0x600E66B0` and `0x60300000` | Outside every EBOOT load segment | Local RTMs use these as wallet, emblem, ammo, and other player-memory ranges. The ELF alone cannot establish their allocation or contents. |
| `0x10030000` | Outside every EBOOT load segment | Local Player Size source uses it as scratch data around a function call. The tool's ordinary memory read/write endpoint does not perform that call. |

Matching static code/data bytes do not prove server persistence. Controlled
observation in the matching 01.24 runtime is needed to confirm dynamic mappings
and in-game effects.
