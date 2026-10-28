# RTM improvement work

Original scope: reproducible desktop build; connection reliability; truthful
control states; visual design and navigation; teleport editing, sorting,
duplicate detection, import validation and deletion undo; full settings backup;
diagnostics; modular code; Overwalls review and port recommendations.

## Current evidence

- The desktop connection badge checks `SERVER GETVERSION` through the exact
  read-only `/ps3mapi.ps3` command when webMAN supports it; if the command is
  unsupported, it shows `WEBMAN ONLINE` and stops probe-driven relinking rather
  than treating that response as a dead console. Auto reconnect only retries the
  version query and does not restart PS3MAPI or reinstall hooks. Health probes defer while another
  RTM request is pending or has run in the prior five seconds, avoiding a
  competing check during memory operations. Health and retry logs include the
  query failure reason; the desktop relay allows only this exact command.
  Restart/shutdown buttons use webMAN's documented `/restart.ps3` and
  `/shutdown.ps3` routes. Shutdown invalidates health checks before sending the
  request. The version
  response confirms the PS3MAPI HTTP command endpoint, not that Destiny's
  process is running; memory tools check the game build separately.
- Original frontend remains in commit e36edf7 and local recovered-current/.
- Development branch: `mvq/destiny-rtm-e-drive`.
- Transport module integrated: bounded requests, HTTP status checks, targeted
  process scoping, cancellation API and bounded diagnostics.
- Four isolated Node tests pass using fake console responses, including stalled
  requests and cancellation. No live-console verification yet.
- Cancellation API is not yet wired to session lifecycle. Existing controls still
  need pending/unknown/verified states and readback verification.

## Remaining implementation and verification

1. Build desktop host, inspect original host HTTP behavior, provide reproducible
   packaging and build instructions. Verify resulting executable with mocks.
2. Route all reads and health checks through consistent transport; cancel stale
   sessions and serialize writes where needed. Verify reconnect/disconnect races.
3. Implement truthful states and diagnostics export, including partial failures.
4. Implement validated versioned settings backups and complete teleport workflow.
5. Separate feature/storage/UI modules and apply responsive visual improvements.
6. Exercise user workflows in browser and desktop. Record live-console limits.
7. Complete Overwalls analysis, compare existing capabilities and deliver port
   recommendations with transport/version dependencies and code evidence.

Do not mark the complete objective achieved based on the transport tests alone.

## Second implementation pass

- Settings and teleport modules added; seven Node tests pass (transport, schema
  rejection, backup round trip and restore rollback).
- Tools panel verified in browser; diagnostics JSON downloaded successfully.
- Recovered app creates some content late; extension toolbars initialize after
  existing DOMContentLoaded callbacks. Browser confirmed both target sections.
- Teleport editing/sorting/import/export/duplicate checking/20-step undo added.
  Full browser workflow coverage is still outstanding.
- Connection generation guards and disconnect cleanup added; later production
  function tests cover reconnect and disconnect races.
- Go 1.27.1 is available in the local pokemon-sots toolchain. Desktop host
  implementation/build remains outstanding.
- No live PS3 requests were made for these checks.

## Desktop and modular frontend pass

- Replacement dependency-free Go host and build.ps1 added. Go tests cover local
  fake-console proxying, endpoint/target restrictions, authentication and embedded
  frontend. The first packaged executable passed host smoke checks.
- Browser reads and health probes now use the bounded transport; host detection
  routes console traffic through the local proxy. Full race tests remain pending.
- Inline JavaScript extracted into named files in original execution order.
- External design.css applies readable system typography, graphite surfaces,
  muted gold actions, keyboard focus and responsive control sizing. Desktop Tools
  screen inspected after extraction. Mobile/light-mode checks remain pending.
- Status badges now distinguish UNKNOWN from requested ON/OFF; actual write
  readback and multi-step operation verification still need implementation.
- Overwalls assessment now includes main and Retrofit projects, scenario schema,
  hook/plugin dependencies, AFK timing and concrete port recommendations.
- Remaining key work: live-console limits, truthful verified states, queue/session
  integration tests, full backup/teleport browser workflows, responsive QA and
  final packaged application checks. Goal is not complete.

