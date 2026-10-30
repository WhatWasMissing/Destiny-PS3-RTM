# Blackrock PS3 — minimal source package

Build with Visual Studio 2022 (Desktop C++) and CMake 3.20+:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release -- /m:8

The executable and runtime data are copied to `out/`. This package contains only the files needed to build and run the Windows app: `src/`, `data/`, CMake, and the required Dear ImGui files. The game dump, research, alternate source snapshot, old build directories, and development tools are omitted.

Target for this testing branch: Destiny BLES01857, APP_VER 01.24, build 51539.16.07.26.1808.v230, using webMAN MOD PS3MAPI. The original Blackrock release target was NPEB02021 at the same build string. The developer's offline tests do not establish that ported features work on a live console; use the app's TESTING > Port checks workflow.

## Connection stability patch

See [PATCH_NOTES.md](PATCH_NOTES.md) for the reduced background PS3MAPI polling,
transaction-level reconnect recovery, testing and BLES01857 / Rebug DEX caveats.
This source package must be rebuilt on Windows; the old preview EXE is unmodified.

This branch adds a separate **RTM Test** tab with BLES 01.24 offset presets.
See [RTM-OFFSET-MAP.md](RTM-OFFSET-MAP.md) for the address comparison and
features intentionally left out of the test mapping.
