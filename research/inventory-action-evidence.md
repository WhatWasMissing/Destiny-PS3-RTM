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
available static dump shows confirmation and a refreshed inventory read. It
does not expose the complete PS3 request bytes or a resolved BLES call target.

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
in oversized regions; these empty results are inconclusive. The nearby
investment BAP-handler string `c_investment_bap_message_push_handler::handle_message`
was also found at VA `0x01319B96` (file offset `0x01309B96`), without a proven
code reference.

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

Next research is a narrow reference trace around the BAP handler and its TOC or
dispatch tables. Until a caller and action-specific state changes are tied to
the BLES code, this switch is not treated as an inventory mutation path.
