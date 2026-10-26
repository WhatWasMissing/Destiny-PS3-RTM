# Destiny RTM frontend

This frontend started from the exact recovery of `DestinyRTM(3).exe` on
25 October 2026 and has since been improved on the reliability branch.
It includes the Home / Mods / Player / Overwalls / Tools / All tabs,
character banner, Quick Access, Character Utilities, and saved teleport fixes.

The original recovery is preserved in Git commit
`e36edf76a50e30824eb972de2c03c5f9a0f34613`. Its SHA-256 is recorded in
`provenance.json`; that record describes the original recovery, not subsequent
edits on development branches. The working frontend now loads transport code
from `js/transport.js`. Serve the whole `source/` directory when previewing. The previously edited Android source was an older
version and is not included here.

The original Go host was not recovered. A replacement host and build script are
included at repository root, providing readable local HTTP transport without
reproducing CCAPI or claiming undocumented RPC capabilities. Console operations
require a compatible PS3 connection and game build.

The repository's proprietary no-redistribution LICENSE applies to material
owned by WhatWasMissing. Third-party rights remain with their owners.


Desktop builds are now available through the replacement Go host. See
[desktop build instructions](../docs/DESKTOP-BUILD.md). Original host source
remains unrecovered; the replacement does not imply native API parity.

Overwalls source behavior is ported in `js/overwalls-tools.js`; see the
[feature coverage and attribution](../docs/OVERWALLS-PORT.md). Its fixed
coordinates, hook bytes and plugin signatures belong to the referenced game
build and are not live-console verified.
