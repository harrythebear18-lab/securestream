#include "scene.hpp"
#include <algorithm>
#include <cstring>

namespace securestream {

SceneItem& Scene::add_source(std::shared_ptr<Source> source) {
    items_.emplace_back(std::move(source));
    return items_.back();
}

bool Scene::remove_source(std::string_view name) {
    auto it = std::find_if(items_.begin(), items_.end(),
        [&](const SceneItem& item) { return item.source->name() == name; });
    if (it == items_.end()) return false;
    items_.erase(it);
    return true;
}

SceneItem* Scene::find_item(std::string_view name) {
    auto it = std::find_if(items_.begin(), items_.end(),
        [&](const SceneItem& item) { return item.source->name() == name; });
    return it == items_.end() ? nullptr : &(*it);
}

const SceneItem* Scene::find_item(std::string_view name) const {
    auto it = std::find_if(items_.begin(), items_.end(),
        [&](const SceneItem& item) { return item.source->name() == name; });
    return it == items_.end() ? nullptr : &(*it);
}

Frame Scene::render(uint32_t width, uint32_t height) const {
    Frame output;
    output.allocate(width, height);

    // Compositing: for each visible, permitted item, get its frame,
    // apply per-item filters, then blit it onto the output at its position.
    for (const auto& item : items_) {
        if (!item.visible || !item.source->permitted()) continue;
        if (!item.source->has_video()) continue;

        Frame src = item.source->get_video_frame();
        if (!src.valid()) continue;

        // Apply per-item filters
        for (auto& filter : item.filters) {
            filter->apply(src);
        }

        // Blit src onto output at (item.x, item.y) with scaling.
        // Simple nearest-neighbour for Phase 1. Later: bilinear.
        uint32_t dst_w = static_cast<uint32_t>(src.width * item.scale_x);
        uint32_t dst_h = static_cast<uint32_t>(src.height * item.scale_y);

        for (uint32_t dy = 0; dy < dst_h; ++dy) {
            uint32_t sy = static_cast<uint32_t>(
                static_cast<float>(dy) / item.scale_y);
            if (sy >= src.height) break;

            int32_t out_y = item.y + static_cast<int32_t>(dy);
            if (out_y < 0 || out_y >= static_cast<int32_t>(height)) continue;

            for (uint32_t dx = 0; dx < dst_w; ++dx) {
                uint32_t sx = static_cast<uint32_t>(
                    static_cast<float>(dx) / item.scale_x);
                if (sx >= src.width) break;

                int32_t ox = item.x + static_cast<int32_t>(dx);
                if (ox < 0 || ox >= static_cast<int32_t>(width)) continue;

                size_t src_idx = static_cast<size_t>(sy) * src.stride +
                                 static_cast<size_t>(sx) * 4;
                size_t dst_idx = static_cast<size_t>(out_y) * output.stride +
                                static_cast<size_t>(ox) * 4;

                // Alpha blend (src over dst)
                uint8_t sa = src.data[src_idx + 3];
                if (sa == 255) {
                    std::memcpy(&output.data[dst_idx],
                                &src.data[src_idx], 4);
                } else if (sa > 0) {
                    float alpha = sa / 255.0f;
                    for (int c = 0; c < 3; ++c) {
                        output.data[dst_idx + c] = static_cast<uint8_t>(
                            src.data[src_idx + c] * alpha +
                            output.data[dst_idx + c] * (1.0f - alpha));
                    }
                    // Keep dst alpha
                }
            }
        }
    }

    return output;
}

std::vector<AudioChunk> Scene::collect_audio() const {
    std::vector<AudioChunk> chunks;
    for (const auto& item : items_) {
        if (!item.visible || !item.source->permitted()) continue;
        if (!item.source->has_audio()) continue;
        auto chunk = item.source->get_audio_chunk();
        if (chunk.valid()) chunks.push_back(std::move(chunk));
    }
    return chunks;
}

} // namespace securestream
