# RTM improvement work

Original scope: reproducible desktop build; connection reliability; truthful
control states; visual design and navigation; teleport editing, sorting,
duplicate detection, import validation and deletion undo; full settings backup;
diagnostics; modular code; Overwalls review and port recommendations.

## Current evidence

- Original frontend remains in commit e36edf7 and local recovered-current/.
- Development branch: codex/rtm-reliability.
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
  interval able to reconnect after a later disconnect.
- Custom writes validate width/overflow and require readback. Legacy fire/multi
  helpers and typed character writes use verified memory writes.
- Player/server hook payload writes are verified before branch activation;
  player hook writes also reject session changes. Failures propagate instead
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
- Replaced the nonfunctional RPC notification variant with the documented PS3
  system notification flow. Removed experimental Player Size and connection
  RPC calls; documented Player Size, trophy unlock, and Destiny-internal toast
  as unsupported by stock webMAN's HTTP PS3MAPI endpoint set.
- The client archive/geolocation feature was excluded because it collects
  other players' IP and location/provider data. Older 1.08/phone offsets were
  excluded because they conflict with the local 1.24 source.
- Source locations, online references, and remaining compatibility limits are
  recorded in `SOURCE-COMPARISON.md`.
