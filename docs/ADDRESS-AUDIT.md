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

The same 01.24 hash was also found at `C:\Users\novel\Downloads\EBOOT.ELF`
and in three E: backups. No confirmed BLUS31181 01.24 ELF was located; the
available BLUS update packages found locally stop at 01.23.

The other decrypted copy at `D:\Soul rtm\EBOOT.ELF` has SHA-256
`e14e1cedaf85a10ef08a3d4b86d86ff942f222cd27fde9a68d612947169846c6`, which
identifies it as BLES01857 **01.00**, not 01.24. Its adjacent game metadata is
also version 01.00. The installed US BLUS31181 and European BLES01857 game
folders inspected on this PC likewise report 01.00. The verified 01.24 ELF
exists separately in the RTM research backups; the folder evidence does not
establish which version is currently running on a console.

This ELF has no static or dynamic symbol-table sections, and its section-name
table is zero-filled. A decompiler can still lift PowerPC instructions into
approximate pseudocode, but function names and boundaries must be reconstructed
from control flow and call sites.

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
| Pause Game / framebuffer freeze | `E:\Destiny PS3 RTMs\Up to Date soul\destinyRTM\Form1.cs` writes `00` to byte `0x006155BF` to pause and `10` to resume; its screenshot handlers repeat that pair. | The aligned instruction at `0x006155BC` is `41820010`; changing its final byte produces `41820000`. | The source label maps to a conditional-branch displacement, not a standalone data field. The app now checks and writes the complete known instruction, and restores only from the original or patched word. The previous screenshot target `0x00614BFF` has no matching local source claim. A live pause effect remains unverified. |
| Power Boost / Variable Buff | C# uses `0x0005E614`, ON `403F0000`, OFF `3F800000`; Overwalls and the app use ON `403F0004` at that address | `0x0005E614`: `3F800000` (Float32 `1.0`) | Supports the shared candidate address and OFF/default value. The two ON values differ slightly and the ELF default does not resolve which source value or gameplay label is intended. |
| One-Hit Enemies | Main RTM and 1.24 C# use ON halfword `447A`; Overwalls uses `547A`, both at `0x0043E626`; both use OFF `3F80` | `0x0043E626`: `3F80` (low halfword of `3C603F80` at `0x0043E624`) | Confirms the source patch site and OFF value in 01.24. The competing ON variants are both source-derived; the pristine ELF cannot determine which variant has the intended gameplay effect. |
| Ultimate RTM source-labeled scalar presets | Recovered handlers write `3FFFFFFF` (“Max Damage”) or `42C80000` (“1 Hit Kill 2”) to `0x004449EC`; both restore `3F800000` | `0x004449EC`: `3F800000`; exact consumer signature at `0x00444B64` | The word is part of a literal pool in the executable segment. A branch at `0x004449E8` jumps over the pool; `0x00444B80` reads the first word as Float32. The consumer uses it as a coefficient only when a ratio denominator is below about `0.0001`, then multiplies output-buffer `+0x11C`. The output structure and gameplay meaning are unidentified, so the source labels do not prove a damage effect. Writes remain source-labeled and require the exact signature, a known current value, and readback. |
| Unlimited Ammo | 1.24 C# writes ON bytes `3880270F`, `38A0270F`, and `60000000FC1EF8003FC0461C63DE3C0093DF0110` at `0x003108EC`, `0x00310554`, and `0x00CF95AC` | OFF bytes in 01.24 ELF are `63C40000`, `63C50000`, and `63DD0000408200083BA00001807F001CD3DF0110`, matching the C# source. The former app address `0x00310F94` contains `7C9E20107C8407B44BFFF7C92C030000…`, not the declared OFF value `3F800000`. | Supports the C# three-site OFF mapping. The incorrect single-site app target was replaced; exact readback is required for all three sites on enable and reset. This does not prove the gameplay effect. |
| Warpriest Immunity - web RTM variant | Local web RTM writes ON `C0434F80` and OFF `C0434DB0` at `0x00444CA4`, plus `3F800000` at `0x01897420` | 01.24 ELF contains `C04100F0` at `0x00444CA4` and `37343036` at `0x01897420` | Both claimed OFF values mismatch the verified executable. The main web RTM control and reset entry are blocked for this build; no replacement values are inferred. |
| Warpriest Immunity - Overwalls C# variant | C# writes ON `C0420348` and OFF `C04100F0` at `0x00444CA4`, and writes `3F800000` to `0x01891AE0` only when enabled | 01.24 ELF contains the claimed OFF instruction `C04100F0`; `0x01891AE0` lies in zero-filled BSS | The primary OFF instruction matches the executable. The source leaves the secondary BSS word set after disabling; its purpose and runtime effect are unconfirmed. This distinct Overwalls variant remains available in the Overwalls tab. |
| Deathsinger candidate scan | `D:\VIRUS\1111111111111PhoneRTMtest\DestinyMobileBETA_2.html` claims `38C6FFFF` → `38C60FFF` and scans broad memory for generic `addi` encodings | No aligned `38C60FFF` in 01.24; seven aligned `38C6FFFF` words are unrelated routines. One `38C00FFF` occurs at `0x006AEFEC`, but its role is unidentified. Fallback `0x001C7B58` contains `30C100907C440C0ED0410070308100A0`. | The signed immediate explains the arithmetic (`FFFF` = −1; `0FFF` = +4095), not the encounter site. The scan accepts generic matches without checking executable alignment or handler context; its first hit `0x00011670` is an initialization sentinel. No Deathsinger patch location is established, so the control remains hidden. |
| Overwalls plate hook | Overwalls replaces the two-instruction site at `0x00A015AC` with a branch to its 104-byte body at `0x010FB514`; its state flag is `0x0190EC98` | 01.24 starts with `3063FFF44BFFFB15`; the code-cave first word and BSS flag are zero | Supports the source layout in the pristine ELF. Before writing, the app now requires the exact two-instruction sequence, the complete 104-byte cave to read as zero, and the flag to be zero. This is a build-layout check; it does not verify the hook's in-game effect. |
| Overwalls physics hook | Source branch site `0x001C4110`; code cave `0x010D8EC0` | 01.24 has original instruction `30640140`; code-cave first word is zero | Confirms the source's initial branch and cave prefix in 01.24; runtime behavior remains unobserved. |
| Player Size | Local C# identifies a player by name in 24 records, gets its handle at record `+1604`, resolves the object index through `0x002FF2D0`, then writes Float32 scale at `0x01A1D9E8 + ((index & 0x1FFF) * 0xC0) + 0x9C`. | `0x002FF2D0`: resolver signature matches; `0x001FB3C8`: leaf getter `8084001C908300004E800020`. Runtime lookup table `0x01A21CB0` and scale-object base `0x01A1D9E8` are zero-filled BSS in the file. | Confirms resolver/helper code and source arithmetic, but not runtime table contents or visual effect. The app and MCP tool emulate the resolver with reads and do not use the C# RPC stub/scratch area. |