## Write verification pass

- Main togFire/Multi/Float/Bytes controls now serialize patch operations and
  verify the resulting memory bytes. HTTP acceptance without readable memory
  leaves status UNKNOWN; mismatches and partial writes are reported explicitly.
- Stored active-cheat highlights no longer restore as live console state.
  Explicit connect/disconnect and automatic reconnect invalidate prior verified
  states. Reconnecting blocks patch-runner operations.
- Late automatic reconnect results cannot overwrite a newer session or a
  disconnect; pending probes do not overlap interval ticks.
- Auto reconnect is opt-in, and health polling uses the read-only PS3MAPI
  version query after foreground activity settles. The online status badge is
  no longer an active relink control, avoiding accidental resets from a click.
- Health-driven relinking never sends a console restart command. Only the
  explicit, confirmation-gated Restart action requests `/restart.ps3` and then
  waits for the console before relinking.
- The diagnostics export now includes a capped, IP-free connection lifecycle
  trail alongside request paths/outcomes, so health relinks can be distinguished
  from explicit restart and manual badge relink actions.
- Thirteen Node tests pass, including serialized operation session changes,
  readback mismatch/unavailability, partial failures, and tests executing the
  production reconnect function with a fake console probe.
- Remaining: extend verification beyond the main generic toggles (custom
  writes, sliders, server modes, teleport and hook operations); lifecycle
  integration beyond automatic reconnect; full UI workflows and responsive QA.
  No live-console verification has been performed.

## Workflow and FOV pass

- Saved-position browser workflow exercised while disconnected: create, edit
  coordinates/name, delete, undo, export, import with duplicate skipping, complete
  settings export and restore/reload. The test position remained intact.
- Replaced saved-position and folder native prompts with labelled in-app dialog
  forms. Initial testing exposed unsupported prompt() and an unset default
  folder option; both are fixed. Settings/position import confirmations now use
  the same in-app dialog system.
- Backup validation rejects coercible invalid coordinates, float32 overflow,
  invalid enums/IPs/address mappings and duplicate folder IDs. Teleport storage
  failures report an error and restore the last persisted in-memory snapshot.
- Duplicate FOV handlers were removed. The obsolete later definition mapped a
  direct slider value through a 0–10 offset formula. Production-handler tests
  now verify 2.5 writes 2.5 and reset writes/displays 2.0.
- FOV/game-speed writes use readback verification. Teleport messaging explicitly
  states position-write acknowledgement rather than proven in-game movement.
- Phone dark/light views inspected at 390px (375px content area); no document
  horizontal overflow. All five navigation tabs now fit without a hidden tab.
- Desktop rebuild passed Go and JavaScript tests. Verification against a live
  PS3 remains outstanding; no console was used.

## Legacy operations and session audit

- Quick relink and restart now invalidate prior sessions and use read-only,
  generation-aware availability probing. Restart no longer leaves a private
  interval able to reconnect after a later disconnect. Linking is transport-only;
  the player/physics hook is an explicit action in the Gamespeed panel.
- Custom writes validate width/overflow and require readback. Legacy fire/multi
  helpers and typed character writes use verified memory writes.
- Player/server hook payload writes are verified before branch activation;
  player hook writes also reject session changes and refuse to replace an
  unrecognized active hook at the shared code cave. Failures propagate instead
  of silently logging installation success. Reset-all reports verified and
  unconfirmed results independently and retains Deathsinger/server reset paths.
- Production-function tests cover hook activation order/failure/session change,
  custom widths, reset-all failure, quick relink and restart cancellation.
- Request timeout now includes body consumption. Existing response parsers can
  still read the buffered response. Additional transport tests cover stalled
  bodies and preserved memory text.
- Replacement executable launched successfully. Native accessibility reports
  the expected app origin and controls. Native screenshot capture timed out;
  visual inspection so far is from the browser rendering of the same frontend.
- Remaining: full mock-console desktop integration, verify final built artifact,
  finish operation/status audit and record final requirement-by-requirement proof.

## Spawn workflow and visual consistency

