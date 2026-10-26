# Overwalls review — initial findings

Inspected local project:
E:/Destiny PS3 RTMs/fly hack/OverwallsTool - Hybrid/

This is a decompiled .NET Framework 4.7.2 Windows Forms application. The main
implementation is AFKCrucible/Form1.cs. OverwallsTool/Form1.cs is generated
resource access code, not the main application. soulsAPI is referenced and
bundled through Costura; its transport and call behavior require further review.

## Evidence so far

- Connect handler, lines 306–311: opens soulsAPI connection UI, then installs
  physics and plate hooks. Hook installation is coupled to connecting.
- Teleport, lines 976–985: reads player pointer, packs three big-endian floats,
  writes coordinates, then selects an internal or external activation path.
- FloatsToArray, lines 997–1012: explicitly handles host endianness.
- LoadActivity, lines 1025–1028: writes activity index to a fixed address.
- GridButton_Click, lines 1298–1316: named coordinate shortcuts; blank entries
  open a coordinate editor. Right-click deletion uses a confirmation dialog.
- Scenario loader begins at line 1332: XML scenario import. Review schema and
  validation before recommending compatibility support.
- UI includes AFK farming, activity loader, teleports, adjustable delays and
  CCAPI-specific functionality. These are not proof of browser compatibility.

## Candidate recommendations to validate

- Scenario XML import/export and named teleport shortcut editing are promising
  data/UI ports. Existing RTM folders should be preserved rather than replaced.
- Internal/external teleport activation may improve compatibility, but requires
  matching game version and pointer/readback evidence before enabling writes.
- Hooks and CCAPI-dependent actions require native transport analysis and cannot
  be treated as ordinary HTTP endpoint ports.

Review is incomplete. No Overwalls files have been copied into the public repo,
no hook code has been executed, and no new console features have been enabled.
