# Destiny RTM source comparison

This port compares the current web app with the newer local 1.24-style HTML
builds (`D:\DestinyRTM\index.html` and `D:\DestimyRTM\index.html`) and the
matching local C# RTM sources under `D:\Soul rtm`. The older 1.08 and phone
builds use different offsets and were not used as address references.

## Ported or completed

| Feature | Result |
|---|---|
| Brightness presets | Added the local 1.24 C# Bright `50`, Normal `1`, and Inverted `-20` values at `0x006101C0`. Writes require exact readback; the matched ELF default is `1.0`, but the in-game effect remains unobserved. |
| No Spread, Bouncy Items, 4× Jump Boost, Rock Jump | Added from the local 1.24 C# source. Overlapping patches disable the other active option before a write. |
| Unlimited Ammo | Corrected the main toggle to the local 1.24 C# three-site patch at `0x003108EC`, `0x00310554`, and `0x00CF95AC`. Enable and reset both verify every patched byte. The former `0x00310F94` target did not contain the UI's claimed OFF instruction in the matching ELF. |
| Memory Scanner | The scan/diff code already existed; its missing controls are now available in Tools. Reads remain bounded and writes require readback. |
| PS3 notification | Available as a PS3 system notification through webMAN MOD's documented notification route. It is separate from the old Destiny-internal toast. |
| PS3 Jukebox | Ported all six tone sequences from `C:\Users\novel\Downloads\DestinyRTM1.0.html` (tone IDs and pauses preserved). The source UI exposed only Imperial March and SOS; this app exposes the other four source-defined sequences too, prevents overlapping playback, tracks progress, and stops between system-beep requests. It only calls `/beep.ps3` and does not write Destiny memory. |
| Character emblem | Added the 103 entries in the local C# `itemList.cs`; the live scan must find exactly one aligned match before a write. Both writes are read back; a saved recovery stack restores verified partial or completed changes on the same console while each value remains original or matches the intended write. |
| Ammo Editor | Reworked as a read-only, chunked 4 MiB scan. Primary/secondary/heavy use the source's 8-byte reserve/magazine signature and `0x80A6` marker; special ammo uses its Float32 signature, `01 00 00 00` suffix, and `0x8080` marker at `+40`. It lists candidates and requires selecting one address. Writes are verified, guarded rollback is attempted on partial failure, and successful edits can be restored in reverse order while each slot still matches. |
| Screenshots | The five local C# capture profiles now have independent web gallery slots with per-profile preview, PNG/JPEG download, PNG clipboard copy, and Fit/Stretch controls. Re-capturing one profile replaces only its prior image. Reads are chunked. Optional pause capture requires persisting the original pause byte before writing and restores it in `finally`; if that fails, the app retains a manual recovery action and blocks other connection changes until recovery. |
| ISO game launch | Added the source sequence from the newer local RTM: mount `/dev_hdd0/PS3ISO/<file>.iso` with `mode=silent`, wait 3 seconds, return to `/wmnav.ps3/game`, wait 1 second, then request `/play.ps3`. The ISO path is restricted to a filename directly inside `PS3ISO`. Each HTTP response is reported by stage; it does not confirm Destiny started. The desktop MCP tool uses the same sequence. |
| Player Size | Ported the local C# name-to-object lookup and Float32 scale formula for the `0.3`, `1`, `2`, and `20` source values. The app and MCP tool check the 01.24 resolver/helper signatures, emulate the lookup with reads instead of calling the game function, recheck identity and pointer mapping before writes, and require exact scale readback. In-game effect and persistence remain unconfirmed. |
| Damage preset label discrepancy | Overwalls C# and the legacy HTML toggle label the `0x0043E626` `0x4120` halfword “4x Damage,” while that HTML calculator maps the same bytes to its “10×” preset. The app exposes one shared “Damage Preset” control and explains both source labels. The 01.24 build confirms the patch site/OFF bytes, not the actual multiplier. |
| Experimental Scalar Preset | The recovered RTM's two source labels target the same literal-pool Float32 at `0x004449EC`: Normal `3F800000`, “Max Damage” `3FFFFFFF` (~2), and “1 Hit Kill 2” `42C80000` (100). In 01.24, four callers initialize a 0x140-byte record through `0x0044CAB0`, which sets `+0x11C` to 1.0, then call the consumer at `0x00444A00`. It multiplies `+0x11C` by a ratio, or by the literal-pool scalar when the denominator is below ~0.0001. No direct caller read establishes what `+0x11C` represents, so the source gameplay labels remain unverified. The experimental app/MCP write requires the exact `0x00444B64` code signature, known current values, and readback. |

