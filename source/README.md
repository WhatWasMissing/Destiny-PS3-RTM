# Recovered desktop frontend

`index.html` is the exact frontend recovered from `DestinyRTM(3).exe` on
25 October 2026. It includes the Home / Mods / Player / Tools / All tabs,
character banner, Quick Access, Character Utilities, and saved teleport fixes.

The original recovery is preserved in Git commit
`e36edf76a50e30824eb972de2c03c5f9a0f34613`. Its SHA-256 is recorded in
`provenance.json`; that record describes the original recovery, not subsequent
edits on development branches. The working frontend now loads transport code
from `js/transport.js`. Serve the whole `source/` directory when previewing. The previously edited Android source was an older
version and is not included here.

This is a frontend backup, not a complete buildable desktop project. The Go
host source and build configuration have not been recovered. Console operations
require the original desktop integration and a compatible PS3 connection.

The repository's proprietary no-redistribution LICENSE applies to material
owned by WhatWasMissing. Third-party rights remain with their owners.


Desktop builds are now available through the replacement Go host. See
[desktop build instructions](../docs/DESKTOP-BUILD.md). Original host source
remains unrecovered; the replacement does not imply native API parity.
