#pragma once

#include "output.hpp"
#include <atomic>
#include <cstdint>
#include <string>

namespace securestream {

// SrtOutput encodes video (H.264) and audio (AAC) using Media Foundation
// and sends the encoded data over UDP to a remote endpoint.
//
// Phase 1: Uses raw UDP transport with a simple packetization scheme.
// Full SRT protocol support (reliability, encryption, flow control) will
// be added when libsrt is integrated in a later phase.
//
// The stream key is injected at start() and held in memory only.
class SrtOutput : public Output {
public:
    SrtOutput(std::string name, std::string url, std::string stream_key,
              uint32_t width = 1920, uint32_t height = 1080, uint32_t fps = 60,
              uint32_t video_bitrate = 6000000,
              uint32_t audio_sample_rate = 48000, uint32_t audio_channels = 2,
              uint32_t audio_bitrate = 192000);
    ~SrtOutput() override;

    SrtOutput(const SrtOutput&) = delete;
    SrtOutput& operator=(const SrtOutput&) = delete;

    bool start() override;
    void stop() override;
    void write_video(const Frame& frame, Timestamp pts) override;
    void write_audio(const AudioChunk& chunk, Timestamp pts) override;

private:
    std::string url_;
    std::string stream_key_;
    uint32_t width_;
    uint32_t height_;
    uint32_t fps_;
    uint32_t video_bitrate_;
    uint32_t audio_sample_rate_;
    uint32_t audio_channels_;
    uint32_t audio_bitrate_;

    // Media Foundation encoding
    void* pSinkWriter_ = nullptr;  // IMFSinkWriter* (writes to a custom byte stream)
    uint32_t video_stream_index_ = 0;
    uint32_t audio_stream_index_ = 0;
    bool mf_initialized_ = false;

    // UDP socket
    int64_t socket_ = -1;  // SOCKET handle, stored as int64_t for header simplicity
    std::atomic<uint64_t> video_frames_{0};
    std::atomic<uint64_t> audio_chunks_{0};
    int64_t first_pts_ = -1;

    bool initialize();
    bool parse_url(std::string& host, uint16_t& port);
};

} // namespace securestream
