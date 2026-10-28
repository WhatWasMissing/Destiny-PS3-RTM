# Blackrock feature port

Blackrock's release targets Destiny **NPEB02021 01.24**. This RTM targets
**BLES01857 01.24**. The inspected Europe ELF includes both title IDs and the
same build string Blackrock documents (`51539.16.07.26.1808.v230`), tying the
analyzed binaries to the same release build. A successful PS3MAPI connection
confirms console transport but does not identify the live game's build. Ported
memory features still use BLES evidence and the RTM's verified runtime profile.

## Ported in this pass

| Feature | RTM implementation | Scope |
|---|---|---|
| Activity and session snapshot | `source/js/activity-session-inspector.js` | On-demand reads behind a fresh 5/5 BLES01857 01.24 signature check; bounded PS3MAPI reads; session and pointer rechecks; explicitly non-atomic. No polling or writes. The resolver algorithm is GPL-2.0-only, ported from the D1 Reimagined source repository. |
| Resource / bootflow snapshot | `source/js/resource-bootflow-snapshot.js` | Reads BLES 01.24 bootflow, eight resource slots, scheduler fields, and readiness masks behind a fresh build-profile check. Dynamic pointers are bounded; it does not follow readiness pointers or asset handles. Rechecks sampled state before displaying. Read-only, sequential, non-atomic, and explicitly not a tag-loader queue view. |
| Activity pool diagnostics | Included in the session snapshot | BLES 01.24 initializer evidence supplies the three descriptor addresses, labels, element sizes, and capacities. `0x01A20B70` and `0x01A20BF0` are raw-only; only `0x01A20C70` has resolver-backed field labels. The feature does not traverse pool storage beyond the guarded context-handle resolver. |
| Local player appearance probe | `source/js/player-appearance-probe.js` | Uses the BLES 01.24 player-pool header and sparse bitmap, requires stride `0x2930`, caps scans at 64 slots, matches the local descriptor transiently, rechecks all captured bitmap words, and returns two short raw windows only for one stable match. It never displays owner bytes and performs no writes. |
| Live item-object pool snapshot | `source/js/live-inventory-pool.js` | Requires the 5-anchor profile plus exact BLES 01.24 manager-getter and pool-lookup signatures. It validates the self-family kind-3 descriptor (`0x80801732`, stride `0xA8`, signed count 1–646), applies conservative Blackrock-derived range ceilings, reads in 18-chunk batches under fresh session capabilities, then rechecks the descriptor and boundary rows. It extracts the raw item index at object `+0x24`. An explicitly BLES-indexed user catalog can add name, bucket, and type labels; the pool lookup at `0x0047E3CC` is by family and instance ID, not item index. No 01.24 index-to-name resolver or live item-list pointer is established. Read-only and explicitly non-atomic. |
| Live item-pool comparison | `source/js/live-inventory-pool.js` | Keeps the latest two complete samples in page memory and compares only pool slot plus item index. Comparison is enabled only when the RTM session token, manager/descriptor, pool address, and size match. SOIDs and owners are never returned or displayed. Samples remain sequential/non-atomic; same-index replacement, ownership, transfer, and persistence cannot be distinguished or proven. |
| Spawn-path code check | `source/js/spawn-path-check.js` | Compares three BLES 01.24 authored-member signatures, Blackrock's confirmed `0x354074` hook-site instruction, the object-creation entry and manager-registration continuation at `0x13ED38` / `0x13ED90`, and the main ELFv1 descriptor/TOC. This identifies matching code only; it does not verify a selected tag's live class, prove descriptor/placement behavior, or execute the path. |
| Spawn executor region preflight | `source/js/spawn-region-preflight.js` | Reads the exact zero-only bootstrap ranges (`0x183E000/0x2000`, `0x1C74000/0xC000`, `0x183D200/0xE00`) behind fresh BLES 01.24 capabilities. Logical reads are split into PS3MAPI GETMEM requests no larger than `0x100` bytes. It does not reserve the memory, install the hook, or prove the regions stay free. |
| Reconstructed spawn images | `source/js/spawn-executor-images.js` | Embeds the audited 2,908-byte spawn program and 292-byte bootstrap as immutable image factories. SHA-256 and byte-for-byte checks compare them with the generated research binaries. |
| Experimental operation-1 spawn controller | `source/js/spawn-executor-controller.js`, `source/js/spawn-executor-runtime.js` | User-triggered install, queue-slot selection, submit/poll/cancel, and compare-before-restore recovery. Requires a fresh BLES 01.24 profile, all three scratch ranges zero, and a persistent recovery journal; hook activation occurs last. Queue reads and writes are not atomic reservations, so another Blackrock or RTM spawn client must not run concurrently. If hook activation may have happened, recovery restores the hook but leaves the task entry, dispatcher header, code and data resident until Destiny restarts, even if no spawn request was sent. Confirmed pre-activation failures can clean exact owned bytes. PPC cache behavior, tag/class matching, actual creation, and placement remain unverified on a live console. |
| Spawn-tag resolver probe | `source/js/spawn-tag-resolver-probe.js` | Requires the 5-anchor profile plus exact BLES 01.24 signatures for the 100-byte packed-handle helper at `0x0013B7E8` and the 16-byte constructor copy at `0x0013EE1C`. It reads/rechecks the handle table and definition byte `+0x4E`, which the constructor copies to object `+0x08`; it can compare that byte with the optional Blackrock expected-type guard. Read-only; it does not prove the tag is loaded, identify hostility, or confirm spawn success. See [ADDRESS-AUDIT.md](ADDRESS-AUDIT.md#bles-0124-packed-handle-resolver-used-by-spawn) and [spawn type evidence](../research/spawn-type-evidence.md). |
| Actor lookup descriptor probe | `source/js/actor-lookup-descriptor-probe.js` | Verifies the five-anchor BLES profile and an exact 64-byte actor-path signature at `0x00255FA8`, then reads/rechecks only the 20-byte descriptor at `0x01A1E160`. Shows its raw leading words, storage pointer, stride, and capacity; checks active dimensions against capacity 40 and stride `0x28D0`; never follows storage. It accepts only the address spans used by Blackrock's recovered host reader. See [ADDRESS-AUDIT.md](ADDRESS-AUDIT.md#bles-0124-actor-lookup-descriptor-path) and [the reproducible PPC evidence](../research/actor-pool-descriptor.md). |
| Allocated actor-candidate snapshot | `internal/rtmclient/actor_pool.go`, CLI `rtmctl actor-candidates`, MCP `rtm_read_allocated_actor_candidates` | Verifies the five-anchor BLES profile plus exact datum/allocator and `+0x226C` caller/resolver signatures, requires a stable non-null player pointer, follows the `0x34`-byte descriptor → allocator header → bitmap descriptor → bitmap words, and reads at most 40 allocated slots. Preserves the full reconstructed datum and selected raw fields from each first `0x48` bytes. Samples/rechecks the raw nested handle and reports decoded bucket/slot arithmetic but never follows it. Remains sequential/non-atomic; it identifies allocated candidates, not enemies or spawnable tags. The `+0x30` word is a unit-datum candidate; `+0x24/+0x28` are opaque, and `+0x40/+0x44` are raw member-owner fields. It does not inspect the separate cleanup bitmap. |
| Offline inventory snapshot viewer | `source/js/offline-inventory-snapshot.js` | Reads explicitly selected BLES 01.24 QZ payload files (28,104-byte character or 28,768-byte account). It validates the big-endian type hash (`EB4B8439` character; `B5DF2907` account) and body size, then shows character slot and item index, with optional name, bucket, and type labels from a matching user-imported BLES-indexed catalog. Header SOID bytes are skipped; row instance IDs are reduced to a transient empty/nonempty check and never surfaced or persisted. Account +0x4C0 appears only as an unverified vault-like candidate. No live memory, writes, actions, network, or persistence. |
| Offline inventory comparison | `source/js/offline-inventory-comparison.js` | Compares two explicitly selected character QZ payloads by slot and item index, and reports added, removed, and changed rows. Instance and owner identifiers remain hidden; a different instance with the same item index is indistinguishable. This is a read-only before/after aid, not inventory mutation. |
| Verified equipped-position labels | `source/js/offline-inventory-snapshot.js` | Labels only the 17 BLES 01.24 equipped inventory starts proven by the bucket-range and equipped-slot tables. Nine have known subclass/armour/weapon names; the other eight remain numbered. Every non-start row keeps its numeric inventory slot. |
| Local catalog browser | `source/js/local-catalogs.js` | Imports user-selected CSV, TSV, JSON, and text catalogs in page memory, with per-file, total-size, and row caps. Supports activity, spawn/hop-on, item/loot, and world-data categories. Spawn rows preserve a bounded numeric `type` as unverified metadata; selecting a row copies its tag, and a separate explicit action can copy that number to the experimental expected-type guard. The catalog-to-BLES guard mapping remains unverified. Inventory labels use an explicitly BLES-indexed `itemIndex` or `blesItemIndex` column only. The Blackrock `def` ordinal and unverified `hash`/`tag` joins are ignored. No catalog data or executable code is bundled. |

The extracted BLES 01.24 item catalog was imported through the production
catalog parser: all 6,389 named rows loaded without truncation and supplied
6,389 in-range item-index labels. The file remains outside the repository and
release packages; this verifies item-name display for the read-only snapshot
tools, not inventory mutation. See the [catalog import audit](../research/item-catalog-load-audit.md).

## Already present in the RTM

The actor-pool descriptor and allocated-candidate snapshots are also available
without the desktop UI through `rtmctl actor-pool` / `rtmctl actor-candidates`
and MCP tools `rtm_read_actor_pool_descriptor` /
`rtm_read_allocated_actor_candidates`. The descriptor tool reads only the
20-byte descriptor. Candidate enumeration follows the BLES allocator bitmap,
preserves full datums, reads selected words from the first `0x48` bytes, and
reports the raw `+0x226C` packed handle with its decoded bucket/slot. Static
analysis shows that helper resolves the handle through a second manager, but
the reader does not follow it or identify its target class. Both tools are
read-only; candidate rows do not confirm enemy status, spawn tags, or
process-instance continuity.

The current app already has activity loading, teleport and scenario routines,
character name/emblem/scale utilities, ammo and cheat controls, Overwalls
consumable routines, screenshot capture, memory scanning/dumping, and
camera/render controls. The local catalog browser can browse catalogs supplied
by the user, including their own extracted or generated data.

## Still missing

The main Blackrock feature groups not yet ported are tag-loader/resourcerer
diagnostics; activity-deploy mirror scans; live hop-on, encounter, squad, and
additional spawn actions; broad inventory/vault editing; loot-event and Grimoire tools;
faction matrices; world mutations and hooks; component/facet/effect probes;
and broader player/loadout/stat inspection. Blackrock's package data does not
prove these features' addresses or layouts apply to BLES01857.

The live and offline inventory comparisons improve before/after snapshot review
but do not close the inventory mutation gap. No BLES 01.24 item-action handler,
transaction format, or persistence/readback path has been verified.

The hash-matched BLES ELF now confirms Blackrock's `actors` descriptor at
`0x01A1E160` as `AI Actor Manager + 0x10`, initialized with capacity 40 and
PS3 stride `0x28D0`; Blackrock's row records `ElemSize=0x28E0` while the PS3
initializer supplies `0x28D0`. The
descriptor's backing pointer is allocated dynamically through a shared helper.
This resolves the pool identity and dimensions, but not whether records are
loaded enemies. A separate allocator bitmap and full-datum reconstruction have
now been traced in the BLES EBOOT and Blackrock. The candidate reader uses the
allocator bitmap and reports the nested handle at `+0x226C`; its target class
and the separate `+0x30` cleanup-bitmap semantics remain unresolved, as does
live actor classification. See
[actor-pool allocation evidence](../research/actor-pool-allocation-evidence.md).
This static
result still does not establish a generic BLES spawn entry point or safe spawn
arguments beyond the experimental Blackrock operation-1 path, which has not
been tested on a live console. See [the descriptor initializer trace](../research/actor-pool-descriptor.md).
Blackrock's hop-on
catalog likewise does not prove a BLES tag/type crosswalk. The BLES authored
Ghost path depends on a live component and the game's state-application /
replication flow. PS3MAPI supports executable page allocation and `THREAD
CREATE`, but that creates a separate PPU thread whose entry is assumed to be
the first function in the page; it does not schedule a callback on Destiny's
update thread. The current RTM does not expose those commands, and a separate
thread does not establish that the authored-member route is safe to call there.
The documented Ghost route cannot be exposed as a working button yet. See the
[upstream PS3MAPI page-allocation handler](https://github.com/aldostools/webMAN-MOD/blob/master/include/ps3mapi/ps3mapi_server.h#L3417-L3522)
and [thread-creation handler](https://github.com/aldostools/webMAN-MOD/blob/master/include/ps3mapi/ps3mapi_server.h#L3818-L3917).
The unbound count-only route is not included. A new read-only diagnostic
compares the three authored-member signatures, Blackrock's executor hook-site
instruction, and the ELFv1 TOC; it does not validate live spawn state. The
separate experimental operation-1 controller can submit Blackrock's emitted
program, but has no live-console success evidence yet.

Blackrock's actual Spawn UI is `brps3::spawnui::runSpawn`; it posts a selected
spawn set and placement to an in-game command handler. The worker uses an
eight-slot queue with `0x370`-byte records. Its native host vector is
`0x130` bytes with a four-byte zero prefix; the handler copies vector bytes
`+4..+0x12F` into record bytes `+4..+0x12F`, a `0x12C`-byte payload, then
waits for a sequence-numbered completion. The builder validates the tag
shape, count 1–32, mode bounds, expected type, and nine Float32-representable
pose values; it rejects a negative pose[1] spacing multiplier and requires
pose[2] scale to be positive. Operation
`1` occupies `+4`,
the sequence is at `+8`, expected type at `+0x24`, content tag at `+0x90`,
mode/count values at `+0x94..+0xA3`, and nine pose words at
`+0xA4..+0xC7`. The command handler only copies/posts this packet; the
game-side operation-1 branch is emitted separately. That branch reaches the
BLES object-creation routine described below, and the bootstrap is linked to
the BLES hook site; live tag-to-class mapping and descriptor placement semantics
remain unverified. The RTM now exposes the executor path, but runtime success
is not established.

The optional expected-type guard defaults to `-1`, which the Blackrock packet
builder encodes as `0xffffffff`. The BLES 01.24 constructor copies definition
byte `+0x4E` to created sObject byte `+0x08`; Blackrock's emitted guard compares
the returned object's `+0x08` byte with packet `+0x24`. The BLES EBOOT also has
a 25-record object-type name table at `0x0189A0CC` with `0x1C`-byte records;
record 9 points to `biped`, and the sequence agrees with the local D1
Reimagined type list (`9 = biped`). Blackrock's `spawnable.tsv` and
`entities.tsv` use catalog type `9` for biped-like objects, including both Vex
archetypes and player Guardians. This is strong static evidence for a shared
object-kind enum, but no direct BLES dataflow proves the catalog `type` column
indexes the BLES name table or is the expected guard value. The RTM labels this
as a possible name-table match, preserves the byte, and offers an explicit
**Use catalog type as expected-type guard** action marked unverified; selecting
a catalog row does not apply it automatically. A wrong value can reject an
otherwise valid spawn. This value describes an object kind, not whether the
object is hostile. See [spawn type evidence](../research/spawn-type-evidence.md).

This spawn flow does not send Blackrock a list of addresses for already-loaded
enemies. The request carries a content tag; the in-process executor resolves
that packed handle against Destiny's runtime table and receives an object
handle/result from the create call. The RTM's separate tag resolver probe
derives one candidate pointer for inspection, but it is read-only, is not a
spawn prerequisite, and does not prove the tag identifies an enemy. The
executor's returned row word is preserved as a handle value; it is not
presented as a confirmed raw object address.

That distinction applies to the general Spawn action. Blackrock's separate
Enemy swap and live-object/spawner tools inspect resident actors and require
objects loaded in the current area; their live-object addresses are not inputs
to `runSpawn`. The current RTM has not ported those actor-enumeration/swap
flows.

Blackrock's release README says to use `TESTING > Port checks` to validate a
live connection. Its `Handler::ensure` path is invoked during command
dispatch. The inspected manager initialization routine also calls the base
bootstrap installer, though the static call chain does not establish that a
particular UI connection event triggers it. Blackrock's visible Spawn
workflow is therefore start the runtime, connect and validate, choose a spawn
set and placement, then submit; base bootstrap and handler setup are internal
steps rather than user buttons. This RTM exposes a separate explicit
**Install executor** action that combines its bootstrap, handler, and spawn
image setup. In the inspected call path, `FUN_140013390` calls bootstrap
`FUN_1402d16d0`; command submit `FUN_14043C180` calls
`Handler::ensure` (`FUN_140436610`) before claiming and publishing a queue
slot. The Blackrock button sequence is: start the app and connect to the
console; run **TESTING > Port checks**; open **Spawn (sobject_new)**; search the
catalog, pick a row, set count and the **Spawn placement** card; then use
**Spawn with the placement below (Ctrl+Enter)** or a spawn shortcut such as
**At Crosshair** / **At My Position**. Destiny must be advancing game frames;
the submit path explicitly reports no frames when blocked by a loading screen,
menu, or pause.

For this RTM, link the same supported BLES 01.24 console and put Destiny in a
playable area. Optionally run the read-only tag and region checks, then choose
the tag and placement. Press **Install executor**
once when an executor is not already active in the current Destiny session,
then press **Spawn selected tag** and wait for a terminal status. Further
requests can reuse the installed executor. After the final request, press
**Recover hook**; if recovery leaves images resident, restart Destiny before
installing again.
If the link drops after publication, reconnect to the same console and
reconcile the journaled request before recovery. The RTM blocks recovery while
a request result is still journaled, so it cannot silently discard that
result. This is a static workflow comparison; no live-console sequence has
been run.

The remote queue is allocated with the handler state at `base+0x150`; each
0x370-byte record begins with a status word. The host writes the payload at
slot+4, sequence at +8, then publishes status 1. The recovered initializer
is reproduced by `source/js/spawn-data-block.js`; `source/js/spawn-packet.js`
now distinguishes the native vector from the copied slot payload. Polling rejects a changed
sequence, treats status 3 as complete and status 4 as command failure, and
clears its local slot flags on return. A cancel request is written at slot+0x10.
The host parser caps the result at 32 rows. Static inspection of the emitted
executor shows a 16-bit per-item result code at `slot+0x130+2*i`, and a handle
word at `slot+0x170+0x10*i`. The common store writes `0x0200` on the no-handle
path and `0x0300` for every non-null handle path. The host parser schema reads
the high byte as a result type and the low byte as a detail, but the emitted
program loses its type-check flags before the common store. Code 3 therefore
does not distinguish a successful type match from a failed live-handle or type
check. The RTM reports it as ambiguous. The worker counter at `slot+0x18`
increments on the emitter's expected-type equality path; the completed count is
at `slot+0x14`. The RTM surfaces the former as `successCount` and preserves
the raw per-item result and handle. These static details do not validate target-side descriptor
contents or prove that a particular tag creates the intended object in BLES
01.24.

Blackrock's `FUN_140436610` is identified by closure RTTI as
`cmd::Handler::ensure`. It validates a table of game-code fingerprints and
passes a lambda to `FUN_1402d4020`, which allocates/names shared code, invokes
the lambda to fill a host byte vector, then uploads and registers that buffer.
The lambda's callable body is `FUN_1404357B0`, which forwards to
`FUN_140437930`. That function emits a PPU program instruction-by-instruction,
copies it into the output vector, and enforces a `0xC00`-byte maximum. It
contains PPC instruction words and command schemas including `spawn`,
`loot_event`, and `sobject_new`-related structures. Separately, the ensure
routine allocates `0x1CD0` bytes through `FUN_1402cc710`, stores the returned
32-bit remote base at `ctx+0x54`, and initializes the eight slot-state pairs.
The allocation receives a full `0x1CD0`-byte initializer: a separate buffer is
zeroed, its header/template is written through `+0xBF`, then the allocator
copies that entire buffer. The descriptor scratch at remote `base+0xC0` is
therefore zero-initialized before a command runs; `desc+4`, `desc+0x68`, and
`desc+0x70` are deliberate zero defaults, not stale allocation contents. The
packet queue is inside a runtime allocation; the packet builder does not
contain a fixed PS3 queue address. The separate bootstrap installer
`FUN_1402d16d0` builds a hook stub and branches BLES site `0x354074` to it. It
first reads three hard-coded scratch ranges and aborts if any byte is nonzero:
stub `0x183E000/0x2000`, handler/queue `0x1C74000/0xC000`, and adjacent reserve
`0x183D200/0xE00`. It generates a stub under `0x400` bytes, writes the `BRX1`
v1 command header, then patches `0x354074` from original `bl 0x3510F0` to
`bl 0x183E000`. The stub tail-branches to the displaced callee. The installer
journals the patch and reads it back. Static recovery inspection found an
explicit restore path: `FUN_1402c5080` restores one address only when live bytes
still match the recorded original or replacement, and pending-file recovery in
`FUN_1402c4390` restores matching replacements, skips bytes already original,
and refuses unknown bytes. Normal setup blocks on a pending journal and asks
the user to run Recover; manager destruction itself does not restore patches.
No cache flush/invalidate sequence is visible in the client installer,
undo/recovery, or write wrapper. The public [webMAN PS3MAPI setter](https://github.com/aldostools/webMAN-MOD/blob/45025987231248e4939a9f00ef851aa4aa0a5345/include/ps3mapi/ps3mapi.h#L3367-L3415)
passes `MEMORY SET` through Cobra's process-memory setter; [Cobra's setter](https://github.com/xXEvilnatXx/Cobra-PS3/blob/main/8.5/4.93/CEX/SRC/stage2/ps3mapi_core.c#L1804-L1817)
delegates to the PS3 firmware `process_write_memory` symbol. Cobra also has an
explicit [instruction-cache helper](https://github.com/xXEvilnatXx/Cobra-PS3/blob/main/8.5/4.93/CEX/SRC/lv2/src/patch.S#L3-L20),
but its PS3MAPI setter does not call it. The firmware implementation is opaque
in those sources, so target-side instruction-cache synchronization remains
unverified. No page allocator or protection-change call was found in
Blackrock's installer. The worker reads each task as two
big-endian words at `0x1C74010 + index*8`: callback address, then context. It
sets ELFv1 TOC `r2=0x01891798`, loads the context into `r3`, and invokes the
callback through `r12`/CTR. `Handler::ensure` registers its shared-code address
with the 0x1CD0-byte state block as context. The ABI is statically resolved;
runtime register preservation, cache coherence, and safe teardown remain
unverified.

The earlier prerelease bootstrap transcribed two instructions incorrectly.
The task callback loaded its context from callback code instead of the task
entry; the generic command queue also loaded eight bytes for the TOC where the
Blackrock emitter specifies a four-byte load. The regenerated image now uses
`807E0004` (`lwz r3,4(r30)`) and `805E0008` (`lwz r2,8(r30)`). An independent
offline Unicorn check executes the 32-bit-compatible dispatch fragments and
observes the intended context, callback, TOC and command status; it does not
execute the PPC64 spawn worker or validate PS3 cache behavior. See
[bootstrap-data-layout.md](../research/bootstrap-data-layout.md).

Three generic direct-call relocations found in the emitted program target BLES
01.24 functions `0x26B398`, `0x26B448`, and `0x26B5E4`. The hash-matched ELF
identifies these as packed-handle validation/type-query and cleanup paths;
none accepts a spawn tag or transform or creates an actor.

A separate call emitted in the spawn-related path sets `r2` to TOC
`0x01891798`, `r3` to `r31+0x140`, and `r4` to `r31+0xC0`, then branches to
`0x13ED38`. The BLES 01.24 disassembly identifies this as an object-creation
routine: it allocates a handle, registers an initialized 0xC0-byte record,
performs lifecycle setup, and returns the handle through an output pointer
(r3; `-1` on failure) while receiving a creation descriptor (r4). This is the
object-creation call emitted by Blackrock's operation-1 branch; the resulting
object's precise class and its runtime tag mapping still need live validation.
The constructor itself reads descriptor `+0x68` and `+0x70`; it has no direct
load or branch on `+0x34`. Its direct helpers `0x138420` and `0x1F95A4` do not
directly consume `+0x20..+0x2B` or `+0x34` in the examined paths. This supports
structural acceptance of the zeroed fields but cannot rule out later indirect
consumers. Native constructor failures clean up their own partial record; the
Blackrock emitter also calls `0x26B5E4` on its post-creation `refuse` route.

The BLES caller at `0x002FF874` provides stronger context: it resolves a
descriptor through `0x001F83F0`, transforms a supplied pose, writes vectors at
descriptor offsets `+0x10` and `+0x20`, writes a caller value at `+0x34`,
then calls `0x13ED38(&out_handle, descriptor)`. Resolver `0x0013B7E8` treats
descriptor `+0` as a packed source handle and `+4` as a suboffset; creation
also consumes `+0x68` option bits, copies `+0x70`, and reads flags from the
source record. The normal descriptor constructor at `0x1F7E68` clears 0x80
bytes, initializes it, then copies the source reference's first three words
into `desc+0`, `desc+4`, and `desc+8`. The second 16-byte pose vector at
`desc+0x20` is copied into the created record by helper `0x147818`; that helper
copies a zero block as-is and does not reject it. This confirms the field can
flow through the copy path but does not establish the resulting pose semantics.
The exact meaning of `+0x34` and the precise created object class remain
unresolved. The constructor copies definition byte `+0x4E` to created sObject
byte `+0x08`, which Blackrock's emitted expected-type guard reads after
creation. The catalog-to-guard numeric mapping remains unverified; see
[spawn type evidence](../research/spawn-type-evidence.md).

Blackrock's generated `spawn` branch copies the packet's 32-bit tag directly
into descriptor `+0`, computes four descriptor words at `+0x10..+0x1C`, and
passes that descriptor to `0x13ED38`. Packet word `+0xA4` participates in an
aim-distance or affine calculation depending on placement mode; `+0xA8` scales
multi-count spacing; pose[2] at `+0xAC` is the positive scale value copied to
`desc+0x2C`; words `+0xB0`, `+0xB4`, and `+0xB8` are target-relative offsets.
Placement mode 2 reads the explicit coordinates at `+0xBC..+0xC4`. The full
coordinate semantics remain unresolved. The
earlier `0x8087600F` sample decodes under the BLES resolver to bucket `0x043B` /
slot `0x000F`; the catalog-backed Vex Minotaur candidate `0x815BA133` decodes
to bucket `0x0ADD` / slot `0x0133`. Both have a syntactically compatible
packed-handle form, but neither has been resolved on a live BLES 01.24 session.
The BLES handle-table root at `0x01896D50` and
its bucket descriptors are dynamic, so this does not prove the tag resolves to
the intended spawn definition. The generated branch also does not write
descriptor `+4`, but the copied zeroed initializer establishes this as offset
zero. It writes four pose words at `desc+0x10..+0x1C` and a packet word at
`desc+0x2C`; the second pose vector's `desc+0x20..+0x2B` and caller field
`desc+0x34` remain zero. This differs from the normal BLES caller, which writes
both 16-byte pose vectors and a caller-provided value at `+0x34`. The tag
resolver probe can inspect a candidate handle-table entry and read the
resolved definition's `+0x4E` type byte on a live verified BLES session without
calling the object-creation routine. It checks whether that byte agrees with
the selected expected-type guard; the catalog mapping, hostility, and runtime
creation result remain unverified.

Blackrock's Spawn UI stores placement mode, distance, screen reticle X/Y,
world offset X/Y/Z, coordinates X/Y/Z, count, spacing, scale, facing, and
position source in that order. `FUN_140426130` normalizes those fields;
`FUN_14042d150` repacks them for `FUN_140436020`. The trace maps mode to packet
`+0x94`, facing to `+0x98`, count to `+0x9C`, and position source to `+0xA0`.
Mode values are `0` Crosshair, `1` Player position, and `2` Coordinates.
Facing value names follow Blackrock's tooltip order: `0` Face me, `1` Along
aim, `2` World axes; the name-to-ordinal order is inferred from the static UI
trace. Position source is `0` verified sObject `+0xB0`, `1` player element
`+0x2810`, which Blackrock itself says still needs a live PS3 check.

The nine packet pose floats are, in order, distance, spacing, scale, world
offset X/Y/Z, and coordinate X/Y/Z. Reticle X/Y are intentionally omitted by
Blackrock's closure; its submit handler warns that the in-game command aims at
screen center. Blackrock's recovered defaults are distance `6`, spacing `2`,
and scale `1`. The RTM mirrors the named modes, a verified engine-position
source default, and the two crosshair/player shortcuts, but has no
non-spawning placement preview. This mapping comes from static source
analysis and has not been checked against a live BLES 01.24 spawn.

The release `spawnable.tsv` catalog has a `type` column, and its `815BA133`
Vex Minotaur row has `type=9`. Other type-9 entries include player Guardians,
so this is an object-kind value rather than an enemy flag. The UI copies the
tag on selection and leaves the guard unchanged; the user can explicitly apply
the catalog type as an unverified candidate. The guard still defaults to `-1`
(disabled).

The signature source is the hash-verified BLES01857 01.24 EBOOT
`E7F100B38BBAF6C23F71B390FDB103D31B427187C38DCFBD47E09C94C4A44206`. The
48-byte `0x13ED38` prologue and 32-byte `0x13ED90` continuation are each unique
aligned hits in its executable load segments, so the live code check can
identify this candidate object-creation path without invoking it.

The BLES 01.24 EBOOT hash matches the inspected binary. Its PT_LOAD extents end
at `0x0183D178` and `0x01C72948`; all three hard-coded scratch ranges are outside
those extents, with the adjacent reserve beginning 0x88 bytes after the first
extent. They have no static EBOOT bytes, so a live all-zero/readable check is
required. The new region preflight performs that check sequentially with fresh
BLES capabilities; it does not reserve the memory or eliminate a race before
installation. At `0x354074`, the EBOOT has the original hook-site instruction
`bl 0x3510F0`; Blackrock's bootstrap installer writes a branch there to its
generated stub. The main entry descriptor at `0x01849568` points to code
`0x0001238C` with TOC `0x01891798`, which matches the value embedded by
Blackrock's executor generator. These matches make a region-specific executor
port plausible; they do not establish instruction-cache behavior for a new RTM
installer or that Blackrock's zero-only region assumptions hold on a different
live session. The
authored-member path is not yet an action: its tested State A arrives through
the game's normal replicated component pipeline, while this RTM has no
verified way to resolve and apply that state to the matching live component.
The BLES 01.00 player-spawn notes below are not substitutes for the 01.24
authored-member path.

The 01.24 authored-member creation gate at `0x2575F8` reads a 0x48-byte state
and rejects it when `state[6]` is zero or signed `state[5]` is 1, 3, or 4. It
also requires `member+0x14C == 0` and `member+0x174 == -1`, then calls
`0x256D04(member, state)`. That helper resolves the member handle through the
descriptor at `0x01896D50`, reads its `+0x50` state value, and calls
`0x247C88`; the deeper state-application route reaches `0x246DF0`. On status 2,
the caller binds the returned handle with `0x255A18`, which resolves through
the descriptor at `0x01A1E160` before writing `member+0x174`. This is evidence
for the authored component/state pipeline, not a general object-spawn API:
none of these calls accepts Blackrock's tag, expected-type, and transform
tuple, and the call chain has not been shown to consume Blackrock operation 1.

The BLES 01.24 player-pool root and local-player match path are now cross-checked
against the EBOOT and exposed through a bounded local appearance probe. The
tag-loader layout remains unresolved: the 01.24 ELF contains tag-loader and
resourcerer labels, but the references found so far register metrics rather
than expose live queue fields. Older 01.00 address notes do not match the 01.24
instruction sites. A separate Resource / Bootflow Snapshot reads the verified bootflow and resource scheduler; it is not a tag-loader queue diagnostic. The activity and player readers provide templates
for later diagnostics. Spawn, inventory, and other mutating features still need
their own BLES address/layout validation and restore behavior before they can
be implemented reliably.

The BLES 01.24 schemas establish account and character **wire payload** sizes
and hashes, plus a 320-slot character inventory and a 326-slot vault-like
account block. The offline viewer validates the 16-byte QZ header and body
length for imported wire objects, but
the runtime cache's per-object body offset is not established. These schemas
do not authorize reading the same offsets from a guessed live cache pointer;
the vault block's live semantics are also unverified.

## Network and identity scope

Blackrock's binary includes raw-socket networking and hard-coded BAP endpoints.
Those peer/network functions are not part of this port. Its profile strings
also identify account/character profiles and SOIDs. The traced inventory
snapshot path sends PS3MAPI `MEMORY GET` commands containing address/length;
the returned profile bytes are received into a separate local buffer and
formatted for the UI. No SOID bytes were found in that path's send payload.
Blackrock's action UI resolves a selected item into a native `ActionRequest`,
then queues a delayed confirmation callback. It also contains a generic
PS3MAPI `MEMORY SET` writer, but the current static trace has not connected
that callback or its native route record to the writer. No direct Bungie
HTTP/OAuth call was found in the action path. Its final transport and request
bytes remain unresolved. A 1,706-function direct-call sweep from the Vault
callbacks and action backend found no direct path to the BAP endpoint
initializer or SOID diagnostics UI. Indirect dispatch is not fully resolved,
so this is not a binary-wide network audit. The account/vault editor is still
unported. The live item-pool snapshot reports only cached item indexes;
it does not map records to the active character, modify items, or prove vault
ownership. The offline viewer handles explicit serialized 01.24 wire payloads
and labels the account vault-like block as unverified. Blackrock's host-side
action IDs and the missing BLES target-side boundary are summarized in
[inventory action evidence](../research/inventory-action-evidence.md).

## License boundary

The RTM's proprietary license continues to cover its original material. The
activity/session resolver, live item-pool reader, resource/bootflow snapshot, and offline inventory wire-layout decoder are
separately marked GPL-2.0-only because they use material from the D1 Reimagined
repository; the full license text is in `third-party-licenses/GPL-2.0.txt`.
Blackrock is a separate, unlicensed binary package and contributes no code or
data to this repository.
