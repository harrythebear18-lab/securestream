#include "clock.hpp"

#include <windows.h>
#include <intrin.h>
#include <iostream>
#include <thread>

namespace securestream {

int64_t Clock::rdtsc_freq_ = 0;
bool Clock::initialized_ = false;

int64_t Clock::read_rdtsc() {
    return static_cast<int64_t>(__rdtsc());
}

void Clock::calibrate() {
    // Calibrate RDTSC frequency by measuring ticks over a known time window.
    // We use a 100ms window for a good balance of accuracy and startup time.

    LARGE_INTEGER perf_freq;
    QueryPerformanceFrequency(&perf_freq);
    int64_t qpc_freq = perf_freq.QuadPart;

    // Sync: read QPC and RDTSC as close together as possible
    LARGE_INTEGER qpc_start, qpc_end;
    int64_t tsc_start, tsc_end;

    // Warm up
    QueryPerformanceCounter(&qpc_start);
    tsc_start = read_rdtsc();

    // Sleep for 100ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    QueryPerformanceCounter(&qpc_end);
    tsc_end = read_rdtsc();

    int64_t qpc_elapsed = qpc_end.QuadPart - qpc_start.QuadPart;
    int64_t tsc_elapsed = tsc_end - tsc_start;

    // Convert QPC elapsed to seconds, then derive RDTSC frequency
    double elapsed_seconds = static_cast<double>(qpc_elapsed) / static_cast<double>(qpc_freq);
    rdtsc_freq_ = static_cast<int64_t>(static_cast<double>(tsc_elapsed) / elapsed_seconds);

    // Sanity check: RDTSC frequency should be in the GHz range
    if (rdtsc_freq_ < 1000000000LL) {
        std::cerr << "Clock: warning — RDTSC frequency seems low ("
                  << rdtsc_freq_ << " Hz), results may be inaccurate\n";
    }

    std::cout << "Clock: RDTSC frequency = " << rdtsc_freq_ / 1000000000.0
              << " GHz\n";
}

void Clock::init() {
    if (initialized_) return;
    calibrate();
    initialized_ = true;
}

int64_t Clock::now_us() {
    if (!initialized_) {
        // Auto-init if forgotten, but warn
        calibrate();
        initialized_ = true;
    }
    return ticks_to_us(read_rdtsc());
}

int64_t Clock::now_ns() {
    if (!initialized_) {
        calibrate();
        initialized_ = true;
    }
    int64_t ticks = read_rdtsc();
    return (ticks * 1000000000LL) / rdtsc_freq_;
}

} // namespace securestream
