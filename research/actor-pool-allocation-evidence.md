# BLES 01.24 actor-pool allocation and datum reconstruction

This is a static cross-check of Blackrock's host-side actor iterator against the
local BLES01857 01.24 PowerPC ELF. It identifies an allocator bitmap and a
full-datum reconstruction path. An allocated actor-pool slot is not by itself
proof of an enemy, a currently rendered object, or a spawnable tag.

## Inputs

| Input | SHA-256 |
| --- | --- |
| `E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF` | `E7F100B38BBAF6C23F71B390FDB103D31B427187C38DCFBD47E09C94C4A44206` |
| `D:\BlackrockPS3-preview-win64-2026-10-26\BlackrockPS3-release-final\blackrock-ps3.exe` | `C37AAE2ED3B291B1FD1474C44BC78F8CE872CA85A4F4C29A28EC16CE2C6F5F58` |
| Blackrock `data\world\pools.csv` | `A8F4F3AD69D2B630E8E35418AD3D7CE8E5BFEAA2C29611DFBC9081E59C4A5238` |
| D1 Reimagined `notes\ghost-light-0124.md` | `98835B33E123248C40080CCA6DAACF58E4D87782B4CFBB346F3A0285F312C443` |
| D1 Reimagined `notes\squad-member-counts-0124.md` | `C3697F7B04B17E56ED163AA47D6CF07F927AC714B4BCA7E818BCB98FCCF5B0FA` |

The D1 Reimagined checkout examined here is at
`E:\DestinyResearch\d1reimagined-source`, commit
`f85cc9a86543d7a4bc4202d613c43b817e2237ce`. Its squad-count note states that
raw definitions and memory snapshots are private. The JOIN observations below
are reported RPCS3 evidence, not a replay of those snapshots on this machine.

## Pool layout and allocation bit

The BLES actor initializer at `0x0015B230..0x0015B264` passes descriptor
`0x01A1E160`, capacity 40 and record stride `0x28D0` to `0x000BE148`. The
shared nested initializer stores an allocator/header pointer at descriptor
`+0`, a backing pointer at `+8`, stride at `+0xC`, and capacity at `+0x10`
(`0x000BDECC`, `0x000BDEDC`, `0x000BDEE4`, `0x000BDEE8`). Descriptor fields
`+0x18`, `+0x1C`, `+0x20`, and `+0x30` participate in identity reconstruction;
they must be read from the live descriptor rather than inferred from the
record stride.

Blackrock's generic pool decoder `0x1402FEA60` reads those same fields from a
`0x34`-byte big-endian descriptor. Its allocated-slot iterator
`0x140305360`, called from the AI Actors diagnostic at `0x14005FE62`, uses
the following chain:

1. Read a 32-byte allocator header through descriptor `+0`. The big-endian
   u16 at header `+0x1C` supplies an index limit; Blackrock requires 1–8192
   and requires the slot index to be below it and descriptor capacity.
2. Read the big-endian pointer at header `+8` to a 12-byte bitmap descriptor.
   Its u32 fields at `+0`, `+4`, `+8` are byte length, bit count, and bitmap
   words pointer respectively. Blackrock requires bit count at most 8192, the
   slot below that count, byte length at least `ceil(bitCount/32)*4` and at
   most `0x1000`, and aligned/bounded pointers.
3. Read the big-endian u32 at `bitmapWords + 4*(slot >> 5)`. The bit
   `1 << (slot & 31)` set is the allocation condition tested by the host at
   `0x140305514..0x140305539`. A clear bit is reported as unallocated.

This chain is also supported by the BLES code, not just Blackrock's labels.
`0x000BF64C` initializes the allocator, stores the supplied capacity at header
`+0x1A/+0x1C`, and stores the bitmap descriptor pointer at `+8`.
`0x000BF4B0` initializes the free list and clears allocation bits.
`0x001089DC..0x00108A78` consumes a free-list entry, sets that slot's bitmap
bit at `0x00108A60..0x00108A6C`, and decrements header `+0x18`; the release
path at `0x00107DC8..0x00107ECC` clears the same bit and increments `+0x18`.
Those operations support interpreting header `+0x18` as the available/free
count in these paths. A count sampled during mutation is not an atomic proof
of the bitmap contents.

This allocation bitmap is **distinct** from the bitmap at `0x01A21138` that
the actor cleanup path checks using record `+0x30`. The latter's bit meaning
is not established as actor-pool occupancy.

## Reconstructing the complete datum

For an allocated index, Blackrock reads a big-endian identity word at
`storage + index * descriptor[+0x1C] + descriptor[+0x18]`. The host checks
identity offset `<=0x100000`, identity stride in `1..0x10000`, four-byte
alignment, address overflow and the memory range before that read. Let
`identity = identityWord & descriptor[+0x20]`, and let `namespace` be the
descriptor u32 at `+0x30`. In u32 arithmetic, its reconstructed datum is:

```text
if (namespace & 0x40000000) == 0:
    datum = ((identity & 0xFF) << 23) | ((namespace & 0x3FF) << 13) | index
else:
    datum = 0x80000000 | ((identity & 7) << 27) | ((namespace & 0x3FFF) << 13) | index
```

