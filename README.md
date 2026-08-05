# Destiny RTM

Destiny RTM is a real-time memory (RTM) tool for PS3, used for teleporting, memory read/write, and other in-game manipulation via [PS3MAPI](https://github.com/aldostools/webMAN-MOD). It's available as a Windows executable and an Android APK, both built from the same core web app.

## Requirements

- A PS3 running **[webMAN MOD](https://github.com/aldostools/webMAN-MOD)** (CFW required)
- **PS3MAPI** enabled in webMAN MOD's settings
- Your PS3 and the device running this tool (PC or phone) must be connected to the **same local network**
- Your PS3's local IP address (found in webMAN MOD or your router's device list)

## Enabling PS3MAPI

1. Install [webMAN MOD](https://github.com/aldostools/webMAN-MOD) on your CFW PS3, if not already installed.
2. From the XMB, go to webMAN's settings.
3. Enable **PS3MAPI**.
4. Note your PS3's IP address — you'll enter this into the app.

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

### Android (.apk)

1. Download `DestinyRTM.apk` to your Android device.
2. If prompted, allow installation from unknown sources (required since this isn't distributed via the Play Store).
3. Open the app, enter your PS3's IP address, and connect.
4. Make sure your phone is on the **same Wi-Fi network** as your PS3 — mobile data will not work.

## Troubleshooting

- **Can't connect:** Double-check your PS3's IP address and that PS3MAPI is enabled in webMAN MOD.
- **Connects on PC but not on Android:** Confirm your phone is on the same local network as the PS3, not using mobile data or a different Wi-Fi/VPN.
- **Nothing happens after entering IP:** Restart PS3MAPI from webMAN MOD's settings and try again.

## Disclaimer

This tool directly reads and writes PS3 process memory. Use at your own risk. It is intended for use with your own console and legally owned games. The developers are not responsible for any damage, bans, or save corruption resulting from its use.

## Credits

- [webMAN MOD](https://github.com/aldostools/webMAN-MOD) by aldostools
- PS3MAPI
- **[Soul](https://github.com/PappaSoul)** — some features used in this mod, and a big help in learning how to build it
- **[KiPOD](https://github.com/kipod8)** — some features used in this mod, and a big help in learning how to build it
