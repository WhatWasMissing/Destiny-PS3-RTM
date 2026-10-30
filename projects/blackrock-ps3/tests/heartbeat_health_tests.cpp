#include "app/heartbeat_health.h"

#include <cassert>
#include <chrono>
#include <iostream>

int main() {
    using brps3::HeartbeatHealth;
    using Clock = HeartbeatHealth::Clock;
    using namespace std::chrono_literals;

    HeartbeatHealth health;
    const auto t = Clock::time_point{} + 123s;
    assert(health.shouldSample(t));
    assert(!health.shouldSample(t + 250ms));
    assert(!health.shouldSample(t + 4999ms));
    assert(health.shouldSample(t + 5000ms));
    assert(!health.shouldSample(t + 5100ms));

    // A fresh heartbeat is considered live until two sample windows pass.
    assert(health.ticking(110.0, 100.0));
    assert(!health.ticking(111.0, 100.0));

    // Transient PASV/memory read failures must not be treated as full disconnects.
    assert(!health.failed(10.0));
    assert(!health.failed(15.0));
    assert(!health.failed(20.0));
    assert(health.failureCount() == 3);
    assert(!health.failed(24.9));
    assert(health.failed(25.0));
    assert(!health.failed(30.0));
    assert(!health.failed(54.9));
    assert(health.failed(55.0));
    assert(health.succeeded());
    assert(health.failureCount() == 0);
    assert(!health.succeeded());

    // A new session starts without inheriting an old failure or polling deadline.
    health.reset();
    assert(health.shouldSample(t + 5100ms));
    assert(!health.failed(100.0));
    assert(!health.failed(105.0));
    assert(!health.failed(110.0));
    assert(health.failed(115.0));
    std::cout << "PASS: PS3MAPI heartbeat polling and degradation policy\n";
}
