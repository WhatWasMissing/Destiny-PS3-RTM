#pragma once

#include <chrono>

namespace brps3 {

// A read of the executor heartbeat opens a PS3MAPI passive data socket.
// Keep the dashboard responsive without continually creating TCP connections.
class HeartbeatHealth {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto SamplePeriod = std::chrono::seconds(5);
    static constexpr double TickWindowSeconds = 11.0;
    static constexpr double ErrorGraceSeconds = 15.0;
    static constexpr double WarningRepeatSeconds = 30.0;

    void reset() {
        nextSample_ = {};
        failures_ = 0;
        lastReport_ = -1e9;
    }

    bool shouldSample(Clock::time_point now) {
        if (now < nextSample_) return false;
        nextSample_ = now + SamplePeriod;
        return true;
    }

    bool ticking(double now, double lastChange) const {
        return now - lastChange < TickWindowSeconds;
    }

    // True only after a sustained failure and at most once per warning window.
    bool failed(double now) {
        if (++failures_ == 1) failingSince_ = now;
        if (now - failingSince_ < ErrorGraceSeconds || now - lastReport_ < WarningRepeatSeconds)
            return false;
        lastReport_ = now;
        return true;
    }

    int failureCount() const { return failures_; }

    // Returns true if a previously reported failure recovered.
    bool succeeded() {
        bool hadReport = lastReport_ >= 0;
        failures_ = 0;
        lastReport_ = -1e9;
        return hadReport;
    }

private:
    Clock::time_point nextSample_{};
    int failures_ = 0;
    double failingSince_ = 0;
    double lastReport_ = -1e9;
};

} // namespace brps3
