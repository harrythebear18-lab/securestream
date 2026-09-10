#pragma once

#include "source.hpp"
#include <atomic>
#include <mutex>
#include <thread>

namespace securestream {

// WebcamSource captures video from the host webcam via Media Foundation.
// Provenance is HostWebcam — this is the creator's real camera.
//
// If no webcam is available, falls back to an animated test pattern so the
// pipeline still works for testing.
class WebcamSource : public Source {
public:
    WebcamSource(std::string name, uint32_t width = 1280, uint32_t height = 720);
    ~WebcamSource() override;

    WebcamSource(const WebcamSource&) = delete;
    WebcamSource& operator=(const WebcamSource&) = delete;

    Frame get_video_frame() override;
    AudioChunk get_audio_chunk() override { return {}; }
    bool has_video() const override { return true; }
    bool has_audio() const override { return false; }

    // Whether we're capturing from a real device or using the test pattern.
    bool is_real_capture() const { return real_capture_; }

private:
    uint32_t width_;
    uint32_t height_;
    uint64_t frame_counter_ = 0;

    // Real capture state
    bool real_capture_ = false;
    std::atomic<bool> capturing_{false};
    std::thread capture_thread_;
    std::mutex frame_mutex_;
    Frame latest_frame_;

    // Media Foundation capture loop (runs in capture_thread_)
    void capture_loop();

    // Test pattern fallback
    Frame generate_test_pattern();
};

} // namespace securestream