All app memory patches, the desktop `/console` write route, MCP write tools,
and the Go memory client now require the 01.24 runtime signature profile before
the first write in an operation. This profile detects the known local 01.00
build mismatch but does not establish full executable hash or title-region
identity. Read-only memory tools remain usable on other builds.

## Not exposed as working controls

- The Destiny-internal toast depends on
  `soulsAPI.Extension.Call`. The local `soulsAPI.dll` implements this on top of
  memory reads/writes by installing a 136-byte PowerPC stub at `0x004A8040`,
  clearing a shared 10,324-byte mailbox, and invoking calls through staged
  memory. Its `Call` implementation uses a shared mailbox, sleeps a fixed 20 ms,
  and has no completion polling or title/version checks. The DLL has no
  accompanying license in the recovered project. The DLL and extracted machine
  code are not included in this repository. Player Size no longer uses this
  RPC path. Stock webMAN also
  does not implement the HTML copy's `/call.ps3mapi` and
  `/enablerpc.ps3mapi` routes. The phone build initializes `0x10030000`, writes
  UTF-16BE title/body strings at `0x10030200`/`0x10030400`, then calls
  `0x00748714` with buffers at `0x01B300E8` and `0x01B2FF58`. In the verified
  BLES01857 01.24 ELF, the called functions at `0x00750540` and `0x00748714`
  are executable and the latter buffers are zero-filled BSS, but
  `0x10030000` is outside every mapped ELF segment. Its requested RPC-stub
  address `0x004A8040` contains executable game code. These facts do not
  establish usable scratch memory or safe call semantics, so the in-game toast
  remains a research candidate; the existing webMAN system notification is a
  different feature.
- Trophy unlock calls the same unsupported function-call layer and has no
  source-level readback or restoration. It remains unavailable.
- Deathsinger's Power appears in
  `D:\VIRUS\1111111111111PhoneRTMtest\DestinyMobileBETA_2.html`, and this source
  tree contains an old scan/toggle implementation, but it remains hidden. Its
  intended change is PowerPC `addi r6,r6,-1` (`38C6FFFF`) to `addi r6,r6,4095`
  (`38C60FFF`), replacing a counter decrement with a large increment. In the
  instruction encoding, the low 16 bits are a signed immediate: `FFFF` means
  `-1`, while `0FFF` means `+4095`. This explains the patch's arithmetic; it
  does not identify which encounter instruction should be changed. The
  recovered scan accepts the first match of generic instructions without
  requiring instruction alignment. In the hash-verified 01.24 EBOOT the first
  scan hit is `38C0FFFF` at `0x00011670`, in the first `0x10000–0x14000` chunk;
  PPC context identifies it as an initialization sentinel set to `-1`, not an
  encounter counter. The EBOOT has seven aligned `38C6FFFF` sites, at
  `0x009AAB00`, `0x009AC35C`, `0x00A730BC`, `0x00B1B76C`, `0x00D743D8`,
  `0x010923CC`, and `0x0109248C`; the earlier list omitted the last two. The
  first two occur in divide/remainder indexing loops; `0x00A730BC` is in an
  array-copy routine; `0x00B1B76C` decrements a list count before indexing its
  records; `0x00D743D8` feeds the value to a bit shift; and the two `0x0109*`
  sites are in size/alignment arithmetic. None is identified as the encounter
  deposit handler. The `38C0FFFF` alternative has 1,112 raw file occurrences
  (1,111 four-byte-aligned); the first, at `0x00011670`, is the initialization
  sentinel above. The reset fallback at `0x001C7B58` also points at unrelated
  01.24 code bytes. A separate `38C00FFF` word exists at `0x006AEFEC`, but no
  caller or encounter context ties it to this patch. The scan searches OFF
  words only, so it cannot rediscover an already patched word. The
  encounter-specific address therefore remains unknown. Locating it requires
  encounter-specific runtime evidence or source/disassembly that identifies
  the function.
