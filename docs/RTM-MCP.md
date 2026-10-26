# RTM tools for MCP clients

`rtm-mcp.exe` exposes direct, named tools over MCP's stdio transport. It talks
to webMAN MOD through the same bounded PS3MAPI client as `rtmctl`; it does not
open the desktop app or automate its UI.

## Tools

| Tool | What it does |
|---|---|
| `rtm_status` | Checks webMAN reachability. It does not confirm Destiny is running. |
| `rtm_read_memory` | Reads 1–65536 bytes from Destiny PID `0x1010200`. |
| `rtm_read_player_position` | Follows the Overwalls player pointer at `0x0190EBB0` and decodes X/Y/Z as big-endian Float32 values at pointer offsets `+0x140/+0x144/+0x148`. Read-only; live build compatibility and gameplay meaning remain unconfirmed. |
| `rtm_write_memory` | Writes 1–8 bytes and succeeds only after exact readback. It does not call game functions. |
| `rtm_set_brightness` | Writes one of the local 1.24 source presets at `0x006101C0` and verifies the bytes. The in-game effect remains unobserved. |
| `rtm_notify` | Dispatches a webMAN system notification; display is not confirmed. |
| `rtm_launch_game` | Mounts one ISO from `/dev_hdd0/PS3ISO/`, waits for webMAN, returns to the game list, and starts the mounted game. HTTP acknowledgement is reported per stage; game start is not confirmed. |

Every tool requires the PS3's private IPv4 address. The server fixes webMAN to
port 80 and Destiny to PID `0x1010200`, disables redirects and proxy use, and
does not expose function-call, client-list, IP-collection, or geolocation
operations. Read/write limits and target validation are enforced by the shared
client as well as the MCP tool layer.

`rtm_launch_game` defaults to `/dev_hdd0/PS3ISO/Destiny.iso`. Its optional
`iso_path` must be a simple `.iso` filename directly inside `/dev_hdd0/PS3ISO/`;
no other webMAN mount paths are accepted. It follows the recovered source's
3-second mount delay and 1-second game-list delay. An acknowledged `/play.ps3`
response does not prove that Destiny finished starting.

## Build and register with Codex

Build the executable from the repository root:

```powershell
& 'C:\path\to\go.exe' build -trimpath -o .\dist\rtm-mcp.exe .\cmd\rtm-mcp
```

Register the local stdio server with Codex, replacing the path with this
checkout's absolute path:

```powershell
codex mcp add destiny-rtm -- 'C:\path\to\Destiny RTM\dist\rtm-mcp.exe'
codex mcp list
```

Codex stores this server in its local MCP configuration. Restart the app or
start a new task to load the tools. The current task cannot dynamically add
tools to its own already-established tool list. After registration, call
`rtm_status` with the PS3's private IPv4 address, then use `rtm_read_memory`
or a specific write tool as needed.

The stdio stream carries only MCP JSON-RPC messages; diagnostic errors go to
stderr. No console connection is made during build or protocol smoke tests.

Protocol references: [stdio transport](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports),
[tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools),
and [Codex MCP setup](https://learn.chatgpt.com/docs/extend/mcp).
