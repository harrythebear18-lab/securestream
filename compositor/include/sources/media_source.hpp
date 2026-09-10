#pragma once

#include "source.hpp"
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace securestream {

// MediaSource plays a video file (MP4, MKV, MOV, etc.) as a video + audio source.
// Provenance is VMLocal — the file must be one the creator deliberately placed
// in the VM's user layer.
//
// Uses Media Foundation's Source Reader for decoding. Supports:
//   - Play / pause / stop
//   - Looping
//   - Seek (later)
//   - Video (any MF-supported codec: H.264, HEVC, VP9, etc.)
//   - Audio (any MF-supported codec: AAC, MP3, Opus, etc.)
class MediaSource : public Source {
public:
    MediaSource(std::string name, std::string file_path, bool loop = true);
    ~MediaSource() override;

    MediaSource(const MediaSource&) = delete;
    MediaSource& operator=(const MediaSource&) = delete;

    Frame get_video_frame() override;
    AudioChunk get_audio_chunk() override;
    bool has_video() const override { return has_video_; }
    bool has_audio() const override { return has_audio_; }

    // Playback control
    void play();
    void pause();
    void stop();
    bool is_playing() const { return playing_.load(); }

private:
    std::string file_path_;
    bool loop_;
    bool has_video_ = false;
    bool has_audio_ = false;

    std::atomic<bool> playing_{false};
    std::atomic<bool> decoding_{false};
    std::thread decode_thread_;

    std::mutex video_mutex_;
    Frame latest_video_frame_;

    std::mutex audio_mutex_;
    std::vector<AudioChunk> audio_queue_;

    void decode_loop();
};

} // namespace securestream
