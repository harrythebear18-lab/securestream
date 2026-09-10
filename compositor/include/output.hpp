#pragma once

#include "types.hpp"
#include <memory>
#include <string>
#include <string_view>

namespace securestream {

// Output is the base class for anything that consumes the compositor's
// encoded stream. There are two kinds:
//   - LiveOutput: sends frames to a streaming platform (SRT, WHIP, RTMPS)
//   - FileOutput: writes frames to a file (local recording)
//
// Both go through the same compositor pipeline, so the same source allowlist
// and security properties apply to recordings as to live streams.
class Output {
public:
    Output(std::string name) : name_(std::move(name)) {}
    virtual ~Output() = default;

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    // Start the output. For live outputs, this connects to the platform.
    // For file outputs, this opens the file.
    // Returns true on success.
    virtual bool start() = 0;

    // Stop the output and release resources.
    virtual void stop() = 0;

    // Whether the output is currently active.
    bool active() const { return active_; }

    // Write a video frame to the output. The output handles encoding.
    virtual void write_video(const Frame& frame, Timestamp pts) = 0;

    // Write an audio chunk to the output.
    virtual void write_audio(const AudioChunk& chunk, Timestamp pts) = 0;

    std::string_view name() const { return name_; }

protected:
    bool active_ = false;

private:
    std::string name_;
};

} // namespace securestream
