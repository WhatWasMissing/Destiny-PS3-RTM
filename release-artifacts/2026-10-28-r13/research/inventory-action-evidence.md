# Blackrock inventory action boundary

## Blackrock host-side actions

The local Blackrock preview's inventory mutator audit identifies these action
IDs in `FUN_140350270`:

| ID | Label |
|---:|---|
| 0 | Transfer to vault |
| 1 | Transfer to character |
| 2 | Equip |
| 3 | Unequip |
| 4 | Dismantle |
| 5 | Use |

`FUN_1400e65a0` registers the vault UI callbacks. `FUN_1403502e0` validates
selected-item location and transfer/equip constraints, and its transfer route
builds item-specific request state through `FUN_140353b60`. The host callback
in `FUN_1400e5190` is identified as
`brps3::invui::...::runRequest(Ctx&, inventory::ActionRequest const&)`; the
function at `0x1400E5190` writes a `[inventory]` status/log line, calls request
interface virtual methods, and uses `Inventory confirm` before a fresh
inventory read. Its false-predicate branch schedules a closure through
`FUN_1400191E0`, which copies closures into a local ring-buffer task queue and
signals the queue. This establishes local deferred work, not the final action
transport.

`FUN_140353B60` constructs a native route/restriction record of about `0x34`
bytes. Its two direct callers use the fields for local route/UI validation;
the record is not serialized by this helper. A separate generic PS3MAPI
`MEMORY SET %u %X` command string at VA `0x140586D80` is referenced by the
writer `FUN_140449280` (`0x140449280`–`0x14044990D`), which calls transport
helper `FUN_140448D80` at `0x140449500`. The writer's two direct callers are
`FUN_1402C9740` and `FUN_1402C9B80`. Static tracing has not linked
`runRequest`, its queued callback, or the `0x34` route record to those write
wrappers. The inventory action's ultimate transport and action bytes therefore
remain unresolved.

## Separate documented Bungie API route

Bungie's D1 API documentation separately lists signed-in `POST /TransferItem/`
and `POST /EquipItem/` operations with the `MoveEquipDestinyItems` scope. The
documented transfer body contains `itemReferenceHash`, `itemId`, `characterId`,
`membershipType`, `stackSize`, and `transferToVault`. This is an alternate
official API route, not evidence about Blackrock's callback path or a PS3
memory handler. The D1 Companion stopped exposing most character data in 2017;
Bungie's legacy-support page says the third-party API continued, subject to
availability. Practical account access and action support still need a
user-authorized OAuth/API-key check. The RTM does not use extracted Bungie
cookies or store credentials.

Sources: [Bungie D1 API endpoint reference](https://www.bungie.net/d1/platform/Destiny/help/),
[D1 TransferItem request details](https://www.bungie.net/d1/platform/Destiny/help/HelpDetail/POST?uri=TransferItem%2F),
[D1 EquipItem reference](https://www.bungie.net/d1/platform/Destiny/help/HelpDetail/POST?uri=EquipItem%2F),
[Bungie Legacy Support for Destiny 1 FAQ](https://help.bungie.net/hc/en-us/articles/360049199871-Legacy-Support-for-Destiny-1-FAQ).

The audit report lives outside this repository at
`E:\DestinyResearch\BlackrockAudit\inventory-mutators.txt`. These findings
describe the local Blackrock preview only. No Blackrock code or data is copied
into the RTM.

## BLES 01.24 boundary

The matching BLES01857 01.24 EBOOT hash is
`E7F100B38BBAF6C23F71B390FDB103D31B427187C38DCFBD47E09C94C4A44206`. Its data
contains the strings `cui::c_investment_equip_inventory_item_component` at
`0x0133A474` and `bootflow:confirm_account_transfer` at `0x012F7C30`. A string
does not establish an entry point, argument layout, thread context, transaction
boundary, or persistence behavior. The current Ghidra project has too little
stored analysis for its missing xrefs to be meaningful.

The RTM's verified BLES 01.24 item-pool reader extracts indexes from existing
item objects, and its offline viewer decodes explicitly imported QZ payloads.
Neither provides a safe live mutation route. No BLES 01.24 inventory action
routine, request format, or active-character/vault ownership mapping has been
verified. This does not prove such a routine is absent; it marks the current
research boundary. Inventory writes remain unimplemented until the target-side
operation and readback behavior are established.

## Bounded BLES 01.24 follow-up

The hash-matched EBOOT was checked for references to the equip-component string
at `0x0133A474` and account-transfer string at `0x012F7C30`. Rizin emitted no
xrefs, but it warned about malformed program headers and skipped string search
in oversized regions; these empty results are inconclusive. The investment
BAP-handler string
`c_investment_bap_message_push_handler::handle_message` starts at VA
`0x01319B94` (file offset `0x01309B94`), without a proven code reference.

A bounded PowerPC scan found three switch-like groups that compare numeric
values 402–404:

| BLES 01.24 VA range | 404 branch target | 403 / 402 branch target |
|---|---:|---:|
| `0x00D5F204`–`0x00D5F218` | `0x00D60514` | `0x00D60594` |
| `0x00D65724`–`0x00D65738` | `0x00D66A34` | `0x00D66AB4` |
| `0x00D6BEF4`–`0x00D6BF08` | `0x00D6D204` | `0x00D6D284` |

The inspected target blocks read byte/word fields from a buffer or stack, write
indexed fields, and return to shared loops. The surrounding switches also
compare neighboring IDs, so this looks more like generic numeric-tag decoding
or serialization than an established inventory action dispatcher. The IDs
coincide with Blackrock's host-side labels 402–404, but the current evidence
does not connect the two meanings. No equip, transfer, dismantle, use, or item
create/update call path, request arguments, or persistence behavior is proven.

A follow-up checked the BAP handler, equip component, item-lock handler, and
item-widget strings for exact 32/64-bit pointers in either byte order and PPC
`addis` plus low-immediate address pairs. It found no reliable code reference.
An isolated low-immediate match uses an unrelated high half. A class-adjacent
table after the item-lock string contains values such as `0x7C5D8400` and
`0x7C5D8800`; removing their final byte produces code-looking addresses, but
the encoding and relationship to the class are unknown. Repeated BAP metadata
value `0x0001DFF0` also recurs in unrelated data and lands inside an epilogue,
so it is not treated as a handler pointer. The stripped ELF has no symbol or
relocation sections, and Rizin reported invalid program-header metadata. These
bounded scans cannot establish a caller chain. Further progress requires a
reliable function-reference view or a runtime trace; until then, 402–404 are
not treated as inventory mutation operations.