- Matched Blackrock's documented Ctrl+Enter submit shortcut in the spawn panel;
  the key binding is scoped to that panel and respects the submit button's
  disabled state.
- Reworked the experimental spawn panel to use the shared surface, typography,
  border-radius, and semantic color tokens. Kept the essential unverified/live
  console and single-client warning visible, with the longer technical rationale
  in a disclosure. Spawn is the primary action, setup/recovery are secondary,
  and cancellation is visually marked as a danger action.
- The action layout adapts at tablet and phone widths. The ordered use steps stay
  visible in both compact and detailed UI modes.
- No test suites or live-console actions were run for the visual changes.

## Five-anchor build profile

- Expanded the BLES01857 01.24 runtime profile from three to five code anchors.
  Both added signatures were verified as unique aligned matches in executable
  segments of the hash-matched ELF. Browser, Go desktop, MCP, write-protection,
  probe checks, fixtures, and documentation now use the same five-anchor list.
- JavaScript syntax checks passed. No test suites or live-console actions were
  run for this change.
- Corrected the Overwalls Read Current control to read `0x306570A4`, matching
  the local Activity Loader C# source and the main RTM reader.
- Rebuilt `dist/DestinyRTM.exe` and copied it to
  `C:/Users/novel/Desktop/DestinyRTM-Prerelease/DestinyRTM.exe`. Both files have
  SHA-256 `49FFBC6BB467571FFB56C2103A9964DC8C904BD78E6015B27CC20F18BFA86FF3`.

## Desktop integration and final-build tooling

- Added Go-driven integration test running the production frontend modules
  against the authenticated local host and a byte-addressed fake console.
  It verifies host detection/auth, read-only relink, verified ON/OFF, readback
  mismatch, partial failure, disconnect state and sanitized diagnostics.
  The test rejects any frontend fetch outside the isolated host origin.
- Extracted connection lifecycle code into connection.js. Browser reload and
  integration tests pass with the production script order.
- Added packaged-executable smoke checks to build.ps1: ephemeral loopback start,
  config and embedded assets, unauthenticated-request rejection and public-target
  rejection, with automatic cleanup. No PS3 requests are sent by the smoke check.
- Added guards for late disconnect cleanup and health probes at the same IP;
  production-function tests cover both. Diagnostics redact popup path text and
  retain HTTP status codes. Thirty-six Node tests currently pass.
- Backup/restore now preserves first-run setup and visibility migration flags.
  Documentation now describes the replacement desktop project consistently.
- Stable string/speed/scanner writes also use verified memory operations.
  The current source comparison, 36-test run, and packaged rebuild passed.

## Final scope audit

The final requirement matrix and validation limits are recorded in
VERIFICATION.md. The shared ps3fetch memory adapter now routes remaining raw
setmem calls through verified writes; the internal runner uses direct bounded
transport to avoid recursion. Integration checks cover destination/process
rejection and exact unaligned UInt16 writes preserving neighboring bytes.
Player hook installation checks supported instructions before any mutation;
cleanup is restricted to hooks owned by this app. Thirty-six Node tests pass,
plus Go host and production-frontend fake-console integration tests.

Stylesheets are now separated into base.css and design.css. A final browser
reload verified the Tools screen with no console errors after that separation.
Build tooling syntax-checks all JavaScript, runs the suites, builds the executable,
smoke-tests that executable, and records executable/source hashes in
 dist/build-manifest.json. No live PS3 or in-game compatibility claim is made.
The original recovery and proprietary license remain preserved. The changes are
reviewable in PR #8; it remains a draft pending live-console compatibility review.

## Local RTM source comparison

- Compared the newer local 1.24 web and C# builds. Added No Spread, Bouncy
  Items, 4× Jump Boost and Rock Jump with conflict handling for overlapping
  patches.
- Restored the scanner controls and added a 103-entry emblem selector.
  Emblem and ammo writes require fresh validation and readback, with saved
  guarded recovery. Ammo edits support the special-ammo float signature and a
  session undo stack.
- Added chunked framebuffer captures with optional pause restoration and a
  persisted manual recovery path. The tool fails closed if it cannot save
  recovery data before pausing. These addresses remain unverified against a
  live game build.
