#include "transition.hpp"
#include <algorithm>

namespace securestream {

Transition::Transition(TransitionType type, std::chrono::milliseconds duration)
    : type_(type), duration_(duration) {}

void Transition::start(Scene* old_scene, Scene* new_scene) {
    old_scene_ = old_scene;
    new_scene_ = new_scene;
    if (type_ == TransitionType::Cut || !old_scene) {
        // Cut is instant — no blending needed.
        old_scene_ = nullptr;
    } else {
        start_time_ = std::chrono::steady_clock::now();
    }
}

bool Transition::update() {
    if (!old_scene_) return true;  // no transition active

    auto elapsed = std::chrono::steady_clock::now() - start_time_;
    if (elapsed >= duration_) {
        old_scene_ = nullptr;
        return true;
    }
    return false;
}

float Transition::progress() const {
    if (!old_scene_) return 1.0f;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time_);
    float t = static_cast<float>(elapsed.count()) /
              static_cast<float>(duration_.count());
    return std::clamp(t, 0.0f, 1.0f);
}

Frame Transition::blend(const Frame& old_frame, const Frame& new_frame, float t) {
    // Crossfade: output = old * (1-t) + new * t
    // Both frames should be the same resolution (compositor output size).
    Frame result;
    result.allocate(new_frame.width, new_frame.height);

    size_t pixel_count = static_cast<size_t>(result.stride) * result.height / 4;
    for (size_t i = 0; i < pixel_count; ++i) {
        size_t idx = i * 4;
        for (int c = 0; c < 4; ++c) {
            uint8_t old_val = (idx + c < old_frame.data.size())
                ? old_frame.data[idx + c] : 0;
            uint8_t new_val = (idx + c < new_frame.data.size())
                ? new_frame.data[idx + c] : 0;
            result.data[idx + c] = static_cast<uint8_t>(
                old_val * (1.0f - t) + new_val * t);
        }
    }
    return result;
}

Frame Transition::render(uint32_t width, uint32_t height) const {
    if (!old_scene_) {
        // No transition active — just render the current scene.
        if (new_scene_) return new_scene_->render(width, height);
        Frame f;
        f.allocate(width, height);
        return f;
    }

    // Transition active — render both scenes and blend.
    Frame old_frame = old_scene_->render(width, height);
    Frame new_frame = new_scene_->render(width, height);

    float t = progress();
    return blend(old_frame, new_frame, t);
}

std::vector<AudioChunk> Transition::collect_audio() const {
    // During a fade transition, we just take audio from the new scene.
    // True audio crossfade would require mixing, which is a later refinement.
    // During cut, the switch is instant so new scene audio is correct.
    if (new_scene_) return new_scene_->collect_audio();
    return {};
}

} // namespace securestream
