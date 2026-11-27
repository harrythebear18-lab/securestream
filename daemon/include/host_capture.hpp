#pragma once

#include "transport/clock.hpp"
#include "transport/ring_buffer.hpp"
#include "transport/vsock_channel.hpp"
#include "transport/transport_packet.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <functional>

namespace securestream {

// HostCapture — captures webcam and microphone on the host and sends
// frames/chunks through the transport layer to the VM compositor.
//
// This is the host-side capture that the daemon owns. It reuses the
// compositor's WebcamSource and MicrophoneSource for actual capture,
// but instead of rendering, it sends the captured data through the
// ring buffer + vsock notification transport.
//
// The capture runs in background threads. Each captured frame/chunk is:
//   1. Timestamped with the daemon's RDTSC clock
//   2. Written to the shared memory ring buffer (zero-copy)
//   3. Notified to the VM via vsock (40-byte packet)

class HostCapture {
public:
    HostCapture();
    ~HostCapture();

    HostCapture(const HostCapture&) = delete;
    HostCapture& operator=(const HostCapture&) = delete;

    // Initialize with transport handles (ring buffers + vsock channel).
    // The daemon creates these and passes them in.
    bool init(RingBuffer* video_ring, RingBuffer* audio_ring,
              VsockChannel* channel);

    // Start capturing webcam + microphone.
    // video_width/video_height: desired capture resolution
    // audio_sample_rate/audio_channels: desired audio format
    bool start(uint32_t video_width = 1280, uint32_t video_height = 720,
               uint32_t audio_sample_rate = 48000, uint32_t audio_channels = 2);

    // Stop capturing.
    void stop();

    bool is_running() const { return running_.load(); }

    // Statistics
    uint64_t video_frames_captured() const { return video_frames_.load(); }
    uint64_t audio_chunks_captured() const { return audio_chunks_.load(); }
    uint64_t video_frames_dropped() const { return video_dropped_.load(); }

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    // Transport handles (owned by daemon, not by HostCapture)
    RingBuffer* video_ring_ = nullptr;
    RingBuffer* audio_ring_ = nullptr;
    VsockChannel* channel_ = nullptr;

    // Capture threads
    std::thread video_thread_;
    std::thread audio_thread_;

    // Sequence numbers
    uint32_t video_seq_ = 0;
    uint32_t audio_seq_ = 0;

    // Stats
    std::atomic<uint64_t> video_frames_{0};
    std::atomic<uint64_t> audio_chunks_{0};
    std::atomic<uint64_t> video_dropped_{0};

    // Config
    uint32_t video_width_ = 1280;
    uint32_t video_height_ = 720;
    uint32_t audio_sample_rate_ = 48000;
    uint32_t audio_channels_ = 2;

    // Video capture (Media Foundation)
    void* video_source_ = nullptr;  // WebcamSource* (opaque to avoid header dependency)
    void video_capture_loop();

    // Audio capture (WASAPI)
    void* audio_source_ = nullptr;  // MicrophoneSource* (opaque)
    void audio_capture_loop();

    // Send a video frame through the transport
    bool send_video(const void* data, uint32_t size, int64_t capture_time);

    // Send an audio chunk through the transport
    bool send_audio(const void* data, uint32_t size, int64_t capture_time);
};

} // namespace securestream
