# Shared server-hook source audit

## Source match

The local `D:\DestinyRTM\index.html` source has SHA-256
`E20B5F3F7E315174DB96A73566F98F4288C841A8240CB5BE242735BADCBC953E`. Its
Base64 string table decodes to three PPC payloads used by the current RTM:

| Source label | Bytes | SHA-256 of decoded hex text | Current RTM constant |
|---|---:|---|---|
| Void Enemies | 292 | `a839ff917027106f7b3e5f357b4dcf37f6b64baac650d0d8009b789800aea79d` | `VOID_ON_HEX` |
| Relative OPK | 332 | `1283cd8c147e9098f7e1926064f9a3034b36435f6451370b3e7f33cec5a3ff39` | `ROPK_ON_HEX` |
| Off | 116 | `9838ec61d3c83fd09349cfd3ab34e9fbe2093d9c898e1d23768cc88312df28f3` | `OFF_HEX` |

All three decoded values match the app constants byte-for-byte. This confirms
the local source lineage for these code images, but its file does not identify
a Destiny title ID or game build, and the labels do not establish runtime
effects.

## BLES 01.24 hook-site and scratch-region evidence

The hash-verified BLES01857 01.24 EBOOT
(`E7F100B38BBAF6C23F71B390FDB103D31B427187C38DCFBD47E09C94C4A44206`) has
`30640140` at VA `0x001C4110`, matching the hook's expected original
instruction. Its file-backed segment contains 384 zero bytes beginning at VA
`0x010D8EC0`, so the full 332-byte largest payload fits inside this verified
zero range. This is static ELF evidence; it does not prove the same bytes are
still clear in a live process.

## Region-length correction

The app previously used the 268-byte Player Hook stub-plus-tail length when
preflighting, recognizing, and clearing the shared cave. That was shorter than
the 292-byte Void payload and 332-byte Relative OPK payload. It could not
recognize those complete active payloads on a later mode switch, and it wrote
bytes past the range it had checked. The app now derives the shared region from
the largest known payload and Player Hook region (332 bytes), accepts only a
blank cave or one of the exact known payloads with zero padding, writes the new
payload, clears its remaining bytes through the verified region, and activates
the branch last. An integration check covers Void → Relative OPK → Off and
guards the first byte beyond the region.

The larger region is a containment correction, not a confirmation that Void
Enemies has the advertised effect or that Relative OPK's coordinate fields
have been semantically resolved. Neither has been exercised on a live BLES
01.24 console in this pass.
