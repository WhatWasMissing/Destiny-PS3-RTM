# Local Destiny 1.24 address audit

This note records a static comparison between local RTM address claims and
the exact BLES01857 executable inspected during the port. It is evidence about
that file only; it does not describe runtime memory after the game loads, a
different update, or an RPCS3/console session.

## Executable identity and mapping

The inspected files `D:\Soul rtm\BLES01857_PATCH0024_FRESH.ELF` and
`D:\Soul rtm\EBOOT.ELF` were byte-identical. Their SHA-256 is
`e14e1cedaf85a10ef08a3d4b86d86ff942f222cd27fde9a68d612947169846c6`.
The image is ELF64, big-endian, PowerPC64. Its first load segment maps virtual
addresses `0x00010000` through `0x01706078` to file offsets `address - 0x10000`
with read/execute permissions. The low addresses below are therefore inside
the executable image, not static float data in this file.

## Conflicting source candidates

| Feature | Local RTM claim | Bytes/instruction in this ELF | Assessment |
|---|---|---|---|
| Brightness | C# writes Float32 `50`, `1`, and `-20` at `0x006101C0` (`42480000`, `3F800000`, `C1A00000`) | File offset `0x006001C0`: `62A30000`, PPC `ori r3,r21,0` | Do not write. The app exposes a live read probe only; matching a preset word during a runtime read still would not establish that this code location is a brightness variable. |
| FOV | C# uses `0x0070900C`; current web RTM uses `0x0070906C` | `0x0070900C`: `bl 0x0073A53C`; `0x0070906C`: `lwz r22,-0x576E(r13)` | Both addresses map to instructions in this ELF. The discrepancy is unresolved; do not switch the app based only on source names. |
| Flip View | 1.24 C# uses ON `40A0A000`, OFF `3F000000` at `0x006150C0`; current web RTM uses those words at `0x00614C40` | `0x006150C0`: `stw r5,0x30(r27)`; `0x00614C40`: `addic r3,r3,-0x1378` | Both are code locations here, and neither baseline instruction matches the toggle words. Keep the existing control unchanged pending runtime evidence. |
| Power Boost / Variable Buff | 1.24 C# uses `0x0005E614`, ON `403F0000`, OFF `3F800000`; Overwalls and the app use ON `403F0004` at the same address | `0x0005E614`: `ld r30,0xA0(r1)` | The address is code in this ELF and neither ON word matches. The values and labels differ across sources; preserve the disagreement as unresolved. |
| One-Hit Enemies | Main RTM and 1.24 C# use ON halfword `447A`; Overwalls uses `547A`, both at `0x0043E626`; both sources use OFF `3F80` | Halfword `1838` within instruction `7CC31838` at `0x0043E624` | Neither source ON value matches this baseline instruction. Do not infer the correct runtime replacement from the source copies alone. |

## How to use the in-app probe

The Brightness panel reads exactly four bytes at `0x006101C0` and displays both
the raw word and its big-endian Float32 interpretation. It reports whether the
word matches one of the three local C# presets, but it never writes or offers a
restore action. A preset match is only a comparison result, not proof that the
address has brightness semantics. Confirm any candidate with disassembly or
runtime behavior on the exact game build before adding a write control.

## Evidence boundary

These findings come from an on-disk executable and local source copies. They do
not establish the loaded process base, runtime code modifications, memory
protection behavior, or gameplay effect. Runtime reads can add evidence about a
connected process, but a single matching word does not prove an address's
meaning. Keep build identity and an instruction/data classification with any
future candidate before exposing a write.
