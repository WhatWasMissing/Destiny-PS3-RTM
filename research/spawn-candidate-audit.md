# Spawn tag candidate audit

The local Blackrock release contains a plausible enemy candidate for the
read-only BLES handle resolver probe:

| Evidence | Finding |
| --- | --- |
| `data/spawn/spawnable.tsv` | Tag `815BA133`, catalog type `9`, six squad references, candidate labels `vex_minotaur`, `Push tevis02`, and `minotaur`. |
| `data/names/entities.tsv` | The same tag is labeled `Vex minotaur (archetype)` and associated with encounter squads. |
| `data/names/tagmap.tsv` | Xbox tag `815BA133` maps to PS3 tag `815BA133`; `size_match=1`. |
| BLES handle arithmetic | The tag satisfies the packed-handle shape and decodes to candidate bucket `0x0ADD`, slot `0x0133`. |

The catalog type `9` is not proven to be the spawn command's `expectedType`
value, so the two values must not be equated. The tag-map row is Blackrock's
cross-platform metadata; it does not prove that the live BLES01857 01.24 handle
table contains the intended resource, nor that constructing it yields a living
combatant. The default value in the RTM's resolver panel is now this candidate
so the user can inspect its live descriptor without invoking object creation.

Source files are in the user-provided `D:\BlackrockPS3-preview-win64-2026-10-26\BlackrockPS3-release-final\data` folder. Catalog data is not copied into the RTM.
