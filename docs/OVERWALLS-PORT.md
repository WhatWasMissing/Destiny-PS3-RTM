# Overwalls feature port

The Overwalls features in `source/js/overwalls-tools.js` are based on the local
Overwalls Tool decompilation at `E:/Destiny PS3 RTMs/fly hack/OverwallsTool -
Hybrid/AFKCrucible/Form1.cs`, its `ButtonData` model, the accompanying scenario
XML files, and the local SPRX Retrofit decompilation. The port keeps the
behavior and address map while using Destiny RTM's JavaScript interface,
verified memory transport, saved settings, and cancellable routines. The C# UI
and soulsAPI reflection code are not bundled.

## Feature coverage

| Overwalls feature | Destiny RTM port |
|---|---|
| Tower, vendor, off-map, Valus, engram and arena destinations | Overwalls tab coordinate shortcuts with source activation modes |
| Position read, PDR, above-player action, OPK teleport and offset presets | Dedicated controls using big-endian float writes and session checks; the MCP server also exposes a read-only player-position tool following pointer `0x0190EBB0` and offsets `+0x140/+0x144/+0x148` |
| Named shortcut grid and XML scenarios | Import, edit, delete, export, local persistence and settings backup for `ArrayOfButtonData` |
| Activity favorites and current index | Source favorites, random Tower index 2–7, index read/write, current four-address loader or source single-address loader |
| Crucible farm | Four timing profiles, faction destinations, Tower selection, 12/12/11 match pattern, stop control and repeat-until-stopped option |
| Taken Archon Priest, Valus, bullets and Echo routines | Sequential source routes with interruptible waits and session checks |
| Totems | Repeatable two-position loop plus all six source Mid/Right/Left route shortcuts, each stoppable |
| Character name, Glimmer, Silver and Marks | Read and selective write with memory readback; does not claim server persistence. The emblem scan excludes the wallet fields and `0x0366` marker so currency bytes cannot be mistaken for an emblem ID. |
| Internal mode, relative OPK, Void Enemies, Warpriest start | Explicit controls; plugin-dependent actions require both Retrofit signatures |
| Flyhack settings and SPRX checks | Enable, mode, velocity, displacement, deadzone, debounced sliders and signature checks |
| SPRX module loading | Fixed-path module-load request through authenticated loopback host or same-origin webMAN, followed by readiness polling |
| Physics and plate hooks | Manual apply with captured prior bytes and guarded restoration; hook site must match known branch values |
| Split rep boosters and consumable randomization | Source values plus local byte snapshots and guarded restore controls |
| Individual source toggles and usual/all/off groups | Source toggles and three grouped presets translated to verified writes, including the 20-byte ammo patch and the additional address used by one preset. The Overwalls C# Warpriest variant restores its source-backed primary instruction; its secondary BSS word remains set after disable, as in the source. |
| One-Hit damage variant | Keeps the Overwalls `547A` value at `0x43E626` and synchronizes the main RTM's One-Hit and damage status only after verified writes; the app does not interpret this variant as a multiplier |
| Skip cutscene, activity ID sweep and CCAPI memory read | Source address/value, cancellable sweep and 4 KB read in eight 512-byte chunks |

The main RTM already contained equivalents for core gameplay toggles, standard
teleporting, FOV, game speed, current activity loading, anti-matchmaking, and
player/server hook controls. Those remain in their existing sections. The
Overwalls tab carries source-specific behavior and selectable source loading
where the existing implementation differs.

The source's empty click handlers have no behavior to port. Its tooltips and
soulsAPI API-number selector are Windows Forms details; the browser uses one
HTTP transport and debounces fly-setting sliders. The Retrofit loader's
reflection scan is replaced by webMAN's documented PS3MAPI `MODULE LOAD`
command and verification of both runtime markers. A returned load response by
itself is not reported as successful.

Fixed coordinates, memory addresses, hook bytes and plugin markers remain tied
to the Overwalls source build. The interface verifies memory writes but does
not confirm in-game behavior or online persistence. Hook snapshots are kept in
browser storage per console and are excluded from settings exports. No live PS3
was used to validate these new operations.

## Attribution and license

Feature behavior is credited to the Overwalls Tool and its SPRX Retrofit in the
user-provided local source collection. The repository does not claim authorship
or ownership of those original tools. Destiny RTM's repository
proprietary license does not claim ownership of Overwalls material, PS3MAPI,
webMAN MOD, Destiny names or third-party assets; applicable rights remain with
their respective owners. The repository's no-redistribution terms apply only
to material owned by WhatWasMissing.