### Deathsinger instruction-context audit

The seven aligned `38C6FFFF` hits listed above were decoded from the hash-verified
01.24 executable with a PowerPC64 big-endian disassembler. Their immediate
instruction windows explain why an opcode match alone is not enough to identify
the raid mechanic:

| Address | Nearby instructions | What the local context supports |
|---|---|---|
| `0x009AAB00` | `mtctr r0; b 0x9AAB00; bdz ...; addi r6,r6,-1; divd; mulld; subf;` | A counted division/remainder loop that emits byte-sized results. The decrement is part of the loop body. |
| `0x009AC35C` | `mtctr r0; b 0x9AC35C; bdz ...; addi r6,r6,-1; divd; slwi ...,1; mulld;` | A similar counted division/remainder loop for halfword-sized results. |
| `0x00A730BC` | Leaf routine `0x00A73080..0x00A730F4`; reads a count, decrements it, then copies three words per shifted element | A vector erase/compaction path with 12-byte elements. The decrement updates the container length. |
| `0x00B1B76C` | `lwz r6,0x34(r27); slwi r9,r6,5; addi r6,r6,-1; ...; stw r6,0x34(r27)` | A record count is decremented and written back while a 0x20-byte record index is formed inside a larger SIMD/FPU method. No player deposit context is identified. |
| `0x00D743D8` | helper starts at `0x00D743D0`; `addi r6,r6,-1; ...; li r0,2; slw r4,r0,r6`, followed by divide/remainder arithmetic | Numeric/bit-rounding helper logic. The shift is used by surrounding arithmetic; no encounter-specific data is identified. |
| `0x010923CC` | function starts at `0x010923C8`; masks `r4`, decrements the transformed value, and calls `0x0108B740` | A shared size/chunk-processing routine, not an identified encounter handler. |
| `0x0109248C` | same function; `add r6,r6,r0; addi r6,r6,-1; and r6,r6,r11; add r31,r31,r6; bl 0x0108ACD8` | Rounds a chunk size, advances a buffer cursor, and calls a helper. This is consistent with generic memory/chunk processing. |

