#pragma once

#include <cstdint>
#include <chrono>

namespace securestream {

// Clock — RDTSC-based monotonic timeline for the transport layer.
//
// The daemon owns the authoritative clock. The VM correlates its local
// clock to the daemon's at vsock connection time (see §5.6 of DESIGN.md).
//
// RDTSC gives sub-nanosecond resolution on x86. We calibrate it against
// steady_clock at startup to convert ticks to microseconds.
//
// This clock never goes backwards. It is the "inline atomic clock" —
// a software-defined monotonic timeline that every component agrees on.
class Clock {
public:
    // Initialize and calibrate RDTSC frequency.
    // Must be called once before any other method.
    static void init();

    // Current time in microseconds on the RDTSC timeline.
    // This is the authoritative timestamp for all transport packets.
    static int64_t now_us();

    // Current time in nanoseconds (for sub-microsecond precision).
    static int64_t now_ns();

    // RDTSC frequency in ticks per second.
    static int64_t rdtsc_frequency() { return rdtsc_freq_; }

    // Convert RDTSC ticks to microseconds.
    static int64_t ticks_to_us(int64_t ticks) {
        // Split into high and low 32-bit parts to avoid 128-bit math
        // ticks * 1e6 / freq = (hi*2^32 + lo) * 1e6 / freq
        uint64_t t = static_cast<uint64_t>(ticks);
        uint32_t lo = static_cast<uint32_t>(t);
        uint32_t hi = static_cast<uint32_t>(t >> 32);
        // lo * 1e6 fits in 64 bits (max ~4.29e15)
        uint64_t lo_us = (static_cast<uint64_t>(lo) * 1000000ULL) / static_cast<uint64_t>(rdtsc_freq_);
        // hi * 1e6 * 2^32 / freq — compute as (hi * 1e6) * 2^32 / freq
        // hi * 1e6 fits in 64 bits for reasonable hi values
        uint64_t hi_us = (static_cast<uint64_t>(hi) * 1000000ULL);
        // Multiply hi_us by 2^32 then divide by freq
        // Use _umul128 for 64x64->128 multiply, then divide
        // Simpler: hi_us * (2^32 / freq) + correction
        // For now, use double for the high part (acceptable precision)
        double hi_part = static_cast<double>(hi_us) * 4294967296.0 / static_cast<double>(rdtsc_freq_);
        return static_cast<int64_t>(hi_part) + static_cast<int64_t>(lo_us);
    }

    // Convert microseconds to RDTSC ticks.
    static int64_t us_to_ticks(int64_t us) {
        return (us * rdtsc_freq_) / 1000000LL;
    }

private:
    static int64_t rdtsc_freq_;
    static bool initialized_;

    static int64_t read_rdtsc();
    static void calibrate();
};

} // namespace securestream
