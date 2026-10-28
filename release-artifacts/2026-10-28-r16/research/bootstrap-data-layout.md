# Blackrock bootstrap and shared data layout

Static reverse-engineering notes from the Blackrock PE and Ghidra outputs in
`C:\Users\novel\AppData\Local\Temp`. These artifacts describe the source
build; the reconstructed code is offline only and has not been installed.

## Bootstrap installation sequence

`FUN_1402d16d0` is the bootstrap installer. It takes the manager lock, checks
the manager counter, and calls `FUN_1402c8db0`. It then checks and registers
three zero-filled remote ranges through `FUN_1402c26e0`:

| Range | Purpose from callsite | Size |
| --- | --- | ---: |
| `0x183E000` | bootstrap executor | `0x2000` |
| `0x1C74000` | dispatcher header and queues | `0xC000` |
| `0x183D200` | recovery-owned code/data range | `0xE00` |

`FUN_1402c26e0` reads the requested range and refuses it if any byte is
nonzero, then records the range for cleanup. It does not allocate remote pages
or write to the range. The bootstrap code itself is built by
`FUN_1402d0030(&out, 0x183E000, 0x1C74000, 0x3510F0)` and written to
`0x183E000`. The dispatcher header is written at `0x1C74000` as
`BRX1 00 00 00 01` followed by four zero bytes. Both writes pass the
readback-verification flag to `FUN_1402c9740`.

The call sequence is visible in `BlackrockBootstrapReadonly.txt` at
`FUN_1402d16d0` (calls at `0x1402d174b`, `0x1402d175e`, `0x1402d1772`,
`0x1402d17b4`, `0x1402d17d4`, and `0x1402d180b`). The first-fit allocator is
separate: the manager constructor `FUN_1402cb040` seeds the ordinary code
pool at `0x183E400` with `0x1C00` bytes, the shared-data pool at `0x1C75000`
with `0xB000` bytes, and the recovery pool at `0x183D200` with `0xE00`
bytes. `FUN_1402cc280` rounds requests up to 16 bytes and walks the free
intervals in order, taking the first aligned interval that fits and splitting
any remainder. The bootstrap executor is explicitly written at `0x183E000`;
it does not come from the `0x183E400` code pool. For normal generated code,
`FUN_1402cc710` allocates from the code pool and, when given a non-null source
and nonzero length, calls `FUN_1402c9740` with readback verification enabled.

The installer then builds a linked branch at `0x354074` targeting
`0x183E000`. `FUN_1402c3c30` snapshots the old four bytes, journals them via
`FUN_1402c5a30`, writes the branch with readback verification, and registers
the installed hook. The executor restores its saved GPR/LR frame and branches
to `0x3510F0` on exit. The installer marks its manager state active only after
these steps.

`FUN_1402c5360` is compare-safe recovery: it compares current remote data with
the journal's before/after snapshots. If another actor changed the range, it
keeps the journal and reports the conflict instead of blindly overwriting the
new bytes. The inspected bootstrap write path has readback checks, but I found
no explicit instruction-cache maintenance after writing PPU code. The hook
site `0x354074` is journaled but is not one of the three zero-checked ranges;
the inspected installer does not show a build-signature comparison at that
site. These remain integration/runtime review items.

### Bootstrap transcription correction

The earlier prerelease used `806C0004` at bootstrap `+0x60`, which is
`lwz r3,4(r12)`. At that point `r12` is the callback address, so the callback
received its own second instruction (`0x7C0802A6` for the packaged worker)
instead of the task entry's context (`0x01C75000` in the first-slot layout).
Blackrock's x64 emitter trace at `E:\DestinyResearch\BlackrockAudit\executor-trace.txt`
passes register 3, displacement 4 and base register 30 to the load encoder;
the corrected instruction is `807E0004`, `lwz r3,4(r30)`.

The same trace emits `805E0008`, `lwz r2,8(r30)`, for the separate command
queue's 32-bit TOC field. The previous reconstruction used the 64-bit load
`E85E0008`. The corrected 292-byte bootstrap SHA-256 is
`616e8294b969ad36fdb78844a0bafb7dcc9b11a25384c7288201c6aed6d24d1f`.
The previous image SHA-256 was
`19168afea738565520cb7a803354398ff76222d91fad1a6ce7e8700ab4d10dea`.

`python research/verify_bootstrap_dispatch.py` uses the optional Python
`unicorn` package to execute the 32-bit-compatible task-loop and command-header
fragments. It checks an empty table, task entries 0/3/7, context/callback/TOC,
status 1→2, and reproduces the old bad context. It does not execute the PPC64
worker, game functions, or cache behavior. When the journal and original hook
establish a pre-activation failure, recovery recognizes the exact older
bootstrap hash, reconstructs that known image, and clears only matching owned
bytes; unknown hashes and changed chunks remain resident. After activation
may have occurred, restoring the hook does not prove an in-flight callback has
returned. Recovery therefore leaves the task entry, dispatcher header, code and
data intact and requires a Destiny restart before another install, even when
no spawn request was submitted.

## Dispatcher block at `0x1C74000`

The whole `0xC000` region must be zero before installation. Only the 12-byte
header is written at installation time; the queues are populated on demand.

