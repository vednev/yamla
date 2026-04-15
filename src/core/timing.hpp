#pragma once
#include <chrono>
#include <cstddef>

// ------------------------------------------------------------
//  TimingStats — wall-clock durations for key operations
// ------------------------------------------------------------
struct TimingStats {
    double parse_ms     = 0.0;
    double filter_ms    = 0.0;
    double frame_ms     = 0.0;
    size_t memory_bytes = 0;
};

// ------------------------------------------------------------
//  ScopedTimer — RAII wall-clock timer; writes to a double&
//
//  Usage:
//    TimingStats stats;
//    {
//        ScopedTimer t(stats.parse_ms);
//        do_expensive_parse();
//    }  // stats.parse_ms filled on destruction
// ------------------------------------------------------------
struct ScopedTimer {
    using Clock = std::chrono::steady_clock;
    Clock::time_point start_;
    double& out_ms_;

    explicit ScopedTimer(double& out_ms)
        : start_(Clock::now()), out_ms_(out_ms) {}

    ~ScopedTimer() {
        out_ms_ = std::chrono::duration<double, std::milli>(
                      Clock::now() - start_).count();
    }

    ScopedTimer(const ScopedTimer&)            = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;
};