These are useful negative results: the verified 01.24 image contains the
instruction shape, but none of the seven decoded windows establishes that it
updates Deathsinger stacks. A direct linked-branch search found no callers to
the inferred starts of the vector erase, large record, numeric helper, or
chunk-processing routines. The stripped ELF has no symbols and indirect calls
remain possible, so this is supporting context rather than proof. The 01.24
ELF also contains no aligned ON encoding `38C60FFF`. The lone `38C00FFF` at
`0x006AEFEC` remains unexplained, and the source reset location `0x001C7B58`
does not contain either proposed OFF instruction. The encounter patch must
stay unavailable until an encounter-specific source path or controlled
runtime evidence identifies both the instruction and its current OFF/ON
values.

### Emote and gesture string scan

The hash-verified BLES01857 01.24 ELF contains the strings `player emote
registry` at file offset `0x012E1D60` / virtual address `0x012F1D60`,
`player emote registry handle` at `0x012E1E50` / `0x012F1E50`, and
`&packet->emote_index` at `0x012E3AB0` / `0x012F3AB0`. All three lie in
the first `PT_LOAD` segment, whose virtual-address delta is `0x10000`.

A whole-file search found no big-endian 32-bit or 64-bit references to
those string addresses. An aligned instruction scan for `addis` using high
immediate `0x012F` followed by `addi` or `ori` with each low immediate also
found no match. TOC-relative references remain unchecked because this
stripped ELF has no usable symbols and the available disassembler lacks the
PowerPC target. The strings show emote-related names and a packet-field
label, but do not identify an activation function, runtime address, or
patch. No gesture control is justified by this static evidence.

### Legacy Detertrots RTM v1.08 candidates

The decompiled source at
`D:\Soul rtm\AAAUntestedRtms\Detertrots Destiny RTM Tool {Source by UnboundGodz}\Detertrots Destiny RTM Tool 1.08 Source by UnboundGodz\Form1.cs`
advertises Headshot Kill, two-site Immunity, Stop Bullets, Crosshairs, and
Bullet Speed presets. Its Damage Modifier presets and One Hit Kill toggle
share `0x003F0D70`, so they are not independent controls. The source does not
identify which Destiny executable build these addresses target.

I read the claimed toggle defaults from the source and compared them at the
same virtual addresses in the local 01.00 and hash-verified 01.24 ELFs. None
matches either image:

| Feature/site | Source OFF bytes | Source ON bytes | 01.00 bytes | 01.24 bytes |
|---|---|---|---|---|
| One Hit Kill / Damage Modifier, `0x003F0D70` | `3C603F80` | `3C60447A` (x999) | `30840001` | `80E60030` |
| Headshot Kill, `0x003F5F5C` | `3C23D70A` | `41200000` | `7FA001CE` | `90C10030` |
| Immunity, `0x0017F3C4` | `3F800000` | `C0000000` | `54631838` | `3B400150` |
| Immunity, `0x0017CA2C` | `3F800000` | `C0000000` | `3B40FFFF` | `40800014` |
| Stop Bullets, `0x004B0FC0` | `3F800000` | `00000000` | `38A00450` | `80630034` |
| Crosshairs, `0x002ED578` | `3F800000` | `00000000` | `80830240` | `486B0E09` |

The Damage Modifier buttons also write `3C604000`, `3C6040A0`, `3C604120`,
and `3C6041A0` to the same address. Fire Speed buttons write
`40000000`, `40400000`, `40800000`, `3F000000`, `3E4CCCCD`, or `3DCCCCCD`
at `0x002E78A0`, but the source gives no reset value for that field.

The source's unconditional writes and lack of readback do not supply a safe
fallback. These controls remain unported until a matching game build and
feature behavior are identified. Bullet Speed only has preset writes in this
source excerpt; it provides no verified reset value.

A broader comparison of current fixed-site toggle OFF values against the same
01.24 ELF also matched Invincibility, No Spread, Infinite Power, Freeze
Projectiles, Enemy Ignore, Instant Respawn, Steady Aim, Unlimited Abilities,
Rapid Fire, Walk Through Objects, Zero Gravity, Fall Through Floors, Freeze
Characters, Bouncy Items, Sparrow Boost, Super Speed, and the shared Rock
Jump/No Pushback baseline. The audit helper includes the mismatched web-RTM
Warpriest values, the Overwalls C# primary OFF instruction and zero-initialized
BSS word, and the Deathsinger source fallback.

