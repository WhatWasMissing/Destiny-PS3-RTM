# Desktop build

Requirements: Windows, Go 1.23 or newer, Node.js with the built-in test runner,
and Microsoft Edge (or a default browser). No external Go dependencies are used.

Run `./build.ps1` from PowerShell. If Go is not on PATH, pass `-Go` with the
absolute path to `go.exe`. The script runs Go and JavaScript tests and writes
`dist/DestinyRTM.exe`, embedding the complete `source/` directory, and
`dist/rtmctl.exe`, a standalone command-line client for webMAN PS3MAPI, and
`dist/rtm-mcp.exe`, a local MCP server for direct agent-tool access. The build
also writes `dist/build-manifest.json` with all executable hashes and embedded
source hashes, and syntax-checks every JavaScript module before packaging.

Run the executable to open an Edge app window. If Edge cannot be located, the
default browser opens instead. The host runs until its console is closed.
Use `DestinyRTM.exe -no-open` for a host-only run. Its default address is
`http://127.0.0.1:8767`. Keep that origin stable to retain browser storage.
If the port is occupied, close the existing host or specify a different loopback
address with `-listen`. Changing the port creates a different browser storage
origin; export settings before moving between origins.

This is a replacement host, not recovered original Go code. It serves local
assets and proxies supported webMAN endpoints to private IPv4 addresses on
port 80, including the constrained ISO launch sequence. It does not implement
CCAPI or add undocumented RPC capabilities.
It uses request authentication, validates Host/Origin, disallows redirects,
caps response size, bypasses system HTTP proxies and bounds upstream timeouts.
No console data or requests are sent to an external service.

`rtmctl` talks directly to a private IPv4 target on port 80. It provides
readback-verified memory writes and system notifications without opening the
desktop UI. Its target, process and write-size checks match the RTM's intended
Destiny process scope. Run `dist/rtmctl.exe help` for commands.

`rtm-mcp` exposes status, bounded reads, verified writes, the source-backed
brightness presets, system notifications and the staged ISO launch workflow
over MCP stdio. Register it with Codex using the steps in [RTM-MCP.md](RTM-MCP.md).
Build/protocol smoke checks use no live console connection.

The frontend detects this host through `/host-config`. Ordinary static hosting
continues to use direct console HTTP, subject to browser cross-origin rules.
`source/index.html` alone is no longer a standalone file: distribute the entire
source directory or use the embedded executable.

Verification: Go HTTP tests use a local fake console. The desktop integration
test runs production frontend connection, transport, memory parser and toggle
code against the authenticated local host, checking readback, mismatches,
partial failures, disconnect and diagnostics. Packaged executable smoke checks
verify config, static JavaScript and unauthorized-request rejection. The Edge
app window launches and exposes expected controls through accessibility;
native screenshot capture timed out. Browser visual checks cover the same
frontend in desktop and phone layouts. Live-console verification remains pending.
The older executable and the recovered baseline are preserved separately.
