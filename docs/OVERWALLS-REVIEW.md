# Overwalls architecture and port assessment

Inspected local project:
E:/Destiny PS3 RTMs/fly hack/OverwallsTool - Hybrid/

This is a decompiled .NET Framework 4.7.2 Windows Forms application. The main
implementation is AFKCrucible/Form1.cs. OverwallsTool/Form1.cs is generated
resource access code, not the main application. soulsAPI is referenced and
bundled through Costura. This review covers its usage from the decompiled
application and Retrofit, not proof of every bundled native transport behavior.

## Inspected behavior

- Connect handler, lines 306–311: opens soulsAPI connection UI, then installs
  physics and plate hooks. Hook installation is coupled to connecting.
- Teleport, lines 976–985: reads player pointer, packs three big-endian floats,
  writes coordinates, then selects an internal or external activation path.
- FloatsToArray, lines 997–1012: explicitly handles host endianness.
- LoadActivity, lines 1025–1028: writes activity index to a fixed address.
- GridButton_Click, lines 1298–1316: named coordinate shortcuts; blank entries
  open a coordinate editor. Right-click deletion uses a confirmation dialog.
- Scenario loader begins at line 1332: XML scenario import. The accompanying
  VoG.xml sample confirms ArrayOfButtonData entries with Name/Text/X/Y/Z fields.
- UI includes AFK farming, activity loader, teleports, adjustable delays and
  CCAPI-specific functionality. These are not proof of browser compatibility.

## Port boundaries

- Scenario XML import/export and named teleport shortcut editing are promising
  data/UI ports. Existing RTM folders should be preserved rather than replaced.
- Internal/external teleport activation may improve compatibility, but requires
  matching game version and pointer/readback evidence before enabling writes.
- Hooks and CCAPI-dependent actions require native transport analysis and cannot
  be treated as ordinary HTTP endpoint ports.

## Port recommendations after reviewing main behavior

| Feature | Source evidence | Recommendation |
|---|---|---|
| XML scenarios | SaveButtons/LoadButtons/ApplyButtonData at 1346–1388; ButtonData has Name, Text, X, Y, Z | Best next data port: import into RTM folders, mapping Text to the position name. Validate finite coordinates, preserve axis order, skip duplicates and optionally export compatible XML. |
| Named shortcuts | GridButton_Click and ApplyUserInput, 1298–1316 and 1390–1403 | New position editing covers part of this. Optional shortcut-grid presentation can coexist with folders. |
| Internal/external teleport activation | Teleport, 976–985 | Conditional port: it writes three big-endian floats and selects an internal fixed field or a player-relative activation field. Requires matching game/plugin version and pointer/readback verification. |
| Plugin readiness diagnostics | button125/button126, 1978–2005 | High priority: independently check the main-hook signature and the SPRX test string. A console connection alone does not prove readiness. |
| Flyhack settings | checkBox26, comboBox3, setFlyVel/setFlyTpDis/setFlyADead, 1868–2007 | Conditional port after readiness checks: enable, velocity, displacement and axial deadzone. Fixed fields belong to the plugin and are not universal game offsets. |
| Transport-aware slider updates | trackBar6/7 event handlers, 1907–1952 | Port the pattern of sending once on release on constrained transports; debounce other paths. The source tests an API selector rather than measured latency. |
| Activity favorites | button32/98/95/96 and adjacent handlers, 1428–1450 | Reuse the existing activity catalog. Verify indexing because other handlers add one to entered values. |
| AFK farming | checkBox1, 313 onward | Later project: replace timing chains with cancellable state transitions and observed load completion. Existing profiles use fixed machine-dependent delays and distinguish API capabilities. |
| Activity ID sweep | checkBox25, 1787–1799 | Experimental diagnostic only: bounded range, cancellation and per-step logging would be needed. |

### Retrofit comparison

The SPRX Retrofit copy adds a connected-state guard, a reflection-based module
loader and two runtime signature checks. It scans soulsAPI objects and candidate
methods rather than using a known typed loader API. Port the explicit readiness
checks; define a verified native loader interface before porting module loading.
Replace its blocking Thread.Sleep(500) with asynchronous bounded polling.

### Architecture and overlap

Forms own the UI state. soulsAPI handles connections and typed memory access;
Costura bundles dependencies. Features are mainly event handlers with fixed
addresses rather than a portable engine. Current Destiny RTM already covers
activity loading, teleports, FOV, gamespeed and multiple patch toggles, so these
should be compared and improved rather than duplicated.

The replacement Go host provides readable HTTP transport but does not implement
CCAPI or prove that the undocumented call endpoint exists. Game hooks, native
RPC and module loading need transport and build compatibility evidence. Local
currency/display writes also do not establish server-side persistence.

This assessment is based on source inspection, not live-console experiments.
No Overwalls source was copied into the public repo, no hook code was executed,
and no farming routines or new console features were enabled.
