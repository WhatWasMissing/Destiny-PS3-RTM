# Destiny RTM source comparison

This port compares the current web app with the newer local 1.24-style HTML
builds (`D:\DestinyRTM\index.html` and `D:\DestimyRTM\index.html`) and the
matching local C# RTM sources under `D:\Soul rtm`. The older 1.08 and phone
builds use different offsets and were not used as address references.

## Ported or completed

| Feature | Result |
|---|---|
| No Spread, Bouncy Items, 4× Jump Boost, Rock Jump | Added from the local 1.24 C# source. Overlapping patches disable the other active option before a write. |
| Memory Scanner | The scan/diff code already existed; its missing controls are now available in Tools. Reads remain bounded and writes require readback. |
| PS3 notification | Available as a PS3 system notification through webMAN MOD's documented notification route. It is separate from the old Destiny-internal toast. |
| Character emblem | Added the 103 entries in the local C# `itemList.cs`; the live scan must find exactly one aligned match before a write. Both writes are read back; a saved recovery stack restores verified partial or completed changes on the same console while each value remains original or matches the intended write. |
| Ammo Editor | Reworked as a read-only, chunked 4 MiB scan. Primary/secondary/heavy use the source's 8-byte reserve/magazine signature and `0x80A6` marker; special ammo uses its Float32 signature, `01 00 00 00` suffix, and `0x8080` marker at `+40`. It lists candidates and requires selecting one address. Writes are verified, guarded rollback is attempted on partial failure, and successful edits can be restored in reverse order while each slot still matches. |
| Screenshots | Added the five framebuffer profiles and PNG download. Reads are chunked. Optional pause capture requires persisting the original pause byte before writing and restores it in `finally`; if that fails, the app retains a manual recovery action and blocks other connection changes until recovery. |

## Not exposed as working controls

- Player Size and the Destiny-internal toast depend on `soulsAPI.Extension.Call`.
  Stock webMAN MOD's HTTP PS3MAPI router does not include the generic
  `/call.ps3mapi` route. The previous experimental Player Size UI and
  connection-time RPC request have been removed.
- Trophy unlock calls the same unsupported function-call layer and has no
  source-level readback or restoration. It remains unavailable.
- The local C# client archive collects other players' IP addresses and
  location/provider details. That collection was not ported.
- Screenshot framebuffer addresses and the other recovered game offsets come
  from local source; they have not been checked against a live console build.
  No live console testing was performed.

Several local source candidates conflict: Brightness, FOV, Flip View, Power
Boost and One-Hit Enemies. A local ELF previously labelled `PATCH0024` has a
hash matching Destiny 01.00, not 01.24, so its disassembly cannot validate or
reject these 01.24 source claims. Brightness remains a read-only probe; the
other conflicts remain unresolved pending a correctly identified 01.24 ELF and
runtime evidence. See [ADDRESS-AUDIT.md](ADDRESS-AUDIT.md) for the corrected
build identity and next verification step.

The local source establishes provenance, not build compatibility. Public
webMAN documentation supports memory reads/writes and the PS3 notification
route, while Bungie's legacy support page identifies the retained PS3 release
as Update 2.3.1. These references do not independently validate Destiny RTM
addresses:

- [webMAN MOD web commands](https://github.com/aldostools/webMAN-MOD/wiki/Web-Commands)
- [webMAN MOD PS3MAPI HTTP router](https://github.com/aldostools/webMAN-MOD/blob/master/include/cmd/ps3mapi.h)
- [Bungie legacy Destiny support](https://help.bungie.net/hc/en-us/articles/360048720052-Installing-Reinstalling-and-Updating-Destiny-on-the-PlayStation-3-Legacy-Console)
