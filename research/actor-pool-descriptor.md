# BLES01857 01.24 actor-pool descriptor trace

This note cross-checks Blackrock's actor-pool metadata against the local,
hash-verified Destiny ELF. It establishes the descriptor's initialization and
two handle-based consumers. It does **not** establish that enumerating all
records yields the currently loaded enemies, or that the fields below are
spawnable actor identities.

## Inputs

| Input | Identity |
|---|---|
| Destiny ELF | `E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF`, 26,181,096 bytes, SHA-256 `e7f100b38bbaf6c23f71b390fdb103d31b427187c38dcfbd47e09c94c4a44206` (BLES01857 01.24) |
| Blackrock metadata | `D:\BlackrockPS3-preview-win64-2026-10-26\BlackrockPS3-release-final\data\world\pools.csv`, SHA-256 `a8f4f3ad69d2b630e8e35418ad3d7ce8e5bfaa2c29611dfbc9081e59c4a5238` |
| Blackrock globals | `D:\BlackrockPS3-preview-win64-2026-10-26\BlackrockPS3-release-final\data\world\globals.csv`, SHA-256 `e13353d6b931373a1f5b68ed9b52be415a4ae3b3a3075974e475d7db6e75ca20` |

The Blackrock `actors` row records `ElemSize=10464` (`0x28E0`), capacity `40`,
alignment `16`, PS3 descriptor `0x01A1E160`, and initializer callsite
`0x0015B264`. Its evidence field notes differing `r6` arguments
`0x28E0`/`0x28D0`; the row is marked `likely`.

The adjacent `AI Actor Manager` row in `globals.csv` identifies its PS3 base
as `0x01A1E150`, initialized at `0x0015B1B4`. The EBOOT code forms that base
at `0x0015B17C`, initializes the manager, then forms `0x01A1E160` at offset
`+0x10` from the manager base. This places the
actors descriptor inside the registered AI Actor Manager rather than merely
near it.

## Initializer: descriptor and dimensions

The PS3 initializer starts at `0x0015B22C`. Its instructions form
`0x01A1E160` in `r28`, set `r9=40`, set `r6=0x28D0`, copy the descriptor to
`r3`, then call the common initializer at `0x000BE148` from `0x0015B264`.
Thus the PS3 record stride is `0x28D0`; the Blackrock CSV's `ElemSize` field is
`0x28E0`.

The adjacent call at `0x0015B2AC` initializes the separate actor-input
descriptor at `0x01A1E1E0` with capacity `40` and stride `0x3E0`; this agrees
with Blackrock's `s_actor_input_data array` row. That adjacent match is a useful
cross-check that the call arguments are descriptor dimensions.

The shared helper saves the descriptor, capacity, and stride, computes their
product (`40 × 0x28D0 = 0x66080` bytes) for its allocator path, and calls a
nested initializer. That nested initializer stores the allocator-returned
backing pointer at `0x000BDEDC` (descriptor `+8`), the stride at `0x000BDEE4`
(descriptor `+0xC`), and the capacity at `0x000BDEE8` (descriptor `+0x10`).
The backing address is dynamic; it is not a fixed address to copy from
Blackrock's CSV.

## Handle consumers and limits

At `0x0015CD04`, code masks a handle to 13 bits, multiplies the result by the
descriptor stride, adds the backing pointer, and reads the record's leading
halfword. Bit 0 and bit 5 of that halfword affect a branch/state value. This
proves indexed record access and those bit tests; it does not name the bits or
prove that either bit means “occupied” or “enemy.”

At `0x00193B14`, code reads a handle from its input object, masks it to 13 bits,
resolves the same descriptor record, reads `record+0x226C`, and passes the
result to `0x00190B30`. That function treats the value as a second packed
handle and resolves it through a distinct manager at `0x01896D50`, returning a
pointer only after another validation path. Static code does not label
`+0x226C`, prove it is a raw object pointer, or show that all 40 records are
currently loaded enemies.

A related same-build RPCS3 research note (`squad-member-counts-0124.md`,
JOIN99/JOIN101) reports actor datums resolving through this pool to active
actors, with `actor+0x30` matching a unit datum. The EBOOT accessor at
`0x00191960` is exactly `lwz r4,0x30(r4); stw r4,0(r3); blr`, consistent with
that interpretation. This is corroborating RPCS3 evidence, not a physical
console sample, and it does not establish the bitmap bit's meaning.

The `0x00255FA8` path is a cleanup routine: after resolving the actor record,
it checks the bitmap bit derived from `+0x30`, clears record `+0x40/+0x44`,
and resets the actor's `+0x174` handle. A set bit selects additional
cross-manager cleanup. This makes the bit a cleanup gate in that path; it does
not prove that it means the pool slot is occupied or that the actor is a live
enemy. The other consumer paths treat `+0x226C` as a packed handle resolved
through manager root `0x01896D50`, so it must not be exposed as an address.

A distinct allocator bitmap is reached through descriptor `+0`, allocator
header `+8`, and its bitmap descriptor. BLES allocation sets its slot bit and
release clears it. The full datum also incorporates identity and namespace
fields; a slot number alone cannot establish identity across reuse. The
address trace, Blackrock iterator and full-datum reconstruction are documented
in [actor-pool-allocation-evidence.md](actor-pool-allocation-evidence.md).
The descriptor-only UI probe does not read this bitmap or enumerate records.
The separate bounded CLI/MCP candidate reader follows the allocator bitmap,
reconstructs datums, and reports the raw `+0x226C` packed handle plus its
decoded bucket/slot arithmetic after signature and value rechecks. It does not
resolve that handle or classify its target.

Consequently, a reader must at minimum reject indices `>=40` and preserve the
distinction between the descriptor record, the `+0x226C` packed handle, and any
resolved object. No spawn action or actor name can be derived from this
descriptor alone. No live-console read or memory write was performed for this
trace.

## Reproduction

With Rizin installed, inspect the argument setup, helper, and consumers:

```powershell
$elf = 'E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF'
$rz = 'E:\DestinyResearch\Tools\Rizin\bin\rizin.exe'
& $rz -q -e scr.color=false -a ppc -b 64 -E big -s 0x0015B230 -c 'pd 34' $elf
& $rz -q -e scr.color=false -a ppc -b 64 -E big -s 0x000BE148 -c 'pd 430' $elf
& $rz -q -e scr.color=false -a ppc -b 64 -E big -s 0x000BDD5C -c 'pd 150' $elf
& $rz -q -e scr.color=false -a ppc -b 64 -E big -s 0x0015CD04 -c 'pd 24' $elf
& $rz -q -e scr.color=false -a ppc -b 64 -E big -s 0x00193B14 -c 'pd 24' $elf
```

The helper emits malformed-ELF/program-header and string-scan warnings on this
file, but still disassembles the selected virtual addresses. Verify the input
hash before relying on the addresses; the project helper can do that with:

```powershell
python tools/rtm_elf_audit.py $elf --json
```
