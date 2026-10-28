# Destiny RTM

## Source backup and license

The recovered frontend is preserved in commit `e36edf7`. The working app is in
[source/](source/README.md), with a replacement Go desktop host and a repeatable
[Windows build](docs/DESKTOP-BUILD.md).

The reliability branch adds bounded and verified writes, session-aware reconnect,
settings backup/restore, improved teleport management and a responsive redesign.
The Overwalls feature port adds source-specific tower and activity shortcuts,
scenario XML, routines, inventory tools, and Flyhack support. The local-source
comparison adds missing 1.24 toggles, an emblem editor, a candidate-based ammo
editor, framebuffer screenshots, and the scanner/notification controls. See the
[Overwalls port map](docs/OVERWALLS-PORT.md), [Blackrock feature port](docs/BLACKROCK-FEATURE-PORT.md), [RTM source comparison](docs/SOURCE-COMPARISON.md),
[initial source review](docs/OVERWALLS-REVIEW.md), and [implementation evidence](docs/IMPLEMENTATION.md).
The experimental Blackrock Spawn port adds tag selection, named placement modes,
and a BLES 01.24 operation-1 executor; its packet/UI mapping is statically traced,
but a live PS3 spawn has not been confirmed.
Automated checks use an isolated fake console; live game compatibility still
depends on the console/game build.
The scriptable client and its command reference are in [docs/RTMCTL.md](docs/RTMCTL.md).
The direct agent-tool server and Codex setup instructions are in
[docs/RTM-MCP.md](docs/RTM-MCP.md). Its named tools include the read-only BLES
01.24 item-pool snapshot and source-backed staged ISO launch workflow; HTTP
acknowledgement does not confirm Destiny booted.

**Proprietary — all rights reserved. Redistribution, rehosting, and sharing modified
versions require written permission.** See [LICENSE](LICENSE) for the full terms,
including GitHub platform rights and third-party exclusions.

https://discord.gg/dHGNkmj39

Destiny RTM is a real-time memory (RTM) tool for PS3, used for teleporting, memory read/write, and other in-game manipulation via [PS3MAPI](https://github.com/aldostools/webMAN-MOD). It's available as a Windows executable and an Android APK, both built from the same core web app.

## Requirements

- A PS3 running **[webMAN MOD](https://github.com/aldostools/webMAN-MOD)** (CFW required/Hen)
- **PS3MAPI** enabled in webMAN MOD's settings
- Your PS3 and the device running this tool (PC or phone) must be connected to the **same local network**
- Your PS3's local IP address (found in webMAN MOD or your router's device list)

## Enabling PS3MAPI

1. Install [webMAN MOD](https://github.com/aldostools/webMAN-MOD) on your CFW/Hen PS3, if not already installed.
2. From the XMB, go to webMAN's settings.
3. Enable **PS3MAPI**.
4. Note your PS3's IP address - you'll enter this into the app.

## Downloads

| Platform | File |
|---|---|
| Windows | `DestinyRTM.exe` |
| Android | `DestinyRTM.apk` |

Grab the latest build from the [Releases](../../releases) page.

## Usage

### Windows (.exe)

1. Download and run `DestinyRTM.exe`.
2. Enter your PS3's IP address in the field provided.
3. Connect and use the available tools.

### Command-line tools

The current build record and verification status are in
[`docs/RELEASE-2026-10-28-r16.md`](docs/RELEASE-2026-10-28-r16.md). The matching
prerelease files are in `release-artifacts/2026-10-28-r16/`. Earlier same-day
prerelease records remain in the adjacent `RELEASE-2026-10-28*.md` files.

The Windows build also produces `dist/rtmctl.exe` for scripted use without
opening the RTM interface. It supports private-network status checks, bounded
memory reads, a read-only BLES 01.24 item-pool snapshot, 1–8 byte writes with
exact readback verification, and PS3 system notifications. It uses Destiny
process ID `0x1010200`; it does not expose undocumented function-call routes.

```powershell
.\dist\rtmctl.exe read --ip 192.168.1.100 --address 0030D230 --length 4
.\dist\rtmctl.exe inventory-pool --ip 192.168.1.100 --json
.\dist\rtmctl.exe actor-pool --ip 192.168.1.100 --json
.\dist\rtmctl.exe actor-candidates --ip 192.168.1.100 --json
.\dist\rtmctl.exe write --ip 192.168.1.100 --address 0030D230 --hex 3DCCCCCD
```

### Android (.apk)

1. Download `DestinyRTM.apk` to your Android device.
2. If prompted, allow installation from unknown sources (required since this isn't distributed via the Play Store).
3. Open the app, enter your PS3's IP address, and connect.
4. Make sure your phone is on the **same Wi-Fi network** as your PS3 - mobile data will not work.

## Troubleshooting

- **Can't connect:** Double-check your PS3's IP address and that PS3MAPI is enabled in webMAN MOD.
- **Connects on PC but not on Android:** Confirm your phone is on the same local network as the PS3, not using mobile data or a different Wi-Fi/VPN.
- **Nothing happens after entering IP:** Restart PS3MAPI from webMAN MOD's settings and try again.

## Disclaimer

This tool directly reads and writes PS3 process memory. Use at your own risk. I am almost certain that Destiny legacy will never ban you, but the chance is non zero if you do not understand how to protect yourself when playing on a modded console, so invest some time to learn this if you do not already know.
AI Disclaimer: Some AI was used in the creation of this project, mostly formatting.

## Credits

- [webMAN MOD](https://github.com/aldostools/webMAN-MOD) by aldostools
- PS3MAPI
- **[KiPOD](https://github.com/kipod8)** - some features used in this mod
- **[Soul](https://github.com/PappaSoul)** - some features used

## Known issues
-Join discord or see known issues page
