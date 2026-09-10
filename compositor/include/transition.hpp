#pragma once

#include "scene.hpp"
#include "types.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace securestream {

// Transition types. When switching scenes, a transition controls how the
// old scene fades/morphs into the new scene over a duration.
enum class TransitionType {
    Cut,   // instant switch, no blending
    Fade,  // crossfade from old to new over duration
};

// A Transition manages the handoff between two scenes.
// While a transition is in progress, the compositor renders BOTH scenes
// and blends them according to the transition's progress.
class Transition {
public:
    Transition(TransitionType type, std::chrono::milliseconds duration);

    // Start transitioning from old_scene to new_scene.
    void start(Scene* old_scene, Scene* new_scene);

    // Update progress. Returns true if the transition is complete.
    bool update();

    // Render the current transition state at the given resolution.
    // If no transition is active, renders the current (new) scene.
    Frame render(uint32_t width, uint32_t height) const;

    // Collect audio from the appropriate scene during transition.
    // During fade, audio crossfades. During cut, audio switches instantly.
    std::vector<AudioChunk> collect_audio() const;

    bool active() const { return old_scene_ != nullptr; }
    Scene* current_scene() const { return new_scene_; }
    float progress() const;

private:
    TransitionType type_;
    std::chrono::milliseconds duration_;
    std::chrono::steady_clock::time_point start_time_;

    Scene* old_scene_ = nullptr;
    Scene* new_scene_ = nullptr;

    // Blend two frames with alpha = t (0 = fully old, 1 = fully new).
    static Frame blend(const Frame& old_frame, const Frame& new_frame, float t);
};

} // namespace securestream