- Matched the five C# framebuffer panels with independent web gallery slots.
  Each profile keeps its latest preview and provides its own PNG download,
  clipboard copy and Fit/Stretch control; recapture revokes only that profile's
  previous preview URL. Tests cover slot isolation, replacement and cleanup.
- Bundled the 19 user-archived Overwalls XML scenarios (276 positions) in the
  Overwalls selector. Loading one confirms replacement of the current scenario;
  the importer validates coordinates and retains the source labels.
- Replaced the nonfunctional RPC notification variant with the documented PS3
  system notification flow. Removed experimental Player Size and connection
  RPC calls at that stage. Player Size has since been ported by emulating its
  1.24 source resolver using guarded memory reads, without executing the
  unsupported function-call path; trophy unlock and Destiny-internal toast
  still depend on that unavailable RPC layer.
- The client archive/geolocation feature was excluded because it collects
  other players' IP and location/provider data. Older 1.08/phone offsets were
  excluded because they conflict with the local 1.24 source.
- Source locations, online references, and remaining compatibility limits are
  recorded in `SOURCE-COMPARISON.md`.

## Blackrock diagnostics and memory tools

- Added BLES 01.24 read-only snapshots for activity/session state, resource and
  bootflow state, the local player appearance window, authored spawn-path code
  signatures, the actor-lookup descriptor, and the bounded self-family
  item-object pool. A packed-handle resolver probe decodes one catalog tag and
  reads/rechecks its live bucket descriptor without following into object
  contents. They verify the
  supported build before reading and disclose the remaining non-atomic or
  unresolved mappings in the UI and port notes.
- The shared session-memory API now splits logical reads of up to `0x600` bytes
  into PS3MAPI GETMEM requests of at most `0x100` bytes, and counts every
  physical request against the session capability budget. This matches the
  current webMAN handler's `BINDATA_SIZE` clamp and prevents short reads from
  invalidating multi-chunk diagnostics.
- Added an offline viewer for explicitly selected BLES wire payloads and a
  bounded local catalog browser. No Blackrock data or code is bundled.
- The offline character view labels only the 17 exact equipped inventory
  starts proven by the BLES 01.24 bucket-range and equipped-slot tables; the
  eight unnamed positions stay numeric.
- User-imported item/loot catalogs with an explicit BLES `itemIndex` or
  `blesItemIndex` field can label live and offline inventory rows. Live rows
  can also use an exact unique unsigned `hash` + `tag` match against the BLES
  01.24 runtime definition table; the table is fully snapshotted twice before
  and once after the pool read under fresh capabilities, and any mismatch
  suppresses those labels. The 8,000-row cap supports browsing the 6,557-row
  Blackrock table as user-selected local data. Its `def` ordinal is never used.
- Added `rtmctl inventory-pool` and the MCP tool
  `rtm_read_live_inventory_pool`; both return pool slots and item indexes only.
  The MCP call has a two-minute deadline for the capped sequential scan.
- Corrected the descriptor count read to use the low halfword of the word at
  `+4` (bytes `+6..+7`), matching the native `lwz`/`extsh` sequence. The
  resource snapshot's bootflow label now uses the captured state-name value.
- Refactored the macro dialog onto the shared palette and form styles, removed
  its malformed input closing tag and hard-coded white field text, and added
  dialog labeling plus initial focus. The accent foreground now follows the
  dark/light theme token.
- Normalized Local Data & Diagnostics table headings, resource block headings,
  and notes to the app's 12px sans-serif label scale; compact data rows and raw
  memory values retain their existing typography.
- Rechecked the three emote-related strings in the hash-verified BLES 01.24 ELF
  with a PPC64 big-endian disassembler. Startup code established the ELFv1 TOC
  base at `0x01891798`; no direct pointer, absolute immediate pair, or nearby
  TOC-relative pair to those strings or the tag-loader labels was found.
  A 16,626-load / 6,341-cell scan found no direct TOC-cell pointers to the
  emote or tag-loader labels; computed lookups and deeper indirection remain
  unresolved.