For comparison, the same candidate sites in the **01.00** ELF do not match
several update-build values. At `0x006101C0` it contains `62A30000`; at
`0x0070906C` it contains `82CDA892`; and at `0x00614C40` it contains
`addic r3,r3,-0x1378`. These observations apply only to base 01.00 and are not
evidence about update 01.24.

## Runtime write compatibility gate

The app, local desktop proxy, MCP server, and Go memory client now refuse
memory writes unless three live code reads match the audited 01.24 signature
profile:

| Address | Expected bytes | Role in the source audit |
|---|---|---|
| `0x00444B64` | `3C600044C03601283CC03F803B4349EC3061034462C4000063850000C05A0000` | Scalar consumer code |
| `0x001FB3C8` | `8084001C908300004E800020` | Player-scale lookup helper |
| `0x002FF2D0` | `7C0802A648A99751F821FF81F8010090` | Player object-handle resolver prologue |

The app checks before each serialized patch operation; desktop and Go clients
cache a successful result for up to five seconds per console. Failed or
unreadable checks are not cached. Read-only operations do not require the
profile. This is a runtime **signature profile**, not a full EBOOT hash or a
region check; current evidence proves it matches BLES01857 01.24 and rejects
the local BLES01857 01.00 image at these same addresses. There is no confirmed
BLUS31181 01.24 ELF in the local inventory, so a successful profile must not
be described as proof of the BLUS region or whole-image identity.

The Home connection panel also provides a separate read-only build check. It
re-reads each anchor without using the write gate's short cache and shows the
observed bytes and match state individually. This display is diagnostic only;
each write operation still performs its own compatibility check.

The three signature byte ranges are protected from the app's generic write
tools, so a mistaken raw write cannot erase the evidence needed for later
checks. The Overwalls plate hook uses a different code site and does not
overlap this fingerprint.

No live-console writes were performed while adding this gate. See
`docs/RTM-MCP.md` for the tools covered and the read-only behavior.

## Brightness read and preset controls

The panel reads exactly four bytes at `0x006101C0` and displays the raw word and
its big-endian Float32 interpretation. It also exposes the local C# presets
(Bright `50`, Normal `1`, Inverted `-20`), writes them through the verified
memory path, and displays success only after exact readback. The 01.24 ELF stores
the source's Normal value (`3F800000`, `1.0`) at this address. A successful
readback confirms bytes, not the in-game brightness effect.

## Ultimate RTM scalar trace

The recovered Ultimate RTM has two handlers for `0x004449EC`: one labeled “1
Hit Kill 2” writes `100.0` (`42C80000`) or restores `1.0`; the other labeled
“Max Damage” writes `3FFFFFFF` (about `2.0`) or restores `1.0`. The presets
target the same word, so the app presents them as one selector.

That word is in an executable load segment, but static control flow identifies
it as literal-pool data: the unconditional branch at `0x004449E8` jumps to
`0x00D98BAC`, around the pool, and no direct branch targets the pool words.
Linear decoding interprets `3F800000` as `lis r28,0`; it is not reached as an
instruction on the traced path. The words at `0x004449EC`, `+4`, `+8`, and
`+0xC` are `3F800000` (1.0), `38D1B717` (about 0.0001), `00000000`, and
`3C23D70A` (about 0.01); `0x004449FC` is an alignment NOP. The function begins
at `0x00444A00`.

At `0x00444B70`, code forms `r26 = 0x004449EC`; `lfs f2,0(r26)` at
`0x00444B80` loads the first pool word. Later code calls `0x00439568` twice,
forms a ratio, and compares the absolute denominator against the pool's
approximately `0.0001` threshold. When the denominator is too small, it selects
the `0x004449EC` coefficient; otherwise it selects the ratio. The selected
value multiplies the previous Float32 at caller-owned output-buffer offset
`+0x11C`, then stores the result back. A later condition can apply another
multiplier from `[r1+0x4DC]`. Initializer `0x0044CAB0` clears a 0x140-byte
record, copies source fields, and sets `+0x118` to `-1` and `+0x11C` to `1.0`.
Its four direct callers are `0x00436570`, `0x004367CC`, `0x00436EC0`, and
`0x00436FD8`; three use a stack buffer at `r1+0x140`, and one uses `r1+0x100`.
After the calculation, caller `0x004367CC` directly reads `+0x118` and passes
that value through `*r30`. The callers do not directly read `+0x11C`; they pass
the record through helpers which may inspect it indirectly. The record layout
and gameplay meaning of `+0x11C` remain unidentified, so the source labels do
not establish a damage effect.

