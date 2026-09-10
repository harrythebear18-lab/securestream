#pragma once

#include "scene.hpp"
#include "source.hpp"
#include <memory>
#include <string>
#include <vector>

namespace securestream {

// Scene presets are pre-built scene layouts for common creator workflows.
// They create scenes with standard source arrangements that creators can
// then customise. This makes SecureStream feel creator-friendly rather than
// requiring manual scene setup from scratch.
//
// Presets are templates — they create the scene structure, but the creator
// still needs to assign actual sources (webcam, mic, etc.) to the slots.

// Scene slot: a placeholder for a source in a preset scene.
struct SceneSlot {
    std::string slot_name;    // e.g. "Webcam", "Background", "Clip"
    SourceOrigin expected_origin;
    int32_t x = 0;
    int32_t y = 0;
    float scale_x = 1.0f;
    float scale_y = 1.0f;
    bool visible = true;
};

// A scene preset: a named layout with slots.
struct ScenePreset {
    std::string name;
    std::string description;
    std::vector<SceneSlot> slots;
};

namespace presets {

// Commentary: webcam bottom-right, background fills screen, lower-third text
// Used by: commentary channels, analysis, news, OSINT
ScenePreset commentary();

// Reaction: large clip/video center, webcam small bottom-right
// Used by: reaction channels, clip breakdowns
ScenePreset reaction();

// Side-by-side: two equal halves, left for content, right for webcam
// Used by: comparisons, dual-commentary
ScenePreset side_by_side();

// Picture-in-picture: full-screen content with small webcam overlay
// Used by: tutorials, walkthroughs, gameplay with facecam
ScenePreset picture_in_picture();

// Full screen: single source fills the entire frame
// Used by: BRB, intermission, full-screen clips, slides
ScenePreset full_screen();

// Talking head: webcam centered, dark background, lower-third text
// Used by: direct-to-camera commentary, vlogs
ScenePreset talking_head();

// Get all available presets
std::vector<ScenePreset> all_presets();

// Create a scene from a preset. Returns the scene and the slot names
// that need to be filled with actual sources.
Scene create_scene_from_preset(const ScenePreset& preset);

} // namespace presets

} // namespace securestream
