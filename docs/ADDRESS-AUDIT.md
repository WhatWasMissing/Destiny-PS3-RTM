# Destiny EBOOT identity and address audit

This audit corrects the build identity used in the earlier static address
review. The local ELF filename contains `PATCH0024`, but its hash identifies it
as the base 01.00 executable. The disassembly from that file cannot validate
the local RTMs' 01.24 addresses.

## Executable identity

The local files `D:\Soul rtm\BLES01857_PATCH0024_FRESH.ELF` and
`D:\Soul rtm\EBOOT.ELF` are identical at 24,853,536 bytes, SHA-256
`e14e1cedaf85a10ef08a3d4b86d86ff942f222cd27fde9a68d612947169846c6`.
The D1 Reimagined build-validation data identifies that size and hash as
BLES01857 **01.00**. It identifies the decrypted 01.24 EBOOT as 26,181,096
bytes, SHA-256
`e7f100b38bbaf6c23f71b390fdb103d31b427187c38dcfbd47e09c94c4a44206`.
The local drive search found no EBOOT/ELF with the 01.24 hash. The filename is
not reliable evidence of patch version.

Primary build references:

- [D1 Reimagined PS3 update export validation](https://github.com/p3n1cillin/d1reimagined-source/blob/main/data/ps3_updates/EXPORT_VALIDATION.md)
- [Published SHA-256 list](https://raw.githubusercontent.com/p3n1cillin/d1reimagined-source/main/data/SHA256SUMS.txt)

## What the local 01.00 file shows

The file is ELF64, big-endian, PowerPC64. Its first load segment maps virtual
addresses `0x00010000` through `0x01706078` to file offsets `address - 0x10000`
with read/execute permissions. The following are static observations from
**01.00 only**; they are not findings about the 01.24 executable or live
process:

| Feature | Local RTM source claim | Bytes/instruction in local 01.00 ELF | 01.24 assessment |
|---|---|---|---|
| Brightness | C# writes Float32 `50`, `1`, and `-20` at `0x006101C0` (`42480000`, `3F800000`, `C1A00000`) | File offset `0x006001C0`: `62A30000`, PPC `ori r3,r21,0` | Unresolved. The app exposes a live read probe only until this address is checked against the correct build and its runtime meaning is established. |
| FOV | 1.24 C# uses `0x0070900C`; current web RTM uses `0x0070906C` | At those addresses in 01.00: `bl 0x0073A53C` and `lwz r22,-0x576E(r13)` | Unresolved. This old-build disassembly cannot select the 01.24 address. |
| Flip View | 1.24 C# uses ON `40A0A000`, OFF `3F000000` at `0x006150C0`; web RTM uses those words at `0x00614C40` | At those addresses in 01.00: `stw r5,0x30(r27)` and `addic r3,r3,-0x1378` | Unresolved. Do not change either address based on 01.00. |
| Power Boost / Variable Buff | 1.24 C# uses `0x0005E614`, ON `403F0000`, OFF `3F800000`; Overwalls and the app use ON `403F0004` at the same address | `0x0005E614`: `ld r30,0xA0(r1)` in 01.00 | Unresolved. Values and labels differ across sources; this old-build instruction does not resolve the target. |
| One-Hit Enemies | Main RTM and 1.24 C# use ON halfword `447A`; Overwalls uses `547A`, both at `0x0043E626`; both sources use OFF `3F80` | Halfword `1838` within `7CC31838` at `0x0043E624` in 01.00 | Unresolved. Both source variants need to be checked against the exact 01.24 build/runtime. |

## Brightness probe

The panel reads exactly four bytes at `0x006101C0` and displays the raw word
and its big-endian Float32 interpretation. It reports whether the word matches
one of the local C# presets, but it never writes or offers a restore action.
The old 01.00 disassembly is not evidence that this is code or data in 01.24.
A preset match during a runtime read would still not, by itself, prove that
the address has brightness semantics.

## Next verification step

Before using static disassembly to assess an RTM patch, acquire or produce a
decrypted ELF whose SHA-256 matches the published 01.24 value above. Record the
hash and load-segment mapping, then compare each candidate's instruction or
data bytes. Follow that with a controlled read-only observation in the matching
01.24 runtime. Source labels and a write/readback alone do not prove the
in-game effect or server persistence.
