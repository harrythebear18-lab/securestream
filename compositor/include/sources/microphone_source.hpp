#pragma once

#include "source.hpp"
#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace securestream {

// MicrophoneSource captures audio from the host microphone via WASAPI.
// Provenance is HostMicrophone — this is the creator's real mic.
//
// If no microphone is available, falls back to silence so the pipeline
// still works for testing.
class MicrophoneSource : public Source {
public:
    MicrophoneSource(std::string name, uint32_t sample_rate = 48000,
                     uint32_t channels = 2);
    ~MicrophoneSource() override;

    MicrophoneSource(const MicrophoneSource&) = delete;
    MicrophoneSource& operator=(const MicrophoneSource&) = delete;

    Frame get_video_frame() override { return {}; }
    AudioChunk get_audio_chunk() override;
    bool has_video() const override { return false; }
    bool has_audio() const override { return true; }

    bool is_real_capture() const { return real_capture_; }

private:
    uint32_t sample_rate_;
    uint32_t channels_;

    bool real_capture_ = false;
    std::atomic<bool> capturing_{false};
    std::thread capture_thread_;
    std::mutex audio_mutex_;
    std::vector<AudioChunk> audio_queue_;

    void capture_loop();
};

} // namespace securestream
