# Desktop build

Requirements: Windows, Go 1.23 or newer, Node.js with the built-in test runner,
and Microsoft Edge (or a default browser). No external Go dependencies are used.

Run `./build.ps1` from PowerShell. If Go is not on PATH, pass `-Go` with the
absolute path to `go.exe`. The script runs Go and JavaScript tests and writes
`dist/DestinyRTM.exe`, embedding the complete `source/` directory.

Run the executable to open an Edge app window. If Edge cannot be located, the
default browser opens instead. The host runs until its console is closed.
Use `DestinyRTM.exe -no-open` for a host-only run. Its default address is
`http://127.0.0.1:8767`. Keep that origin stable to retain browser storage.
If the port is occupied, close the existing host or specify a different loopback
address with `-listen`. Changing the port creates a different browser storage
origin; export settings before moving between origins.

This is a replacement host, not recovered original Go code. It serves local
assets and proxies supported webMAN endpoints to private IPv4 addresses on
port 80. It does not implement CCAPI or add undocumented RPC capabilities.
It uses request authentication, validates Host/Origin, disallows redirects,
caps response size, bypasses system HTTP proxies and bounds upstream timeouts.
No console data or requests are sent to an external service.

The frontend detects this host through `/host-config`. Ordinary static hosting
continues to use direct console HTTP, subject to browser cross-origin rules.
`source/index.html` alone is no longer a standalone file: distribute the entire
source directory or use the embedded executable.

Verification so far: Go HTTP tests use a local fake console; packaged executable
smoke checks verify config, static JavaScript and unauthorized-request rejection.
Live-console read/write verification and native window inspection remain pending.
The older executable and the recovered baseline are preserved separately.
