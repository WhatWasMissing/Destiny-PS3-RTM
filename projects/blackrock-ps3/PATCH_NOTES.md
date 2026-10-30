# BlackrockPS3 — low-impact PS3MAPI connection patch

Prepared from the uploaded 26 October 2026 source snapshot. This is a SOURCE patch;
the original Windows preview binary is unchanged and should not be described as patched.

## What changed

1. The background game-executor heartbeat is now sampled once every **5 seconds**
   instead of once every 250 ms. Each PS3MAPI `MEMORY GET` requires a separate
   temporary passive-mode data TCP connection, so this removes roughly 95% of
   the *idle dashboard polling* data-socket creation (240/minute -> 12/minute).
   Other enabled features and manual memory operations still generate traffic.
2. The executor's "ticking" status tolerates the longer sample interval; FPS
   is computed over that longer window, rather than claiming instant FPS.
3. A sustained *heartbeat memory-read* failure is now shown as a degraded
   executor warning **without automatically destroying the session or attempting
   remote patch restoration**. A heartbeat failure by itself does not establish
   that the TCP control channel has gone down. The existing explicit Disconnect,
   restoration journal and Recover operations remain unchanged.
4. The reconnect backoff is reset only after a complete, successful PS3MAPI
   request (`200` for a control-only query, or complete binary transfer and
   final `226` for a memory read/write). Receiving only the initial `PASV`/
   `150` response no longer falsely resets reconnect backoff. Repeated
   failed control reconnections back off as far as 30 seconds.
5. A socket `select()` error is no longer misclassified as readable data.
6. The connected screen shows the executor error when heartbeat polling is
   degraded, instead of hiding it behind the ONLINE label.

## BLES / Rebug DEX

The European Destiny disc edition is catalogued as **BLES01857**. Blackrock's
original developer target was **NPEB02021**, APP_VER **01.24**, build
**51539.16.07.26.1808.v230**. The current connection code locates the process
by `EBOOT.BIN`, not by its BLES/NPEB title ID, so no title-ID switch is needed.

**Do not blindly reuse profile addresses for a different EBOOT.** The existing
`Session::verifyBuild()` fingerprint validation remains intact. BLES01857 is
not confirmed compatible by this patch. A modified DEX EBOOT or a different game
revision may fail these checks: stop instead of bypassing them. Avoid other
memory-patching tools simultaneously during tests.

## How to build on a Windows PC

Requires Visual Studio 2022 with Desktop development with C++ and CMake 3.20+:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release -- /m:8
ctest --test-dir build -C Release --output-on-failure
```

The executable and `data/` are written to `out/` by the project's existing
CMake build. Do **not** use the old `BlackrockPS3-preview-win64-2026-10-26.zip`
as proof of these changes; it predates this patch.

## Verification and limitations

Portable unit tests of the **real** heartbeat policy can be run without a PS3:

```sh
g++ -std=c++20 -Wall -Wextra -Werror -Isrc tests/heartbeat_health_tests.cpp -o heartbeat_health_tests
./heartbeat_health_tests
python3 tests/test_connection_wiring.py
```

The tests verify the poll schedule, heartbeat liveness window, error grace,
warning throttling, recovery and session reset, plus critical integration
wiring. They **do not** prove live PS3MAPI timing, console notification behavior,
BLES executable compatibility or Windows build success. Only a real console
and a Windows release build can validate those.

When Windows is available, check `%LOCALAPPDATA%\BlackrockPS3\blackrock.log`
for `PS3MAPI: control socket reopened`, `Executor heartbeat unavailable` and errors.
If the PS3 continues to report repeated TCP **control** connections despite
no active features, capture both the app log and, if possible, the destination
port of the connections; individual passive-mode data connections are expected
for memory GET/SET under PS3MAPI.