## Runtime-only addresses

| Address | Relation to the 01.24 ELF | Local source use |
|---|---|---|
| `0x0190EBB0` | In the zero-filled BSS of the writable segment | Overwalls reads a runtime pointer here; its coordinate fields are at pointer `+0x140`, `+0x144`, and `+0x148`, with activation data at `+0x1B8`. The pointer value itself must be read at runtime. |
| `0x600...` regions, including `0x600E66B0` and `0x60300000` | Outside every EBOOT load segment | Local RTMs use these as wallet, emblem, ammo, and other player-memory ranges. The ELF alone cannot establish their allocation or contents. |
| `0x10030000` | Outside every EBOOT load segment | The original C# Player Size source uses this as scratch data around a function call. The app/MCP port does not call that function or use this scratch address. |
| `0x01A21CB0`, `0x01A1D9E8` | In zero-filled BSS of the writable segment | Player Size lookup table and scale-object base; only live reads can reveal their initialized runtime values. |

## Process-thread call path audit

The 01.24 ELF's entry point is `0x01849568`, which contains a 32-bit function
descriptor `[0x0001238C, 0x01891798]`. Scanning the inferred contiguous OPD
range `0x01849508` through `0x01889790` finds 32,850 descriptors with the same
TOC, `0x01891798`. The next descriptor at `0x01889798` has a different TOC
(`0x018C218C`) and is outside that inferred range. This supplies a static TOC
candidate for a short thread stub that calls code in the main EBOOT; it does
not prove the toast callees are thread-safe or can run outside Destiny's normal
UI thread.

