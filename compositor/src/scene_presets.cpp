#include "scene_presets.hpp"
#include "sources/image_source.hpp"
#include "sources/text_source.hpp"
#include <iostream>

namespace securestream {

namespace presets {

ScenePreset commentary() {
    ScenePreset p;
    p.name = "Commentary";
    p.description = "Webcam bottom-right, background fills screen, lower-third text. For commentary, analysis, news, OSINT.";
    p.slots = {
        {"Background", SourceOrigin::VMLocal, 0, 0, 1.0f, 1.0f, true},
        {"Webcam", SourceOrigin::HostWebcam, 1480, 660, 0.25f, 0.25f, true},
        {"LowerThird", SourceOrigin::Generated, 100, 720, 1.0f, 1.0f, true},
    };
    return p;
}

ScenePreset reaction() {
    ScenePreset p;
    p.name = "Reaction";
    p.description = "Large clip/video center, webcam small bottom-right. For reaction channels, clip breakdowns.";
    p.slots = {
        {"Clip", SourceOrigin::VMLocal, 160, 0, 0.833f, 0.833f, true},
        {"Webcam", SourceOrigin::HostWebcam, 1480, 660, 0.25f, 0.25f, true},
    };
    return p;
}

ScenePreset side_by_side() {
    ScenePreset p;
    p.name = "Side-by-Side";
    p.description = "Two equal halves: left for content, right for webcam. For comparisons, dual-commentary.";
    p.slots = {
        {"LeftContent", SourceOrigin::VMLocal, 0, 0, 0.5f, 1.0f, true},
        {"RightWebcam", SourceOrigin::HostWebcam, 960, 0, 0.5f, 1.0f, true},
    };
    return p;
}

ScenePreset picture_in_picture() {
    ScenePreset p;
    p.name = "Picture-in-Picture";
    p.description = "Full-screen content with small webcam overlay. For tutorials, walkthroughs.";
    p.slots = {
        {"MainContent", SourceOrigin::VMLocal, 0, 0, 1.0f, 1.0f, true},
        {"Webcam", SourceOrigin::HostWebcam, 40, 40, 0.2f, 0.2f, true},
    };
    return p;
}

ScenePreset full_screen() {
    ScenePreset p;
    p.name = "Full Screen";
    p.description = "Single source fills the entire frame. For BRB, intermission, full-screen clips, slides.";
    p.slots = {
        {"Content", SourceOrigin::VMLocal, 0, 0, 1.0f, 1.0f, true},
    };
    return p;
}

ScenePreset talking_head() {
    ScenePreset p;
    p.name = "Talking Head";
    p.description = "Webcam centered, dark background, lower-third text. For direct-to-camera commentary, vlogs.";
    p.slots = {
        {"Background", SourceOrigin::VMLocal, 0, 0, 1.0f, 1.0f, true},
        {"Webcam", SourceOrigin::HostWebcam, 560, 100, 0.5f, 0.5f, true},
        {"LowerThird", SourceOrigin::Generated, 100, 720, 1.0f, 1.0f, true},
    };
    return p;
}

std::vector<ScenePreset> all_presets() {
    return {
        commentary(),
        reaction(),
        side_by_side(),
        picture_in_picture(),
        full_screen(),
        talking_head(),
    };
}

Scene create_scene_from_preset(const ScenePreset& preset) {
    Scene scene(preset.name);

    // Create placeholder sources for each slot. These are empty sources
    // that the creator will replace with real ones. For now, we create
    // image sources with a placeholder path so the scene structure is visible.
    //
    // In a real UI, the creator would drag real sources into these slots.
    for (const auto& slot : preset.slots) {
        // Create a placeholder source based on the expected origin
        if (slot.expected_origin == SourceOrigin::Generated) {
            auto src = std::make_shared<TextSource>(slot.slot_name, slot.slot_name);
            auto& item = scene.add_source(src);
            item.x = slot.x;
            item.y = slot.y;
            item.scale_x = slot.scale_x;
            item.scale_y = slot.scale_y;
            item.visible = slot.visible;
        } else {
            // Use a placeholder image path. The creator replaces this with
            // a real source via the UI.
            auto src = std::make_shared<ImageSource>(slot.slot_name,
                "[placeholder:" + slot.slot_name + "]");
            auto& item = scene.add_source(src);
            item.x = slot.x;
            item.y = slot.y;
            item.scale_x = slot.scale_x;
            item.scale_y = slot.scale_y;
            item.visible = slot.visible;
        }
    }

    return scene;
}

} // namespace presets

} // namespace securestream
