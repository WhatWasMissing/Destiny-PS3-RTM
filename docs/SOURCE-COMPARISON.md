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
| Character emblem | Added the 103 entries in the local C# `itemList.cs`; the live scan must find exactly one aligned match before a write. Both writes are read back; a saved recovery stack restores verified partial or completed changes on the same console while each value remains original or matches the intended write. |
| Ammo Editor | Reworked as a read-only, chunked 4 MiB scan. Primary/secondary/heavy use the source's 8-byte reserve/magazine signature and `0x80A6` marker; special ammo uses its Float32 signature, `01 00 00 00` suffix, and `0x8080` marker at `+40`. It lists candidates and requires selecting one address. Writes are verified, guarded rollback is attempted on partial failure, and successful edits can be restored in reverse order while each slot still matches. |
| Screenshots | Added the five framebuffer profiles and PNG download. Reads are chunked. Optional pause capture requires persisting the original pause byte before writing and restores it in `finally`; if that fails, the app retains a manual recovery action and blocks other connection changes until recovery. |
| ISO game launch | Added the source sequence from the newer local RTM: mount `/dev_hdd0/PS3ISO/<file>.iso` with `mode=silent`, wait 3 seconds, return to `/wmnav.ps3/game`, wait 1 second, then request `/play.ps3`. The ISO path is restricted to a filename directly inside `PS3ISO`. Each HTTP response is reported by stage; it does not confirm Destiny started. The desktop MCP tool uses the same sequence. |

## Not exposed as working controls

- Player Size and the Destiny-internal toast depend on
  `soulsAPI.Extension.Call`. The local `soulsAPI.dll` implements this on top of
  memory reads/writes by installing a 136-byte PowerPC stub at `0x004A8040`,
  clearing a shared 10,324-byte mailbox, and invoking calls through staged
  memory. Its `Call` implementation uses a shared mailbox, sleeps a fixed 20 ms,
  and has no completion polling or title/version checks. The DLL has no
  accompanying license in the recovered project. The DLL and extracted machine
  code are not included in this repository; the previous experimental Player
  Size UI and connection-time RPC request remain removed. Stock webMAN also
  does not implement the HTML copy's `/call.ps3mapi` and
  `/enablerpc.ps3mapi` routes.
- Trophy unlock calls the same unsupported function-call layer and has no
  source-level readback or restoration. It remains unavailable.
- Deathsinger's Power appears in a recovered mobile RTM, and this source tree
  contains an old scan/toggle implementation, but it remains hidden. Its
  intended change is PowerPC `addi r6,r6,-1` (`38C6FFFF`) to `addi r6,r6,4095`
  (`38C60FFF`), replacing a counter decrement with a large increment. The
  recovered scan accepts the first exact match of generic instructions. The
  hash-verified 01.24 EBOOT has seven aligned `38C6FFFF` sites, at
  `0x009AAB00`, `0x009AC35C`, `0x00A730BC`, `0x00B1B76C`, `0x00D743D8`,
  `0x010923CC`, and `0x0109248C`; `38C0FFFF` occurs over a thousand times.
  PPC disassembly identifies those sites as copy loops, array/list bookkeeping,
  or bit/size calculations. The first match is a byte-copy loop; the closest
  counter-like match is an array count used to index 32-byte records. None is
  the encounter deposit handler. The encounter-specific address therefore
  remains unknown; locating it requires matching encounter runtime evidence or
  source/disassembly that identifies the function.
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
