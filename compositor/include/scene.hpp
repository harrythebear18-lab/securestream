#pragma once

#include "source.hpp"
#include "filter.hpp"
#include "types.hpp"
#include <memory>
#include <string>
#include <vector>

namespace securestream {

// A scene item is a source placed on a scene with position, scale, and
// associated filters. It is the unit of composition — the compositor renders
// a scene by compositing all its scene items in order.
struct SceneItem {
    std::shared_ptr<Source> source;
    int32_t x = 0;           // position in pixels
    int32_t y = 0;
    float scale_x = 1.0f;    // scale factor
    float scale_y = 1.0f;
    bool visible = true;
    std::vector<std::unique_ptr<Filter>> filters;  // per-item filters

    SceneItem(std::shared_ptr<Source> src) : source(std::move(src)) {}
};

// A scene is a collection of scene items. Switching scenes is the only way
// to change what's on stream. The compositor renders exactly one scene at a time.
class Scene {
public:
    Scene(std::string name) : name_(std::move(name)) {}

    // Add a source to this scene. Returns the item for further configuration.
    SceneItem& add_source(std::shared_ptr<Source> source);

    // Remove a source by name. Returns true if removed.
    bool remove_source(std::string_view name);

    // Find a scene item by source name.
    SceneItem* find_item(std::string_view name);
    const SceneItem* find_item(std::string_view name) const;

    // Render this scene to a single composited frame at the given resolution.
    // Items are composited in order (later items on top).
    // Only permitted and visible sources are rendered.
    Frame render(uint32_t width, uint32_t height) const;

    // Collect audio from all audio sources in this scene.
    std::vector<AudioChunk> collect_audio() const;

    std::string_view name() const { return name_; }
    const std::vector<SceneItem>& items() const { return items_; }

private:
    std::string name_;
    std::vector<SceneItem> items_;
};

} // namespace securestream
