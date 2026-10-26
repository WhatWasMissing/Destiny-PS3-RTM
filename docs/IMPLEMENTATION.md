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
- Connection generation guards and disconnect cleanup added; race integration
  tests are still required. The original reconnect and direct read paths still
  need consolidated handling.
- Go 1.27.1 is available in the local pokemon-sots toolchain. Desktop host
  implementation/build remains outstanding.
- No live PS3 requests were made for these checks.
