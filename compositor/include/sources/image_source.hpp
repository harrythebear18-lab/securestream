#pragma once

#include "source.hpp"

namespace securestream {

// ImageSource loads a static image from the VM's user layer and produces
// it as a video frame. Provenance is VMLocal — the image must be a file
// the creator deliberately placed in the VM.
class ImageSource : public Source {
public:
    ImageSource(std::string name, std::string file_path);

    Frame get_video_frame() override;
    AudioChunk get_audio_chunk() override { return {}; }
    bool has_video() const override { return true; }
    bool has_audio() const override { return false; }

private:
    std::string file_path_;
    Frame cached_frame_;
    bool loaded_ = false;

    void load();
};

} // namespace securestream
