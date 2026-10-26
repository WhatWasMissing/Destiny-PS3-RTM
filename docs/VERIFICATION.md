# Scope and verification

Objective: implement the RTM reliability and visual improvements, then port the
missing feature workflows identified in the local Overwalls source.

| Requested improvement | Implementation and evidence |
|---|---|
| Reproducible desktop build | Root Go host embeds source/, build.ps1 runs tests, builds dist/DestinyRTM.exe and smoke-tests that exact executable. No external Go dependencies. |
| Connection reliability | connection.js owns lifecycle/generation checks. transport.js bounds headers and body reads, rejects HTTP errors and supports cancellation. Production-function tests cover quick/automatic reconnect, restart, disconnect and health-check races. |
| Truthful control states | Main toggles and the shared memory adapter require matching readback; stored active flags are not restored as verified state. Partial failures and unavailable reads remain unknown. Tests and the fake-console desktop integration cover acknowledged writes, mismatch and partial failure. Notification/restart/CID requests are not presented as proof of completed console behavior. |
| Visual language | base.css/design.css separate the original layout from graphite surfaces, muted gold actions, readable system typography, restrained borders, focus states and responsive layouts. Existing Destiny imagery and category navigation are preserved. Browser desktop/phone dark/light views were inspected. |
| Teleport management | In-app labelled forms for save/edit/move/folders; sorting, duplicate-aware JSON import, export and bounded undo. Browser tests exercised save, edit, delete, undo, duplicate skipping, export and restored data. Validation rejects non-finite and float32-overflow coordinates; storage failures restore the previous snapshot. |
| Complete settings backup | Versioned allowlisted backup covers teleports/folders, macros, pins, custom addresses, appearance/accent/UI mode/tab, HEN, reconnect and setup/section preferences. Validation occurs before mutation; rollback on storage failure is tested. Active console state and session warning bypasses are excluded. |
| Diagnostics | Tools exports timestamped request outcomes and HTTP statuses. Tests check export redaction of IP addresses and popup text. Browser diagnostics download was exercised. |
| Code organization | Connection, transport, operations, backups, dialogs, teleport tools, settings, pins/macros, onboarding, notifications and artwork are separate classic-script modules. Stylesheets are separate; script execution order is preserved. Original recovery remains in commit e36edf7. |
| Overwalls feature port | `source/js/overwalls-tools.js` and the Overwalls tab implement scenario XML, tower/activity shortcuts, source activity mode, farm profiles, stoppable routes, inventory snapshots, character values, individual and grouped cheat controls, plugin checks/loading and saved hook restoration. Coverage and source attribution are in `docs/OVERWALLS-PORT.md`. No live PS3 action was performed. |
| Local RTM feature comparison | Local 1.24 sources were used to add the missing core toggles, scanner controls, emblem list, candidate-based ammo editor, framebuffer capture, and PS3 notification UI. Pure memory parsers/converters are covered by `tests/memory-features.test.cjs`. Unsupported RPC-only functions and the other-player IP/geolocation archive are documented in `docs/SOURCE-COMPARISON.md`. |
| Scriptable client | `rtmctl` exposes status, bounded reads, 1–8 byte writes with exact readback, and notifications without a browser window. Go tests use an isolated HTTP fake and reject public IPs, invalid widths, malformed responses and readback mismatches. |

## Automated checks

The suites below passed before the Overwalls and local RTM source ports. The
current run also includes regression tests for the newly added memory helpers.

- `node --test tests/*.test.cjs`: production-function regressions, transport,
  operations, backup schema and rollback tests.
- `go test ./...`: host target/auth/status tests plus a Node-driven production
  frontend integration against the authenticated host and an isolated fake PS3.
  The frontend test rejects requests outside its local host origin.
- `build.ps1`: runs those suites and tests the built binary's configuration,
  embedded assets and rejected requests on an ephemeral loopback port.
- Syntax checks run on every source JavaScript file.

## Manual UI evidence

The current browser preview exercised the actual application controls without a
console connection: settings and diagnostics exports; position save, edit,
delete, undo, export, import/duplicate skip; settings restore and reload. Phone
dark/light inspection at a 390px viewport showed a 375px content area with no
document horizontal overflow and all five original tabs visible. That visual
inspection predates the sixth Overwalls tab and does not validate the new
section's responsive layout. The replacement Edge app launched and exposed the
correct local origin and expected controls through Windows accessibility.
Native screenshot capture timed out; visual evidence is from the same frontend
rendered in the browser.

## Practical limits

No live PS3 memory operations or in-game effects were tested. Mock-console
readback proves the app's request, parser and state behavior; it does not prove
every recovered game address matches a particular console/game build. Hook
installation checks the expected instruction before patching and stops on an
unsupported value. Movement, loading and notification delivery require observed
game behavior beyond memory acknowledgement. The replacement host does not
implement original CCAPI transport or undocumented RPC parity. Module loading
uses the fixed webMAN PS3MAPI command path, but actual plugin loading and game
compatibility still require a live console and matching module build.

The framebuffer offsets, emblem scan location, and ammo signature come from the
local RTM source. They have not been validated against a live console. A saved
pause byte is kept locally if screenshot recovery fails so the user can reconnect
to that console and restore the value.