- A BLES 01.24 data-flow scan found two direct `lfs` references to the Super
  Speed source site at `0x001A96C0`; both add that Float32 value inside vector
  adjustment loops. The source writes only one byte, so semantic and speed
  behavior remain unconfirmed and activation stays disabled.
- Moved the local catalog browser and offline inventory decoder into a dedicated
  Local Data & Diagnostics section registered in the Tools tab and first-run
  section settings; this fixes their previous placement under Overwalls.
- Ported Overwalls' scenario-editor “read current coordinates” action into both
  add and edit dialogs. It checks the BLES 01.24 profile, uses the session-bound
  read capability to capture XYZ, validates finite Float32 values, and rechecks
  the player pointer before filling fields. The Local Data & Diagnostics grid
  now uses one shared read-only/snapshot note and shorter purpose-led card copy.
- All source JavaScript passed `node --check`, and a structural HTML check
  confirmed the new section is a sibling of Overwalls. The original build
  deferral in that pass was superseded by the later local desktop build.
- Node syntax checks passed for the changed and newly added JavaScript modules.
  Go builds of the desktop host, `cmd/rtmctl`, and `cmd/rtm-mcp` passed with the
  local Go 1.27.1 toolchain. The separate desktop build is
  `dist/DestinyRTM-diagnostics-mvq.exe`; no test suites, packaged smoke checks,
  or live-console reads were run in this pass.
- Blackrock mutating features that need unresolved BLES runtime structures or
  game-thread calls remain incomplete; see `BLACKROCK-FEATURE-PORT.md`.
- The PS3MAPI health probe checks the HTTP server version and emits its failure
  reason. The explicit Restart button calls webMAN's `/restart.ps3` console
  command; automatic health checks and relinking never restart the console or
  PS3MAPI service.
- A second relink trigger was in the old Restart button path: it requested the
  undocumented `/restart.ps3mapi` URL, then entered its 15-second relink flow
  even if that request failed. Auto reconnect was enabled by default in that
  version, so a rejected request could make the UI go offline and repeatedly
  reconnect without restarting the console. webMAN documents `/restart.ps3` as
  the console restart command. The current path uses that endpoint and returns
  early on a definite HTTP rejection, so an invalid request no longer
  masquerades as a console restart followed by a PS3MAPI reconnect.
- Source diagnosis for repeated relinks: the earlier monitor fetched the full,
  dynamic `/index.ps3` page with a 5-second timeout. If the user had auto
  reconnect enabled, two slow or missed page requests could mark the UI offline
  and start frequent relinking even while PS3MAPI remained available.
  The monitor now uses an 8-second read-only `SERVER GETVERSION` check, defaults
  auto reconnect off, records the failure reason, and defers checks while an RTM
  request is pending or recently completed. Explicit unsupported/invalid HTTP
  command responses now disable health-driven relinking and keep a separate
  `WEBMAN ONLINE` state. A local diagnostics export is still needed to identify
  a specific console-side drop.

## Actor lookup and Safe Mode follow-up

- A BLES 01.24 PPC trace confirms that the actor-handle record's `+0x30` word
  indexes a bitset at `0x01A21138`; the selected bit gates a further status
  check. A related same-build RPCS3 research note associates `+0x30` with a
  unit datum in two sampled runs, and the EBOOT accessor at `0x00191960` returns
  that field. This is not a physical-PS3 sample; the bitmap bit's allocation,
  liveness, or residency meaning remains unknown. The `0x00255FA8` path uses it
  to gate cleanup, not to prove an occupied enemy slot.
- Two additional paths consume the `0x01A1E160` descriptor's storage and stride
  while resolving masked handles. A helper-mediated initializer now confirms
  it is the AI Actor Manager's `actors` pool, capacity 40, PS3 stride `0x28D0`.
  The earlier direct-store/literal scan missed the indirect initialization.
  A separate allocator bitmap and full-datum reconstruction are now traced;
  the app does not yet sample them. Live-enemy correlation and the `+0x226C`
  packed handle's target remain unresolved, so actor enumeration remains
  unimplemented. See `research/actor-pool-descriptor.md`.
