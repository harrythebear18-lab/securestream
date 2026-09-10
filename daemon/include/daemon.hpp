#pragma once

#include "ring_buffer.hpp"
#include "vsock_channel.hpp"
#include "clock.hpp"
#include "transport_packet.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace securestream {

// Daemon — the host-side process that owns:
//   1. The authoritative clock (RDTSC-based)
//   2. The transport layer (ring buffer + vsock notification)
//   3. Host source capture (webcam, mic, game capture)
//
// The daemon captures sources on the host, writes frames to the shared
// memory ring buffer, and sends vsock notifications to the VM compositor.
//
// The VM compositor trusts the daemon's timestamps and renders according
// to the presentation_time in each packet.
//
// Phase 2: The daemon is a standalone process that can run with or without
// a VM. Without a VM, it sends to a local receiver for testing.

class Daemon {
public:
    Daemon();
    ~Daemon();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    // Initialize the daemon: clock, ring buffer, vsock channel.
    // ring_name: shared memory name for the ring buffer
    // port: vsock/TCP port for notifications
    // force_tcp: true to use TCP loopback (for testing without a VM)
    // frame_size: size of each video frame in bytes (default: 1080p BGRA8)
    // audio_chunk_size: size of each audio chunk in bytes
    bool init(const std::string& ring_name, uint32_t port, bool force_tcp,
              uint32_t frame_size = 1920 * 1080 * 4,
              uint32_t audio_chunk_size = 48000 * 2 * 4 * 2 / 1000);  // 48kHz, 2ch, float32, 2ms = 768 bytes

    // Start the daemon. Listens for VM connection, then starts capture.
    bool start();

    // Stop the daemon and clean up.
    void stop();

    // Send a video frame through the transport.
    // The daemon writes the frame to the ring buffer and sends a notification.
    bool send_video_frame(const void* frame_data, uint32_t size);

    // Send an audio chunk through the transport.
    bool send_audio_chunk(const void* chunk_data, uint32_t size);

    // Is the daemon running?
    bool is_running() const { return running_.load(); }

    // Get statistics
    uint64_t frames_sent() const { return frames_sent_.load(); }
    uint64_t audio_sent() const { return audio_sent_.load(); }
    uint64_t frames_dropped() const { return frames_dropped_.load(); }

private:
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    // Transport
    std::unique_ptr<RingBuffer> video_ring_;
    std::unique_ptr<RingBuffer> audio_ring_;
    std::unique_ptr<VsockChannel> channel_;
    uint32_t video_sequence_ = 0;
    uint32_t audio_sequence_ = 0;

    // Config
    std::string ring_name_;
    uint32_t port_ = 9999;
    bool force_tcp_ = false;
    uint32_t frame_size_ = 0;
    uint32_t audio_chunk_size_ = 0;

    // Stats
    std::atomic<uint64_t> frames_sent_{0};
    std::atomic<uint64_t> audio_sent_{0};
    std::atomic<uint64_t> frames_dropped_{0};
};

} // namespace securestream
