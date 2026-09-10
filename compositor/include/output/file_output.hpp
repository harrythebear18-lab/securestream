#pragma once

#include "output.hpp"
#include <atomic>
#include <cstdint>
#include <string>

namespace securestream {

// FileOutput writes the encoded stream to an MP4 file for local recording.
// Uses Media Foundation's Sink Writer for H.264 video + AAC audio encoding.
// The file path is on the host (received via the daemon over vsock),
// never on VM-local storage.
class FileOutput : public Output {
public:
    FileOutput(std::string name, std::string file_path,
               uint32_t width = 1920, uint32_t height = 1080,
               uint32_t fps = 60,
               uint32_t video_bitrate = 6000000,  // 6 Mbps
               uint32_t audio_sample_rate = 48000,
               uint32_t audio_channels = 2,
               uint32_t audio_bitrate = 192000);  // 192 kbps
    ~FileOutput() override;

    FileOutput(const FileOutput&) = delete;
    FileOutput& operator=(const FileOutput&) = delete;

    bool start() override;
    void stop() override;
    void write_video(const Frame& frame, Timestamp pts) override;
    void write_audio(const AudioChunk& chunk, Timestamp pts) override;

private:
    std::string file_path_;
    uint32_t width_;
    uint32_t height_;
    uint32_t fps_;
    uint32_t video_bitrate_;
    uint32_t audio_sample_rate_;
    uint32_t audio_channels_;
    uint32_t audio_bitrate_;

    // Media Foundation sink writer
    void* pSinkWriter_ = nullptr;  // IMFSinkWriter*
    uint32_t video_stream_index_ = 0;
    uint32_t audio_stream_index_ = 0;
    bool mf_initialized_ = false;
    std::atomic<uint64_t> video_frames_{0};
    std::atomic<uint64_t> audio_chunks_{0};
    int64_t first_pts_ = -1;

    bool initialize();
    void shutdown();
};

} // namespace securestream
