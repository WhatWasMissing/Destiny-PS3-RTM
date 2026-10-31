# Codex Desktop build handoff

## Branch

- Repository: `WhatWasMissing/Destiny-PS3-RTM`
- Branch: `codex/blackrock-rtm-offsets-test`
- This branch is a testing copy based on `codex/add-blackrock-stability-source`.
- Pull request: https://github.com/WhatWasMissing/Destiny-PS3-RTM/pull/11

In Codex Desktop, open the repository and switch to the branch above. If using a
terminal in a fresh checkout:

```powershell
git fetch origin
git switch --track origin/codex/blackrock-rtm-offsets-test
```

The app source is in `projects/blackrock-ps3/`.

## Build

Use Windows with Visual Studio 2022 and the **Desktop development with C++**
workload, plus CMake 3.20 or newer. From the `projects/blackrock-ps3/`
directory, run:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- /m:8
```

The executable should be written to `projects/blackrock-ps3/out/` as
`blackrock-ps3.exe`, with runtime data copied to `out/data/`. The existing
preview executable was not rebuilt as part of these source changes.

## Changes in this branch

- Adds a separate **RTM Test** tab with guarded BLES01857 01.24 cheat toggles.
- Adds the RTM's five BLES build-signature anchors to the build fingerprint
  checks. The displayed BLES title ID is a target label; the app does not read
  the title ID from the console.
- Uses the RTM site for Instant Respawn (`0x0071C348`) and Stop Bullets
  (`0x0051E7E0`). Sparrow (`0x0050A700`) already matched the RTM.
- Adds RTM address presets for No Recoil, Unlimited Abilities, Unlimited Ammo,
  No Collision, One-Hit Kill (`0x447A` main variant), Rapid Fire, No Target,
  God Mode, No Gravity, No Pushbacks, No Floors, Static Actors, and Variable
  Buff.
- RTM toggles require the expected original words and use Blackrock's patch
  journal to restore them on disable or disconnect.

See [RTM-OFFSET-MAP.md](RTM-OFFSET-MAP.md) for the full address/value map and
items intentionally left unmapped. Warpriest needs a live snapshot for safe
restoration; the Super Speed effect is unresolved; and RTM's FOV implementation
differs from Blackrock's. Player, weapon, movement, and object-structure offsets
remain Blackrock-specific.

## Build status

This branch has not yet been built with Windows/MSVC or tested against a live
PS3. Please report the build result and any compiler errors against this branch.
Keep any build fixes on this test branch for review.
