# PS3 Safe Mode and RTM hook investigation

## Finding

The current source contains no direct write path to PS3 firmware or system
storage in the reviewed connection and hook code. Its game modifications use
PS3MAPI memory writes against the Destiny process (`0x1010200`). A bad or
conflicting live patch could plausibly freeze or crash Destiny; if that leads
to a forced power-off, a later storage-recovery prompt is an indirect possible
consequence. The source evidence does not establish that this caused the
reported Safe Mode boots.

Sony describes PS3 Safe Mode as a minimal startup path for cases including the
XMB failing to appear, file-system or database repair failing or stalling, and
system-software update problems. This points to a startup/storage recovery
state, not a normal symptom of a PS3MAPI link dropping. See Sony's
[PS3 Safe Mode guide](https://www.playstation.com/ja-jp/support/hardware/ps3-safe-mode/)
and [system-storage restore guidance](https://manuals.playstation.net/document/en/ps3/current/settings/restoreps3.html).

## RTM-specific evidence

- Before this change, `source/js/connection.js` installed the player hook after
  every successful connection. The hook patches Destiny at `0x001C4110` to
  branch to `0x010D8EC0`.
- The local Overwalls/AFKCrucible source also writes a physics hook at the same
  branch and cave addresses. Other server-cheat hook modes in this RTM reuse
  that location. Running two RTMs or hook modes together can race over shared
  executable memory.
- A local `soulsAPI.dll` used by older RTM source copies also installs an RPC
  hook and clears a memory region, without a build guard or rollback in the
  reviewed wrapper. This shows older tools can patch on connection too; their
  behavior varies by build, so “older RTM” is not a uniform control group.
- The current Game Speed writer targets `0x30586864`, while the old player-hook
  comment names `0x030606E4`. The dependency between them remains unverified.

## Mitigation in this source tree

- Linking is transport-only; it no longer installs the player hook.
- The experimental player hook requires an explicit action and confirmation.
- Hook switching checks the branch instruction and shared code cave, refuses
  unrecognized data, writes the stub before activation, and only restores a
  recognized payload.
- The UI warns that the hook overlaps Server Cheats and Overwalls and advises
  closing other RTMs first.
- Settings now include Quiet connection for an idle comparison. It keeps one
  read-only PS3MAPI version query during initial Link, skips the beep commands
  and XMB popup, stops recurring version checks, and disables auto reconnect.
  An explicitly requested Restart still follows its relink path. This option
  isolates background traffic; it does not identify the cause by itself.

These checks reduce accidental writes but cannot make PS3MAPI's separate reads
and writes atomic. Do not run another RTM that can touch the same hook site at
the same time.

## Isolating the cause

Use the same console, game build, firmware, network, and plugins for each
comparison. First leave the RTM linked and idle with Quiet connection enabled
and no memory features enabled; one initial PS3MAPI version query still occurs.
Then, only if that remains stable, disable Quiet connection and compare idle
behavior before enabling one hook mode at a time. Record the last action and
time before any freeze/reboot, and capture the exact Safe Mode message. A
failure during quiet idle use points away from this app's background requests
and game hook; it warrants checking the console's storage, power, firmware, and
plugins. A failure that repeats only after one hook mode makes that patch a
stronger suspect, but still does not prove that it caused a later Safe Mode
prompt.
