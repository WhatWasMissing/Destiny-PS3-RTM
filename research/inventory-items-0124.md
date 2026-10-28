# BLES 01.24 inventory item indexes and names

## Verified executable and live inventory path

The verified BLES 01.24 executable is
`E:\Destiny PS3 RTMs\fly hack\gpttest\ebbotelfBAK\EBOOT.ELF`, 26,181,096
bytes, SHA-256
`E7F100B38BBAF6C23F71B390FDB103D31B427187C38DCFBD47E09C94C4A44206`.
This is the ELF whose five compatibility signatures are present at their
expected virtual addresses. The file named `BLES01857_PATCH0024_FRESH.ELF` on
D: is instead the 01.00 base-disc ELF; its misleading filename must not be
used to assign 01.24 semantics.

In 01.24, the self-family kind-3 item-object pool uses type `0x80801732`,
stride `0xA8`, and a capacity of 646. The inventory item index is at object
`+0x24`. The verified `0x0047C1A0` getter signature is
`3C6001AD30635FA0806300044E800020`. The `0x0047E3CC` lookup signature is
`78842EA478A526E47CE420147C8438143CE000047C83201460E793987C8428143CA00004788A00203C80000460A9939C`;
this path looks up an existing item object by family and instance ID. It is not
an item-index-to-name resolver. The RTM reads occupied pool records and exposes
the raw index; it does not expose instance IDs or write memory.

The old `0x0041436C` → `0x0028A304` / `0x00288278` resolver trace and its
cached-cell addresses came from analysis of the 01.00 ELF. Those addresses do
not identify the 01.24 item-list resolver and must not be used as 01.24
runtime pointers. No 01.24 index-to-name resolver or live list pointer has yet
been independently located in this RTM work.

## 01.24 package evidence and item names

D1 Reimagined research describes the investment-globals item list as indexed
definitions and its package parser supports the 01.24 root reference at `+0xE8`
(the older layout uses `+0xD8`). That parser validates the item-array element
class and can emit each index, definition tag, bucket, type, and name.

The local BLUS base-disc copies of packages `012c_0`, `012e_0`, and `012f_0`
match the BLES base-package SHA-256 values in D1 Reimagined's
`data/SHA256SUMS.txt` byte for byte. Their patch ID is 0; parsing only those
base-disc files yields 3,181 item rows. That is a base-disc count, not the final
01.24 list length.

The Sony BLES01857 update list includes 01.24. The final item-related package
files listed by the accompanying 01.24 package inventory were last changed in
01.23. The non-stale patch chains recovered from those BLES updates are:

| Package family | Final patch files | Update versions |
| --- | --- | --- |
| `012c` | `_1`–`_4` | 01.13, 01.20, 01.22, 01.23 |
| `012e` | `_1`–`_2` | 01.13, 01.20 |
| `012f` | `_1`–`_4` | 01.13, 01.20, 01.22, 01.23 |
| `0130` | `_1`–`_4` | 01.13, 01.20, 01.22, 01.23 |
| `0131` | `_1`–`_4` | 01.13, 01.20, 01.22, 01.23 |

The later `_5` files in these chains are marked stale by the inventory and were
excluded. Every extracted update entry matched the BLES inventory's exact path
and byte length, and its Tiger package header matched platform PS3 and the
filename's patch ID. The files and their SHA-256 provenance are kept outside
Git under `E:\DestinyResearch\destiny-0124-inventory\`.

With those updates, the BLES 01.24 root's `+0xE8` reference resolves to the
`0x808033E8` item array in package family `012f`: 6,557 indexes, numbered
`0`–`6556`. The item definitions and localized string references resolve
through the final `012f`, `0130`, and `0131` package chains. The item data was
decoded with the locally available Oodle runtime. For shifted localized text,
the stored UTF-8 was decoded first and the per-entry shift was then applied to
each Unicode code point; this recovers accented names such as Häkke, Façade,
Válka, and Dämmerung.

The resulting local catalog is
`E:\DestinyResearch\destiny-0124-inventory\BLES01857-01.24-items.tsv`.
It contains 6,389 named rows with `itemIndex`, `name`, `bucket`, `type`, and
`tag`; 168 entries with no name in the package data are omitted. Its SHA-256 is
`82C6588678EBFC24DC897FBBEB861A4D0B6F565487C2E3BB16161916F0DA38D4`. It is
not included in the executable or Git; the RTM's user-selected catalog importer
accepts it by the explicit `itemIndex` column. The known 01.24 fixture indexes
710, 204, 198, 200, 206, 202, and 862 resolve to Striker, Lightmail armour,
Mark of the Risen, and Generalist Shell respectively.

The local Blackrock preview's `data/inventory/items.tsv` also has 6,557 rows
with `def` values 0–6556. In a same-position comparison, 6,306 of the 6,389
named BLES rows have the same name after Unicode normalization, case-folding,
and trimming outer quotes/whitespace; 6,388 have the same type. This corroborates
the ordering for this local preview dataset, but differences remain (including
`Classified` labels). None of the 6,389 compared Blackrock `tag` values equals
the BLES definition tag at the same position. The RTM catalog therefore remains
derived directly from BLES 01.24 packages; Blackrock's `hash` and `tag` columns
are not treated as BLES item indexes.

The generic importer can display signed-16-bit item keys (`0`–`32767`) from a
user-selected table. Its live BLES 01.24 name lookup accepts only the verified
list range (`0`–`6556`); out-of-range rows remain visible in the imported
table but cannot label a live inventory object. The table still needs an
`itemIndex` or `blesItemIndex` column and a name column. Do not bundle
game-derived names or infer an item index from a Blackrock hash/tag value.

## Remaining live validation

This package-derived catalog does not identify a live 01.24 item-list pointer
or index-to-name resolver in the executable. The last read-only console request
timed out before the initial compatibility signature, so no item-pool read or
catalog label has yet been checked against the development PS3. The catalog can
be imported for live/offline inventory labels once the console connection is
available; that path still only reads item indexes and does not write memory.
