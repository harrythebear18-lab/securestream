#pragma once

#include "source.hpp"

namespace securestream {

// TextSource renders text as a video frame. Provenance is Generated —
// the compositor produces this itself, no external data involved.
class TextSource : public Source {
public:
    TextSource(std::string name, std::string text,
               uint32_t width = 1920, uint32_t height = 1080);

    Frame get_video_frame() override;
    AudioChunk get_audio_chunk() override { return {}; }
    bool has_video() const override { return true; }
    bool has_audio() const override { return false; }

    void set_text(std::string text);

private:
    std::string text_;
    uint32_t width_;
    uint32_t height_;
    Frame cached_frame_;
    bool dirty_ = true;

    void render_text();
};

} // namespace securestream
