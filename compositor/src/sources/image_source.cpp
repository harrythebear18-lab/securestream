#include "sources/image_source.hpp"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG
#include "stb_image.h"
#include <iostream>
#include <cstring>

namespace securestream {

ImageSource::ImageSource(std::string name, std::string file_path)
    : Source(std::move(name), SourceOrigin::VMLocal)
    , file_path_(std::move(file_path)) {}

void ImageSource::load() {
    int w, h, channels;
    unsigned char* img = stbi_load(file_path_.c_str(), &w, &h, &channels, 4);

    if (!img) {
        std::cerr << "ImageSource: failed to load " << file_path_
                  << ": " << stbi_failure_reason() << "\n";
        // Fall back to a solid colour so the pipeline doesn't break.
        cached_frame_.allocate(1920, 1080);
        for (uint32_t y = 0; y < cached_frame_.height; ++y) {
            for (uint32_t x = 0; x < cached_frame_.width; ++x) {
                size_t idx = static_cast<size_t>(y) * cached_frame_.stride +
                            static_cast<size_t>(x) * 4;
                cached_frame_.data[idx + 0] = 40;  // B
                cached_frame_.data[idx + 1] = 40;  // G
                cached_frame_.data[idx + 2] = 50;  // R
                cached_frame_.data[idx + 3] = 255; // A
            }
        }
        loaded_ = true;
        return;
    }

    cached_frame_.allocate(static_cast<uint32_t>(w), static_cast<uint32_t>(h));
    std::memcpy(cached_frame_.data.data(), img,
                static_cast<size_t>(w) * h * 4);
    stbi_image_free(img);

    // stb_image gives us RGBA, we need BGRA. Swap R and B channels.
    size_t pixel_count = static_cast<size_t>(w) * h;
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t idx = i * 4;
        std::swap(cached_frame_.data[idx + 0], cached_frame_.data[idx + 2]);
    }

    loaded_ = true;
    std::cout << "ImageSource: loaded " << file_path_
              << " (" << w << "x" << h << ", " << channels << " channels)\n";
}

Frame ImageSource::get_video_frame() {
    if (!loaded_) load();
    return cached_frame_;
}

} // namespace securestream
