# Spawn tag candidate audit

The local Blackrock release contains a plausible enemy candidate for the
read-only BLES handle resolver probe:

| Evidence | Finding |
| --- | --- |
| `data/spawn/spawnable.tsv` | Tag `815BA133`, catalog type `9`, six squad references, candidate labels `vex_minotaur`, `Push tevis02`, and `minotaur`. |
| `data/names/entities.tsv` | The same tag is labeled `Vex minotaur (archetype)` and associated with encounter squads. |
| `data/names/tagmap.tsv` | Xbox tag `815BA133` maps to PS3 tag `815BA133`; `size_match=1`. |
| BLES handle arithmetic | The tag satisfies the packed-handle shape and decodes to candidate bucket `0x0ADD`, slot `0x0133`. |

The catalog type `9` is strongly consistent with the BLES object-kind byte:
the constructor copies definition `+0x4E` into created sObject `+0x08`, and the
Blackrock operation-1 guard compares returned-object `+0x08` to packet `+0x24`.
The BLES type-name sequence and Blackrock catalogs both support `9` as a broad
biped-like class; type-9 rows also include player Guardians. However, no direct
BLES dataflow or live-console read proves that Blackrock's catalog `type` is
the exact value expected by that guard. The RTM preserves it as metadata and
offers an explicit unverified copy action, while leaving the guard unchanged
by default. The tag-map row is Blackrock's cross-platform metadata; it does not
prove that the live BLES01857 01.24 handle table contains the intended resource,
nor that constructing it yields a living combatant. The default value in the
RTM's resolver panel is now this candidate so the user can inspect its live
descriptor without invoking object creation. See
[spawn type evidence](spawn-type-evidence.md).

Source files are in the user-provided `D:\BlackrockPS3-preview-win64-2026-10-26\BlackrockPS3-release-final\data` folder. Catalog data is not copied into the RTM.
