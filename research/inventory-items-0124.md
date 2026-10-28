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

## Local package evidence and catalog limits

D1 Reimagined research describes the investment-globals item list as indexed
definitions and its package parser supports the 01.24 root reference at `+0xE8`
(the older layout uses `+0xD8`). That parser validates the item-array element
class and can emit each index, definition tag, bucket, type, and name.

The matching PS3 package files located on this PC are
`ps3_investment_globals_012c_0.pkg` and
`ps3_investment_globals_012e_0.pkg` in the BLES base-disc `PS3_GAME/USRDIR/packages`
directory. Their package headers report patch ID 0 and a 2014 build time. The
parser produced 3,181 rows from these files, but this is **base-disc package
output**, not proof that these are the 01.24 item definitions. No matching
01.24 investment-globals packages were found in the searched Destiny, research,
download, desktop, and document folders. The external D1 notes also report a
3,181-entry list, but the 01.24 package set used for that result is not present
here for independent reproduction.

The RTM's item-catalog importer therefore accepts nonnegative signed-16-bit
indexes (`0`–`32767`), which matches the index field's numeric representation
without asserting an unverified 01.24 list length. A user-selected catalog
still needs to be keyed by `itemIndex` or `blesItemIndex` and contain a name
column. Do not bundle the base-disc mapping or infer a label by joining
Blackrock's `def` ordinal with its `hash`/`tag` columns.

## Next evidence needed

To build a verified label catalog, obtain the BLES01857 01.24 investment-globals
package set (at least the package-012c root and package-012e definitions plus
their required string tables), validate its patch/build identity, then export
an importer-ready TSV with `itemIndex`, `name`, `bucket`, `type`, and `tag`.
The current console request timed out while reading the initial compatibility
signature, before any inventory-specific address was queried. No live 01.24
item-list pointer or label read has been validated on the PS3.
