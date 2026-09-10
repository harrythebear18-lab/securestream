#include "compositor.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>

namespace securestream {

Compositor::Compositor(uint32_t width, uint32_t height, uint32_t fps)
    : width_(width), height_(height), fps_(fps)
    , transition_(TransitionType::Cut, std::chrono::milliseconds(0)) {}

Scene& Compositor::add_scene(std::string name) {
    auto scene = std::make_unique<Scene>(std::move(name));
    scenes_.push_back(std::move(scene));
    if (!active_scene_) active_scene_ = scenes_.back().get();
    return *scenes_.back();
}

bool Compositor::remove_scene(std::string_view name) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(),
        [&](const auto& s) { return s->name() == name; });
    if (it == scenes_.end()) return false;

    bool was_active = (active_scene_ == it->get());
    scenes_.erase(it);
    if (was_active) {
        active_scene_ = scenes_.empty() ? nullptr : scenes_.front().get();
    }
    return true;
}

Scene* Compositor::find_scene(std::string_view name) {
    auto it = std::find_if(scenes_.begin(), scenes_.end(),
        [&](const auto& s) { return s->name() == name; });
    return it == scenes_.end() ? nullptr : it->get();
}

bool Compositor::set_active_scene(std::string_view name,
                                   TransitionType transition_type,
                                   std::chrono::milliseconds duration) {
    auto* scene = find_scene(name);
    if (!scene) return false;

    // Configure and start the transition.
    transition_ = Transition(transition_type, duration);
    transition_.start(active_scene_, scene);
    active_scene_ = scene;
    return true;
}

std::string_view Compositor::active_scene_name() const {
    return active_scene_ ? active_scene_->name() : "";
}

void Compositor::add_output(std::shared_ptr<Output> output) {
    outputs_.push_back(std::move(output));
}

bool Compositor::remove_output(std::string_view name) {
    auto it = std::find_if(outputs_.begin(), outputs_.end(),
        [&](const auto& o) { return o->name() == name; });
    if (it == outputs_.end()) return false;
    if ((*it)->active()) (*it)->stop();
    outputs_.erase(it);
    return true;
}

void Compositor::run() {
    if (!active_scene_) {
        std::cerr << "Compositor: no active scene, cannot run.\n";
        return;
    }

    running_ = true;
    const auto frame_duration = std::chrono::microseconds(1000000 / fps_);
    auto start_time = std::chrono::steady_clock::now();
    uint64_t frame_num = 0;

    std::cout << "Compositor: starting render loop at "
              << width_ << "x" << height_ << "@" << fps_ << "fps\n";

    while (running_) {
        auto frame_start = std::chrono::steady_clock::now();
        auto pts = std::chrono::duration_cast<std::chrono::microseconds>(
            frame_start - start_time).count();

        render_and_dispatch(pts);

        frame_num++;
        if (frame_num % 60 == 0) {
            std::cout << "Compositor: rendered " << frame_num << " frames\n";
        }

        auto frame_end = std::chrono::steady_clock::now();
        auto elapsed = frame_end - frame_start;
        if (elapsed < frame_duration) {
            std::this_thread::sleep_for(frame_duration - elapsed);
        }
    }

    std::cout << "Compositor: stopped after " << frame_num << " frames\n";
}

void Compositor::stop() {
    running_ = false;
}

Frame Compositor::render_one_frame() {
    if (!active_scene_) {
        Frame f;
        f.allocate(width_, height_);
        return f;
    }
    transition_.update();
    return transition_.render(width_, height_);
}

void Compositor::render_and_dispatch(Timestamp pts) {
    if (!active_scene_) return;

    // Update transition state
    transition_.update();

    // Render either the transition (if active) or the current scene
    Frame frame = transition_.render(width_, height_);
    auto audio = transition_.collect_audio();

    for (auto& output : outputs_) {
        if (!output->active()) continue;
        output->write_video(frame, pts);
        for (const auto& chunk : audio) {
            output->write_audio(chunk, pts);
        }
    }
}

} // namespace securestream