The BLES ELF's helper `0x00108960..0x001089D8` independently performs this
same identity/mask/namespace construction. A separate deterministic simulation
of the PowerPC rotate-and-mask instructions matched the simplified formulas in
100,000 input cases across both branches; that checks the arithmetic, not live
game memory. The BLES release path increments
and masks the identity word at `0x00107E08..0x00107E40`, while allocation
also updates the identity at `0x00108AB8..0x00108AF4`. This is why a slot
index alone is not a stable actor identity. Blackrock rejects reconstructed
`0` and `0xFFFFFFFF`; that is its host policy and does not make index zero
invalid. When a caller supplies an actor datum, Blackrock compares the **full**
reconstructed datum with it at `0x14032AFDC..0x14032AFE3` and reports a
recycled handle on mismatch.

The D1 Reimagined JOIN99 and JOIN101 notes give actor datums `0x28F58000` and
`0x02F58000` in separate runs: both have low 13-bit index zero, while their
full values differ. These are run-specific examples, not reusable addresses
or a complete proof of what every high bit represents.

## Actor fields and the PS3 stride discrepancy

Blackrock's actor reader `0x14032ABA0` calls the allocator iterator, then
explicitly requires stride `0x28E0` at `0x14032AE0F`; its record read at
`0x14032B1D0` also multiplies by `0x28E0`. The verified BLES 01.24 initializer
instead supplies `0x28D0`. A BLES port must use its verified runtime stride;
successful connection to Blackrock does not prove that this actor-reader gate
works on this BLES executable. Individual actor field meanings and offsets
still need same-build runtime validation.

After its stride and full-datum checks, Blackrock reads 0x48 bytes of the
actor record and extracts fields `+0x24`, `+0x28`, `+0x30`, `+0x40` and
`+0x44` (`0x14032B1E8..0x14032B259`). It passes actor-record `+0x30` as a
datum to `0x1402FF780`, which uses the `sobjects` pool descriptor at
`0x01A1D9E0`, requiring stride `0xC0`. Its host resolver `0x1402FF650` checks
the low-13-bit index against capacity, then requires the sobject record's
`+0x0C` word to match the full supplied datum and bit `0x4` of the `+0x04`
word to be clear. These checks are host behavior, not an independently
verified enemy-type predicate.

Do not confuse actor-record `+0x30` with descriptor `+0x30`: the former is
associated with a unit datum in D1 Reimagined's JOIN99/101 observations;
the latter is a namespace word used for handle reconstruction. The BLES
accessor at `0x00191960` returns actor-record `+0x30`.

Actor-record `+0x40/+0x44` is an authored-member owner handle/offset pair in
the binding path. The BLES `0x00255A18` code populates it and writes the actor
datum at member `+0x174`; `0x0025604C..0x00256058` clears it during cleanup.
JOIN108 reports a physical count-only Ghost with member owner `+0x40` absent;
JOIN109 reports a bound Ghost with that owner present. Thus `+0x40 != -1`
cannot be a general live-actor or allocated-slot test.

Blackrock's AI Actors port check examines at most 64 indices and stops after
six allocated candidates (`0x14005FDF4..0x14005FE14`); its diagnostic result
is not a complete list. Its host range check `0x1402E8C30` accepts spans in
`[0x00010000, 0x01C80000)` or `[0x30000000, 0x80000000)`, with the computed
end allowed to equal the upper bound. That host policy is evidence for its
reader, not a guarantee that every address in either interval is readable.

## Implications for a read-only BLES snapshot

Use the five-anchor profile plus address-specific allocator/identity code
checks, validate the descriptor, header, bitmap metadata and every calculated
range before a read, and use u64 arithmetic for bounds. Sample the allocation
words and identities before and after the candidate reads; discard results if
the descriptor, header, bitmap or selected identity changes. Preserve the full
datum and report allocated *candidates* separately from any subsequently
validated unit or squad relationship. Such sequential reads are non-atomic:
an ABA change, a datum-generation wrap, or transient game-state changes can
still evade a before/after comparison. The reported JOIN captures remain
private, so a same-build live correlation is still needed before treating the
result as a complete list of currently loaded enemies.

## Reproduction

These commands only read the two local binaries; they do not connect to a PS3:

```powershell
$rz = 'E:\DestinyResearch\Tools\Rizin\bin\rizin.exe'
$elf = 'E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF'
$br = 'D:\BlackrockPS3-preview-win64-2026-10-26\BlackrockPS3-release-final\blackrock-ps3.exe'
Get-FileHash -Algorithm SHA256 $elf, $br
foreach ($address in '0x0015B230','0x000BDECC','0x000BF64C','0x000BF4B0','0x00108960','0x001089DC','0x00107DC8') {
  & $rz -q -e scr.color=false -a ppc -b 64 -E big -s $address -c 'pd 100' $elf
}
foreach ($address in '0x1402FEA60','0x140305360','0x1403056D7','0x14032AE0B','0x14032AFDC','0x14032B192','0x1402FF650','0x1402E8C30') {
  & $rz -q -e scr.color=false -a x86 -b 64 -s $address -c 'pd 100' $br
}
```

Rizin warns about malformed ELF program-header entries and skips oversized
string scans for this EBOOT, but it disassembles the selected addresses.
Verify the input hashes before reusing these addresses. The first 32 bytes at
`0x00108960` are
`54A704FE80A4001C7CA539D681040018812400083CC040007D08281480A40030`;
this is an address-specific code signature, not a complete build identity.
