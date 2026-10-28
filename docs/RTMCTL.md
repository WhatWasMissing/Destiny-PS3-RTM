# rtmctl command-line client

`rtmctl.exe` allows scripts to use the RTM's local PS3MAPI operations without
opening the desktop window. It connects directly to a private IPv4 PS3 running
webMAN MOD on port 80. Destiny's process ID defaults to `0x1010200`.

```powershell
.\dist\rtmctl.exe status --ip 192.168.1.100
.\dist\rtmctl.exe read --ip 192.168.1.100 --address 0030D230 --length 4
.\dist\rtmctl.exe write --ip 192.168.1.100 --address 0030D230 --hex 3DCCCCCD
.\dist\rtmctl.exe notify --ip 192.168.1.100 --message "Hello from RTM"
.\dist\rtmctl.exe read --ip 192.168.1.100 --address 0030D230 --length 4 --json
.\dist\rtmctl.exe actor-pool --ip 192.168.1.100 --json
.\dist\rtmctl.exe actor-candidates --ip 192.168.1.100 --json
```

Addresses and values use hexadecimal. Reads are limited to 64 KiB per request;
writes are limited to 1–8 bytes and print a verified result only when a fresh
read matches every written byte. If the write reaches the console but readback
fails, the command reports that the result is unconfirmed. Notification output
confirms the request was dispatched, not that the PS3 displayed it.
Add `--json` to a command for one JSON result object. Errors return a nonzero
exit code and a JSON error object.

`actor-pool` reads only the 20-byte BLES 01.24 actor-pool descriptor at
`0x01A1E160`. It verifies the five-anchor build profile and full 64-byte actor
path before and after the sample, then discards a changed descriptor. An
initialized descriptor must have capacity 40, stride `0x28D0`, an aligned
storage pointer and a bounded backing span. A null pointer with zero or expected
dimensions is reported as uninitialized; unexpected dimensions are refused.
No backing records are read; this is not a loaded-enemy list or
proof that a tag can spawn. The command accepts no custom read address or size.

`actor-candidates` follows the BLES actor allocator's bitmap and reads no more
than 40 allocated slots. It verifies the actor allocation and datum code, the
active-player pointer, and the descriptor/header/bitmap/identity values before
returning a sequential snapshot. Each row includes the full actor datum and
selected fields from the record's first `0x48` bytes. The unit datum and member
owner fields are candidates/raw values; this command does not classify enemies,
follow `+0x226C`, choose a spawn tag, or write memory. It accepts only `--ip`
and `--json`; one request is bounded to 64 KiB and the overall scan to 40 slots.
The command can take up to 45 seconds on a slow PS3MAPI link.

Every memory write first checks the five-anchor BLES01857 01.24 runtime
signature profile documented in `ADDRESS-AUDIT.md`. A mismatch or unreadable
signature blocks the write. The profile is not a full EBOOT hash or title-region
check; read commands continue to work on builds that do not match.

The client rejects public, host-name and IPv6 targets, follows no redirects,
and does not expose generic function-call routes. It uses only the PS3MAPI
memory read/write and system notification endpoints.
