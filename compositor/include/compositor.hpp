#pragma once

#include "scene.hpp"
#include "output.hpp"
#include "transition.hpp"
#include "types.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace securestream {

// The Compositor is the core of SecureStream. It owns scenes, manages the
// active scene, and drives the render loop that produces frames for outputs.
//
// Security properties enforced here:
//   - Only the active scene is rendered. No background capture.
//   - Only permitted sources within the active scene are rendered.
//   - Only registered outputs receive frames. No implicit disk writes.
//   - The render loop is the single path from sources to outputs.
class Compositor {
public:
    Compositor(uint32_t width = 1920, uint32_t height = 1080, uint32_t fps = 60);

    // Scene management.
    Scene& add_scene(std::string name);
    bool remove_scene(std::string_view name);
    Scene* find_scene(std::string_view name);

    // Set the active scene with a transition. If transition_type is Cut
    // and duration is 0, switches instantly. Otherwise crossfades.
    // Returns false if the target scene doesn't exist.
    bool set_active_scene(std::string_view name,
                          TransitionType transition_type = TransitionType::Cut,
                          std::chrono::milliseconds duration = std::chrono::milliseconds(0));
    std::string_view active_scene_name() const;

    // Output management.
    void add_output(std::shared_ptr<Output> output);
    bool remove_output(std::string_view name);

    // Start the render loop. Blocks until stop() is called.
    void run();

    // Signal the render loop to stop.
    void stop();

    // Render one frame without running the full loop. Used for testing.
    Frame render_one_frame();

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t fps() const { return fps_; }

private:
    uint32_t width_;
    uint32_t height_;
    uint32_t fps_;
    bool running_ = false;

    std::vector<std::unique_ptr<Scene>> scenes_;
    Scene* active_scene_ = nullptr;

    Transition transition_;

    std::vector<std::shared_ptr<Output>> outputs_;

    // Render the active scene (or transition) and push to all active outputs.
    void render_and_dispatch(Timestamp pts);
};

} // namespace securestream
