"""Static integration guard checking the production PS3MAPI/monitor wiring.

Unlike a mock PS3 server, this test does not pretend to verify network behavior.
It checks the production source, while heartbeat_health_tests.cpp executes the
actual portable polling policy used by the engine.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
transport = (ROOT / "src/core/ps3mapi.cpp").read_text()
engine = (ROOT / "src/app/engine.cpp").read_text()
home = (ROOT / "src/ui/pages/home.cpp").read_text()

command = transport.split("std::string Ps3Mapi::command(", 1)[1].split("std::string Ps3Mapi::value(", 1)[0]
read = transport.split("std::vector<uint8_t> Ps3Mapi::read(", 1)[1].split("void Ps3Mapi::write(", 1)[0]
write = transport.split("void Ps3Mapi::write(", 1)[1]
assert "markHealthy(ctrl_)" not in command, "An initial reply must not reset reconnect backoff"
assert 'if (startsWith(l, "200 "))' not in command
assert 'if (startsWith(done, "226") && got == n) markHealthy(ctrl_);' in read
assert 'if (startsWith(done, "226")) markHealthy(ctrl_);' in write
assert 'if (!heartbeatHealth.shouldSample(Clock::now())) continue;' in engine
assert 'heartbeatHealth.failed(t)' in engine
assert 'doDisconnect("connection lost")' not in engine
assert '!s.error.empty() ? s.error :' in home
print("PASS: connection transaction and watchdog integration wiring")
