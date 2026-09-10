#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace securestream {

// A single video frame in BGRA8 format.
// This is the only pixel format the compositor deals with internally.
// Sources convert from their native format; filters operate on this format.
struct Frame {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t stride = 0;  // bytes per row (width * 4 for BGRA8)
    std::vector<uint8_t> data;

    bool valid() const { return width > 0 && height > 0 && !data.empty(); }

    void allocate(uint32_t w, uint32_t h) {
        width = w;
        height = h;
        stride = w * 4;
        data.assign(static_cast<size_t>(stride) * h, 0);
    }
};

// A single audio chunk: interleaved float samples, any channel count.
struct AudioChunk {
    uint32_t channels = 0;
    uint32_t sample_rate = 0;
    uint32_t frames = 0;  // number of audio frames (samples per channel)
    std::vector<float> samples;

    bool valid() const { return channels > 0 && sample_rate > 0 && !samples.empty(); }
};

// Timestamp in microseconds relative to compositor start.
using Timestamp = int64_t;

} // namespace securestream