- `D:\VIRUS\PDR.html` has a “Player Hook” and “True Death” action. The True
  Death write is already ported as **PDR position** in the Overwalls tab:
  `source/js/overwalls-tools.js` bounds the pointer read from `0x0190EBB0` and
  verifies the Float32 writes `47C35000` at `+0x148` and `3F800000` at `+0x1B0`.
  The related PDR + Static Actors off action is ported there as well. Neither
  operation's in-game result has been observed. The PDR **Player Hook** is a
  separate variant: it redirects `0x001C4110` to a 46-word code block at
  `0x010D8EE8`; the existing Overwalls physics hook and Server Cheats use that
  same branch site and a 116-byte block beginning at `0x010D8EC0`. Those cave
  ranges overlap from `0x010D8EE8` through `0x010D8F33`, so PDR's hook cannot be
  added as an independent toggle. It also writes `3F99999A` at `0x0190EBAC`,
  a field used by the existing hook, and performs no readback or restoration.
  This alternate hook remains unavailable until its interaction with the
  existing hook is modeled and safely reversible.
- The main web-RTM Warpriest Immunity variant is present in local HTML, but
  its source OFF values (`C0434DB0` at `0x00444CA4` and `3F800000` at
  `0x01897420`) do not match BLES01857 01.24 (`C04100F0` and `37343036`). The
  main control and reset entry are blocked for this build. A separate Overwalls
  C# variant uses the matching `C04100F0` primary OFF instruction, but sets
  `0x01891AE0` to `3F800000` only on enable. The C# source does not restore that
  BSS word on disable, so its purpose and runtime behavior remain unconfirmed.
- The local C# client archive collects other players' IP addresses and
  location/provider details. That collection was not ported.
- The newer local RTM's launch sequence is available for a single ISO in
  `/dev_hdd0/PS3ISO/`; the source's `no-cors` requests could not verify stage
  responses. The desktop port reports HTTP acknowledgements but cannot confirm
  the game booted.
- Screenshot framebuffer addresses and the other recovered game offsets come
  from local source; they have not been checked against a live console build.
  No live console testing was performed.

Static comparison against the hash-verified 01.24 ELF confirms the source's
Brightness default, FOV field, Flip View address/default, Power Boost default,
and One-Hit OFF halfword at their candidate sites. The local 1.24 C# and web RTM
sources agree on FOV and Flip View addresses. Power Boost ON values still differ
slightly between source variants, and One-Hit retains two competing ON values
(`447A` and `547A`). Static bytes establish build compatibility at these sites,
not live runtime effects. Brightness now exposes the source presets through
readback-verified writes; its in-game effect remains unobserved. See
[ADDRESS-AUDIT.md](ADDRESS-AUDIT.md) for hashes, bytes, and runtime limitations.

The local source establishes provenance, not build compatibility. Public
webMAN documentation supports memory reads/writes and the PS3 notification
route, while Bungie's legacy support page identifies the retained PS3 release
as Update 2.3.1. These references do not independently validate Destiny RTM
addresses:

- [webMAN MOD web commands](https://github.com/aldostools/webMAN-MOD/wiki/Web-Commands)
- [webMAN MOD PS3MAPI HTTP router](https://github.com/aldostools/webMAN-MOD/blob/master/include/cmd/ps3mapi.h)
- [Bungie legacy Destiny support](https://help.bungie.net/hc/en-us/articles/360048720052-Installing-Reinstalling-and-Updating-Destiny-on-the-PlayStation-3-Legacy-Console)