The official webMAN interface documents `MEMORY PAGEALLOCATE`, `PAGEFREE`, and
`THREAD CREATE` through `/ps3mapi.ps3?<command>`. Its current handler ignores
the syscall result when reporting page allocation/free and responds HTTP 200
after issuing thread creation. In the inspected Cobra source, `THREAD CREATE`
creates a joinable process thread and calls `ppu_thread_join`, but discards
both the join result and thread exit code; webMAN then discards the syscall
result too. A successful HTTP response therefore proves neither execution nor
completion. [Cobra thread implementation](https://github.com/xXEvilnatXx/Cobra-PS3/blob/main/8.5/4.93/CEX/SRC/stage2/ps3mapi_core.c),
[webMAN handler](https://github.com/aldostools/webMAN-MOD/blob/master/include/ps3mapi/ps3mapi_server.h).

At inspected webMAN commit `45025987231248e4939a9f00ef851aa4aa0a5345`, the
HTTP JSON command route `/ps3mapi.ps3?MEMORY SET ...` is not a reliable code-write
path. Its HTTP branch treats pairs of
ASCII hex characters as bytes with `*buffer1 = (buffer2[0] << 8) | buffer2[1]`
and stores the result in an 8-bit `char`; for example, ASCII `"AB"` becomes
`0x42`, not `0xAB`. The corresponding JSON `MEMORY GET` encoder also shifts the
masked high nibble of a `char` by eight, so the returned first hex digit is
always zero. HTTP status 226/200 from those JSON commands cannot establish
correct bytes. The TCP PS3MAPI commands use their binary data connection and
bypass these two text conversions, although the handler ignores some memory
operation return values. [webMAN HTTP entry point](https://github.com/aldostools/webMAN-MOD/blob/45025987231248e4939a9f00ef851aa4aa0a5345/include/cmd/ps3mapi.h#L2-L28),
[HTTP MEMORY GET/SET handlers](https://github.com/aldostools/webMAN-MOD/blob/45025987231248e4939a9f00ef851aa4aa0a5345/include/ps3mapi/ps3mapi_server.h#L518-L617).

The RTM's `/setmem.ps3mapi` and `/getmem.ps3mapi` requests use webMAN's separate
GETMEM editor routes, not the HTTP JSON `MEMORY SET/GET` commands. The inspected
editor converts `val=` with `Hex2Bin` before calling `ps3mapi_patch_process`,
and renders returned bytes with `%02X`; this is the byte path used for exact
write readback in the app. The editor handles up to 256 bytes per operation.
Static source support does not establish the
behavior of the user's installed webMAN build, so continue to require exact
readback. Cobra's setter calls the firmware
`process_write_memory(..., 1)` wrapper, but the inspected source does not
establish whether that firmware function invalidates the game's instruction
cache. The `clear_icache` calls in Cobra's executable-page allocator apply to
its temporary kernel mapper patch, not the newly allocated process page.
webMAN's payload helper writes binary chunks through `SET_PROC_MEM` before
thread creation, but it does not verify exact bytes or cache visibility.
Therefore a GETMEM readback can prove the bytes were stored, not that the
processor executed them. [webMAN GETMEM editor](https://github.com/aldostools/webMAN-MOD/blob/45025987231248e4939a9f00ef851aa4aa0a5345/include/ps3mapi/ps3mapi.h#L751-L981),
[Cobra memory implementation](https://github.com/xXEvilnatXx/Cobra-PS3/blob/main/8.5/4.93/CEX/SRC/stage2/ps3mapi_core.c),
[webMAN payload helper](https://github.com/aldostools/webMAN-MOD/blob/45025987231248e4939a9f00ef851aa4aa0a5345/include/ps3mapi/ps3mapi_payload.h).

The 1.47.48t fix changed the generated thread descriptor from `u64[2]` to
`u32[2]`; the commit notes that the previous form could start at address zero
and crash. These are implementation details of particular webMAN/Cobra
sources, not proof that a user's installed versions support the full lifecycle.
[Command reference](https://github.com/aldostools/webMAN-MOD/wiki/Web-Commands/e75ae66ee03f1ec8d61ad011e56b62e0a3559bab),
[fix commit](https://github.com/aldostools/webMAN-MOD/commit/75327b1).

No toast stub is exposed. A future probe would need a live build/process check,
nonzero allocation values, exact code readback, and a harmless stub that writes
a completion marker and returns, followed by marker readback. It would then
need to call `PAGEFREE` with both retained values. The current HTTP handler
cannot confirm that freeing succeeded, so cleanup behavior must be validated
separately before repeated use. Instruction-cache behavior, toast-function
thread affinity, and observed toast output also remain unverified; no
live-console writes were made for this audit.

Matching static code/data bytes do not prove server persistence. Controlled
observation in the matching 01.24 runtime is needed to confirm dynamic mappings
and in-game effects.

## Reading the memory map

The tools send addresses to the Destiny process (`0x1010200`) through PS3MAPI;
four-byte values are decoded as big-endian PPC data. For the executable segment,
virtual address `A` maps to ELF file offset `A - 0x00010000`; for example,
`0x004449EC` is file offset `0x004349EC`. Its default bytes and the nearby code
signature can therefore be checked in the exact EBOOT, and disassembly can show
which code reads it. This verifies a candidate against the build, not the named
gameplay effect.

The writable segment's file-backed data ends at `0x019013A4`; the rest of that
segment is BSS, zero-filled in the ELF. Roots such as `0x0190EBB0` are expected
to be empty in the file and initialized at runtime, so their live pointer value
must be read before following the chain. Addresses used by local RTMs in
`0x600...` are outside the EBOOT load segments; the ELF gives no allocation or
contents for them. Those require fresh live reads while the matching game is
running. A pointer read and its target fields are not one atomic snapshot, so
write tools should revalidate identity and mapping immediately before a write,
then verify the exact bytes afterward.

## Repeatable local audit

The read-only helper at `tools/rtm_elf_audit.py` hashes an ELF, parses its
big-endian load segments, reads candidate virtual addresses, and searches
executable segments for aligned byte signatures. For the recovered game copy,
run:

```powershell
python tools/rtm_elf_audit.py 'E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF' `
  --search-signature 38C6FFFF `
  --search-signature 38C0FFFF `
  --search-signature 38C60FFF `
  --search-signature 38C00FFF
```

It reports whether the input hash is the verified 01.24 build, the bytes at
known source sites, and the count plus first 32 aligned matches for each
signature. Pass `--max-signature-hits 0` to print every match; JSON output
always contains every match. The helper does not decrypt SELF files, disassemble
instructions, or inspect a running game; signature hits still need context
before they can identify a feature. Run its synthetic parser checks with
`python -m unittest discover -s tests -p test_elfaudit.py -v`.