- The Safe Mode source audit found that the previous connection flow installed
  a shared Destiny hook using about 81 sequential memory writes. The current
  flow requires an explicit hook action. Current idle Link performs no Destiny
  memory writes. Normal Link sends beeps, an XMB popup, and read-only
  `SERVER GETVERSION` requests. Quiet connection suppresses the beeps, popup,
  recurring probes, and auto reconnect after its one initial version check.
  This supports a prior-build game-patch risk but does not establish that an
  RTM caused the reported Safe Mode boots. See `SAFE-MODE-INVESTIGATION.md`
  and `ADDRESS-AUDIT.md`.
- Added hash-checked initializer and consumer traces in
  `research/actor-pool-descriptor.md`, cross-checked against the local
  D1 Reimagined RPCS3 research notes. The earlier scan findings in
  `research/actor-lookup-descriptor-evidence.md` remain heuristic counts, not
  complete xrefs. No loaded-enemy list was added because live slot correlation
  and record semantics remain unresolved.
- The actor-probe desktop build passed Go, 97 Node tests, and
  packaged desktop smoke checks. `dist/DestinyRTM.exe` and
  `C:\Users\novel\Desktop\DestinyRTM-Prerelease\DestinyRTM.exe` match at
  SHA-256 `12D0676F5E032F391CDA993B8A8237A499575256BD47FF7E8B48A0F22CB15783`.

## Direct actor-pool descriptor tools

- Added the shared Go `ReadActorPoolDescriptor` reader, the CLI command
  `rtmctl actor-pool`, and MCP tool `rtm_read_actor_pool_descriptor`. They
  verify the five-anchor BLES 01.24 profile plus the full 64-byte actor-path
  signature before and after reading only the 20-byte descriptor. They refuse
  changed descriptors, unexpected dimensions and invalid storage spans; no
  backing pointer is followed and zero actor records are read.
- CLI flags are limited to `--ip` and `--json` for this command. The MCP tool
  accepts only `target_ipv4`, returns explicit read-only/non-atomic metadata,
  and does not claim a loaded-enemy list or confirmed spawning.
- Fake-console Go tests cover initialization, descriptor changes, initial/final
  signature mismatches, pointer/span bounds, strict target/argument handling,
  unsupported clients and refusal without partial results. The full build
  passed Go tests, all 97 Node tests and packaged desktop/protocol smoke checks.
  No live-console request was made in this tool-extension pass.
- The direct-tool build refreshed the Desktop prerelease folder with the
  manifest and `PRERELEASE-TOOLS.txt` instructions. Its desktop app SHA-256 was
  `60F488FA1D32797463189F39E2055EA11BF340BD622311DB9B07DA4FD5EE0F1D`.

## Spawn bootstrap correction

- Compared the original Blackrock emitter trace against the generated PowerPC
  bootstrap and corrected two mistranscribed loads. The task callback now
  receives the context from the task table at `+4`, and the generic command
  queue loads its 32-bit TOC field. The previous bootstrap incorrectly used
  the callback code address as the context base and loaded eight TOC bytes.
- The independent `research/verify_bootstrap_dispatch.py` check executes the
  32-bit-compatible task and command-header fragments in Unicorn. It reproduces
  the prior bad context and verifies the corrected context, callback, TOC and
  status transition. It does not execute the PPC64 worker or Destiny code.
- A saved journal for the prior exact bootstrap image is recognized during
  confirmed pre-activation recovery. Cleanup compares owned bytes; unknown
  bootstrap or program hashes keep all scratch resident. Altered chunks remain
  untouched. If hook activation may have
  occurred, recovery restores the hook but retains the task entry, dispatcher
  header, code and data until Destiny restarts; a callback may still be in
  flight even when no spawn request was submitted.
- Rebuilt the current Desktop prerelease after the correction and recovery
  changes. Go checks, all 104 Node tests, protocol and packaged-desktop smoke
  checks passed; no live
  PS3 request was made. Desktop and `dist` executables match at SHA-256
  `ECD6F5E49A36209BC55873B2CFCF72A1194FCA341BC95168E188614BDAF29FD6`.
  Live spawning, placement and PS3 instruction-cache behavior remain
  unverified. `docs/SPAWNING-INSTRUCTIONS.txt` is synced to the Desktop folder.
