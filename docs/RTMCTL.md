# rtmctl command-line client

`rtmctl.exe` allows scripts to use the RTM's local PS3MAPI operations without
opening the desktop window. It connects directly to a private IPv4 PS3 running
webMAN MOD on port 80. Destiny's process ID defaults to `0x1010200`.

```powershell
.\dist\rtmctl.exe status --ip 192.168.0.62
.\dist\rtmctl.exe read --ip 192.168.0.62 --address 0030D230 --length 4
.\dist\rtmctl.exe write --ip 192.168.0.62 --address 0030D230 --hex 3DCCCCCD
.\dist\rtmctl.exe notify --ip 192.168.0.62 --message "Hello from RTM"
.\dist\rtmctl.exe read --ip 192.168.0.62 --address 0030D230 --length 4 --json
```

Addresses and values use hexadecimal. Reads are limited to 64 KiB per request;
writes are limited to 1–8 bytes and print a verified result only when a fresh
read matches every written byte. If the write reaches the console but readback
fails, the command reports that the result is unconfirmed. Notification output
confirms the request was dispatched, not that the PS3 displayed it.
Add `--json` to a command for one JSON result object. Errors return a nonzero
exit code and a JSON error object.

The client rejects public, host-name and IPv6 targets, follows no redirects,
and does not expose generic function-call routes. It uses only the PS3MAPI
memory read/write and system notification endpoints.