* The task queue begins at `0x1C74010`: eight entries, eight bytes each.
  `FUN_1402cbf20` selects a free task slot, serializes two 32-bit values in
  big-endian order, and writes the selected entry with readback enabled.
* The executor checks sixteen command entries from `0x1C74100`, at `0x80`
  byte strides. A command with state `1` becomes state `2`, calls its function
  pointer with the supplied GPR/FPR arguments, stores results at entry offsets
  `0x70` and `0x78`, issues `sync`, then changes the state to `3`.
* The task and command queues are independent. `FUN_1402d16d0` creates only
  the header and executor; normal manager/API paths enqueue task and command
  records later.

The installer does not write individual zero records for either queue.
Their initial empty state follows from the `0xC000` range having passed the
all-zero check before installation. Task entries are filled later by
`FUN_1402cbf20`; its assembly sets the write helper's readback flag to `1`
and targets `0x1C74010 + slot*8` (`HandlerFollow.txt`, around
`0x1402cc08d`–`0x1402cc0a7`). The fixed `0x80` command slots start at
`0x1C74100`; they are separate from both those per-frame task entries and
the records in the shared-data pool at `0x1C75000`.

The reconstructed executor is in [generate_bootstrap_executor.py](generate_bootstrap_executor.py).
The emitted bytes, event transcript, word listing, and hash are
[bootstrap-executor.bin](bootstrap-executor.bin),
[bootstrap-executor-events.jsonl](bootstrap-executor-events.jsonl),
[bootstrap-executor-words.txt](bootstrap-executor-words.txt), and
[bootstrap-executor-summary.json](bootstrap-executor-summary.json).

## Shared data pool at `0x1C75000`

The manager constructor seeds a separate first-fit pool at `0x1C75000` with
length `0xB000` (end `0x1C80000`). `FUN_1402cc710` allocates from this pool;
`FUN_1402cc280` rounds requests to 16 bytes and selects the first interval
whose aligned start fits. The request length `0x1CD0` is already 16-byte
aligned, so its allocation span remains exactly `0x1CD0`. If this is the first
allocation in that pool, its address is `0x1C75000`; if earlier features have
consumed the pool, the returned base moves. The installer does not hardcode
this data-block base.

`FUN_140435960` makes a zero-filled `0x1CD0` host image, then
`FUN_1402cc710` copies the entire image to the allocated PPU range with
readback enabled. Its layout:

| Offset | Initialized contents |
| --- | --- |
| `+0x000..+0x007` | `BRC1 00 00 00 01` version header |
| `+0x008..+0x00F` | zero |
| `+0x010..+0x02F` | `3F000000 3F800000 3A83126F 3C23D70A 358637BD 4B189680 00000000 7149F2CA` |
| `+0x030..+0x03F` | zero |
| `+0x040..+0x0BF` | 32 big-endian template words from PE address `0x1405719C0`, with overrides at `+0x044..+0x04B` and `+0x074..+0x077` |
| `+0x0C0..+0x14F` | zero |
| `+0x150..+0x1CCF` | eight `0x370`-byte fixed records, all zero initially |

Record starts are `+0x150`, `+0x4C0`, `+0x830`, `+0xBA0`, `+0xF10`,
`+0x1280`, `+0x15F0`, and `+0x1960`. They exactly tile the final
`0x1B80` bytes of the image. At reconnect, the host reads the four-byte state
at each record start. If it is `1` or `2`, it writes state `0` to that record
with readback enabled; eight host-side state bytes at manager offsets
`+0x6C..+0x73` are cleared too.

The template's 32 source words, before the three post-copy overrides, are:

```text
F4A7AA80 0C000000 06098080 00000000 402DCCBC 104F963B E61B663F 75FFDF3E
6A8A3D42 AD0038C4 61B21E41 FEFF7F3F FFFFFFFF 0480FB61 00000000 00000000
FFFFFFFF FFFFFFFF FFFFFFFF FFFFFFFF 00000000 FFFFFFFF 00000000 0000803F
FFFFFFFF C59D1C81 00000310 00000000 FFFFFFFF FFFFFFFF 0C000000 00000000
```

After the initializer's overrides, words at `+0x044`, `+0x048`, and
`+0x074` are respectively `0000000C`, `80800906`, and `FFFFFFFF`.

The Blackrock spawn handler's generated PPC uses record-relative fields for
argument/transform data; the executor's 16 `0x80` command slots are located in
the distinct `0x1C74000` dispatcher region. This source confirms the record
boundaries and status field, but it does not by itself validate every operand
or runtime state transition for a particular Destiny binary.

## Remaining port blockers

* No console write, runtime acceptance, or cache-coherency check has been
  performed. A positive transport-level write/readback is not evidence that
  the PS3 executes newly written PPU code.
* `FUN_1402c5a30` is the journal update called before patch writes. It and the
  recovery path are not yet tied to a complete, end-to-end restore simulation
  for bootstrap plus every shared-data allocation.
* The zero-region guard only establishes that the reserved ranges are empty;
  it does not signature-check the original instruction bytes at hook address
  `0x354074`.
* The task and command queues are dynamically populated by separate manager
  paths. The executor payload is reconstructed; all higher-level object
  request producers and their resource/state semantics still need independent
  app integration review.
