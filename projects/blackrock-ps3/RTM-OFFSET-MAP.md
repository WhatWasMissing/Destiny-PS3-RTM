# RTM offset test copy

This branch is a BLES01857 01.24 testing copy of the edited Blackrock source.
It adds the RTM's main static cheat sites as guarded Blackrock patch units. The
RTM's source off value is each unit's required original value; activation is
refused if any word differs, and the existing patch journal restores those
original words on disable or disconnect.

## Main cheat offset mapping

| RTM cheat | RTM site(s) | Test-copy change |
|---|---|---|
| Instant Respawn | `0x0071C348` | Uses the RTM's single `0x93BF0030` → `0x60000000` site. Blackrock's separate multi-site Auto Respawn remains unchanged. |
| No Recoil | `0x005056DC`, `0x00505D24`, `0x0030DC44` | Added RTM test toggle; `0x3F800000` → `0x00000000`. |
| Unlimited Abilities | `0x002C1DB4`, `0x00CCB9E8`, `0x002C36FC` | Added RTM test toggle with the RTM's three instruction words. |
| Unlimited Ammo | `0x003108EC`, `0x00310554`, `0x00CF95AC`–`0x00CF95BC` | Added RTM test toggle; the final 20-byte sequence is represented as five checked words. |
| No Collision | `0x014FC690` | Added RTM test toggle; `0x3F800000` → `0x00000000`. |
| One-Hit Kill | `0x0043E626` | Added the main RTM `0x447A` variant. Blackrock writes the enclosing aligned word at `0x0043E624` (`0x3C603F80` ↔ `0x3C60447A`) so the neighboring halfword is preserved. |
| Rapid Fire | `0x00307590` | Added RTM test toggle. |
| No Target | `0x0018681C`, `0x001891E4` | Added RTM test toggle. |
| God Mode | `0x00444CA0` | Added RTM test toggle. |
| Stop Bullets | `0x0051E7E0` | The Blackrock Stop Bullets unit now uses this RTM site and its `1.0` ↔ `0.0` values instead of Blackrock's different instruction site at `0x0051E6E4`. |
| No Gravity | `0x0143AD08` | Added RTM test toggle; `1.0` ↔ `-1.0`. |
| No Pushbacks | `0x01389760`–`0x01389768` | Added RTM test toggle for the three RTM vector words. |
| No Floors | `0x01689470` | Added RTM test toggle; `1.0` ↔ `-1.0`. |
| Static Actors | `0x002EDAC4` | Added RTM test toggle; `1.0` ↔ `0.0`. |
| Unlimited Sparrow | `0x0050A700` | Same site and values as Blackrock's Sparrow Energy unit; now sourced from the RTM offset table. |
| Variable Buff | `0x0005E614` | Added RTM test toggle using the source preset `0x3F800000` ↔ `0x403F0004`; the gameplay effect is not established. |

The main RTM One-Hit preset is `0x447A`; the Overwalls variant is `0x547A`.
The branch uses `0x447A`, consistent with the RTM's main preset and its
`memory-features.js` classifier.

## Not translated

- Warpriest (`0x00444CA4`, `0x01891AE0`) writes a secondary value only while
  enabled and relies on a live snapshot for recovery. The fixed original-value
  patch unit cannot safely restore that unknown secondary word, so this branch
  leaves Blackrock's original Warpriest control untouched.
- Super Speed writes a single byte at `0x001A96C0`. The RTM address audit notes
  that the byte is read in float paths and does not establish a useful speed
  value, so no test toggle was added.
- RTM FOV (`0x0070906C`) and Blackrock's hooked FOV controller are different
  implementations; changing that controller requires a separate port.
- Blackrock player, weapon, movement, and object-layout offsets describe
  runtime structures rather than the RTM's fixed cheat sites. They remain
  Blackrock-specific in this copy and are not mechanically substituted.

## Build profile and limits

The global build gate now also checks the five BLES01857 01.24 signatures used
by the RTM, in addition to Blackrock's existing fingerprints. The RTM test
toggles also compare every target against its RTM off value before writing.
These are static checks, not live-console validation. No Windows build or PS3
test has been performed for this branch.
