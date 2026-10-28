# BLES 01.24 actor lookup descriptor evidence

This note records a narrow offline review of the actor lookup path in the
hash-matched BLES01857 01.24 ELF. It does not establish that this table is the
separately discussed 40-record AI actor pool.

## Input and reproducibility

Input ELF: `E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF`

SHA-256: `e7f100b38bbaf6c23f71b390fdb103d31b427187c38dcfbd47e09c94c4a44206`

Ghidra 12.2 imported it as `PowerPC:BE:64:A2ALT`. Auto-analysis exceeded a
600-second timeout in the large executable segment. The address windows below
were then decoded with Ghidra's PPC processor in the saved project, using
`-process EBOOT.ELF -noanalysis` and a small GhidraScript. The project and
script files are local research artifacts, not required to run the byte scan.

From the repository root, the companion script checks the input hash, prints
raw words at the evidence windows, searches the executable PT_LOAD segment for
candidate `lis/addic` materializations of the descriptor and bitmap addresses,
and checks for simple direct `stw/std` writes to descriptor offsets `+8/+0xC`:

```powershell
python research/actor-lookup-descriptor-scan.py "E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF"
```

The scan is deliberately a heuristic. It treats aligned instruction-shaped
bytes in an executable ELF segment as candidates; it does not reconstruct all
control flow, aliases, relocations, or indirect initialization. Its direct
store filter follows only a short linear instruction window and stops at a
branch or a detected write to the materialized base register. Candidate counts
must not be presented as verified cross-reference counts.

## Descriptor and record use

At `0x00255FBc`, the code materializes `0x01A1E160` with `lis r3,0x1A2` and
`subic r3,r3,0x1EA0`. It reads `lwz r5,0xC(r3)` as the stride, then `lwz
r3,0x8(r3)` as the storage pointer. The actor handle at actor `+0x174` is
masked with `0x1FFF`, multiplied by that stride, and added to storage. Thus
`0x01A1E168` and `0x01A1E16C` are directly used as the storage and stride
fields of the descriptor-shaped global at `0x01A1E160`.

The selected record word at `+0x30` is used as a bit number. In the function at
`0x00255FA8`, PPC at `0x00255FF0–0x00256008` computes a word offset and bit mask,
loads from the bitmap rooted at `0x01A21138`, ANDs the selected word with the
mask, and branches to `0x00256034` when the bit is clear. If set, the function
continues through the status check beginning at `0x0025600C`. The executable
proves a bitset gate here; it does not label what wider allocation or lifecycle
state the bitset represents.

After the check/cleanup path, the code stores `0xFFFFFFFF` to selected record
`+0x40`, zero to `+0x44`, and `0xFFFFFFFF` to actor `+0x174`. The helper is
therefore consistent with handle-indexed lookup and cleanup. These stores do
not show that a record is an enemy pointer or a spawn argument.

## Additional descriptor consumers

The instruction window at `0x0015CD04` independently reads descriptor `+0xC`
and `+8`, then indexes the resulting storage using a masked handle. A later
`stw r28,0xC(r3)` at `0x0015CE24` targets a different computed vector entry:
`r3` has already been replaced by the result of `r27 + r30`, so this is not a
write to descriptor `0x01A1E16C`.

The window at `0x00193B14` also reads stride and storage before resolving a
handle. The apparent `stw r5,0xC(r3)` at `0x00193C30` occurs after the loaded
storage pointer and resolved-record address have replaced the descriptor base
in `r3`; it is not an initializer for the descriptor's stride field.

The Ghidra-decoded PPC excerpts are:

```text
0015CD04  lis     r3,0x1a2
0015CD08  rlwinm  r5,r28,0x0,0x13,0x1f
0015CD0C  subic   r3,r3,0x1ea0
0015CD10  ori     r29,r30,0x0
0015CD14  lwz     r6,0xc(r3)       ; descriptor stride
0015CD18  mullw   r5,r5,r6
0015CD1C  lwz     r3,0x8(r3)       ; descriptor storage pointer
0015CD20  addc    r3,r3,r5
0015CD24  rldicl  r5,r3,0x0,0x20
0015CD28  lhz     r5,0x0(r5)

00193B14  lis     r3,0x1a2
00193B18  lwz     r4,0x4(r4)
00193B1C  subic   r3,r3,0x1ea0
00193B20  rlwinm  r4,r4,0x0,0x13,0x1f
00193B24  lwz     r5,0xc(r3)       ; descriptor stride
00193B28  mullw   r4,r4,r5
00193B2C  lwz     r3,0x8(r3)       ; descriptor storage pointer
00193B30  addc    r3,r3,r4
00193B34  li      r4,-0x1
00193B38  rldicl  r3,r3,0x0,0x20
00193B3C  lwz     r3,0x226c(r3)
```

Ghidra's partial database had no function boundaries at `0x0015CD04` or
`0x00193B14` (`getFunctionContaining` returned none), so it could not produce
decompiler C for these blocks. The PPC disassembly is from the imported ELF;
no C pseudocode is inferred for these callers.

The Ghidra reference manager reports direct call sites for `0x00255FA8` at
`0x00256130`, `0x00256F08`, and `0x002570C8`. The two functions above are
additional consumers of the descriptor fields, not additional calls to
`0x00255FA8`.

## Relationship to the 40 × 0x28D0 AI actor pool

A separate register-aware trace of the initializer resolves the relationship:
`0x01A1E160` is the `actors` descriptor inside the AI Actor Manager at
`0x01A1E150`. The initializer at `0x0015B22C` passes capacity 40 and PS3 stride
`0x28D0` to a shared helper; the helper allocates the backing storage and writes
the dynamic pointer, stride, and capacity into descriptor fields. Blackrock's
`0x28E0` pool size is its Xbox layout. Full PPC windows, input hashes, commands,
and field-store addresses are in [actor-pool-descriptor.md](actor-pool-descriptor.md).

The earlier scan reports 302 aligned `lis/addi(c)` byte-pattern candidates for
`0x01A1E160`, zero simple nearby direct `stw/std` candidate stores to its
`+8/+0xC` fields, zero aligned executable `0x28D0` literals, and 482 aligned
`lis/addi(c)` candidates for bitmap `0x01A21138`. These remain heuristic
candidate counts, not verified xrefs: aligned instruction-shaped bytes can
occur inside data, and the short direct-store filter does not follow aliases,
branches, or indirect calls. Its negative direct-store and literal findings do
not contradict the helper-mediated initializer. Static layout evidence does
not establish bitmap-bit semantics, whether every slot is a live enemy, the
meaning of record `+0x226C`, or a spawnable actor identity. A live actor-to-slot
correlation is still needed before exposing actor enumeration.
