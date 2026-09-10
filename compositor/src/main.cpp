#include "compositor.hpp"
#include "sources/webcam_source.hpp"
#include "sources/microphone_source.hpp"
#include "sources/image_source.hpp"
#include "sources/text_source.hpp"
#include "sources/media_source.hpp"
#include "output/srt_output.hpp"
#include "output/file_output.hpp"
#include "scene_presets.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

static std::atomic<bool> g_should_stop{false};

void signal_handler(int) {
    g_should_stop.store(true);
}

int main() {
    using namespace securestream;

    // Flush immediately so we see output in pipes
    std::cout.setf(std::ios::unitbuf);

    std::cout << "=== SecureStream Compositor — Phase 1 Test ===\n";
    std::cout << "    (general-purpose broadcast surface — not gaming-only)\n\n";

    // Register Ctrl+C handler
    std::signal(SIGINT, signal_handler);

    // Create the compositor at 1080p60
    Compositor compositor(1920, 1080, 60);

    // --- List available scene presets ---
    std::cout << "=== Available Scene Presets ===\n";
    for (const auto& preset : presets::all_presets()) {
        std::cout << "  " << preset.name << ": " << preset.description << "\n";
    }
    std::cout << "\n";

    // --- Scene 1: "Commentary" — built from the Commentary preset ---
    // Commentary layout: background + webcam bottom-right + lower-third text
    auto& main_scene = compositor.add_scene("Commentary");

    auto bg = std::make_shared<ImageSource>("Background", "E:/SecureStream/test_bg.png");
    auto& bg_item = main_scene.add_source(bg);
    bg_item.scale_x = 1.0f;
    bg_item.scale_y = 1.0f;

    auto webcam = std::make_shared<WebcamSource>("Webcam", 1280, 720);
    auto& webcam_item = main_scene.add_source(webcam);
    webcam_item.x = 1480;
    webcam_item.y = 660;
    webcam_item.scale_x = 0.25f;
    webcam_item.scale_y = 0.25f;

    auto text = std::make_shared<TextSource>("LowerThird", "SecureStream — Commentary Mode");
    auto& text_item = main_scene.add_source(text);
    text_item.x = 100;
    text_item.y = 720;

    auto mic = std::make_shared<MicrophoneSource>("Microphone", 48000, 2);
    main_scene.add_source(mic);

    // --- Scene 2: "Reaction" — clip playback + webcam overlay ---
    // Reaction layout: media clip center + webcam small bottom-right
    auto& reaction_scene = compositor.add_scene("Reaction");

    // Use the MP4 we recorded earlier as a test clip
    auto clip = std::make_shared<MediaSource>("Clip", "E:/SecureStream/recordings/test.mp4", true);
    auto& clip_item = reaction_scene.add_source(clip);
    clip_item.x = 160;
    clip_item.y = 0;
    clip_item.scale_x = 0.833f;
    clip_item.scale_y = 0.833f;

    auto reaction_webcam = std::make_shared<WebcamSource>("Reaction Webcam", 1280, 720);
    auto& reaction_webcam_item = reaction_scene.add_source(reaction_webcam);
    reaction_webcam_item.x = 1480;
    reaction_webcam_item.y = 660;
    reaction_webcam_item.scale_x = 0.25f;
    reaction_webcam_item.scale_y = 0.25f;

    auto reaction_mic = std::make_shared<MicrophoneSource>("Reaction Mic", 48000, 2);
    reaction_scene.add_source(reaction_mic);

    // --- Scene 3: "BRB" — full screen background ---
    auto& brb_scene = compositor.add_scene("BRB");
    auto brb_bg = std::make_shared<ImageSource>("BRB Background", "E:/SecureStream/test_brb.png");
    brb_scene.add_source(brb_bg);

    // --- Outputs ---
    auto srt = std::make_shared<SrtOutput>(
        "Live", "udp://127.0.0.1:8888", "test-stream-key",
        1920, 1080, 60, 6000000, 48000, 2, 192000);
    auto file = std::make_shared<FileOutput>(
        "Recording", "E:/SecureStream/recordings/test2.mp4",
        1920, 1080, 60, 6000000, 48000, 2, 192000);

    compositor.add_output(srt);
    compositor.add_output(file);

    // --- Print scene/source provenance (this is the security audit) ---
    std::cout << "=== Source Provenance Audit ===\n";
    for (const auto& scene_name : {"Commentary", "Reaction", "BRB"}) {
        auto* scene = compositor.find_scene(scene_name);
        if (!scene) continue;
        std::cout << "Scene: " << scene->name() << "\n";
        for (const auto& item : scene->items()) {
            std::cout << "  Source: " << item.source->name()
                      << " | Origin: " << origin_to_string(item.source->origin())
                      << " | Permitted: " << (item.source->permitted() ? "yes" : "no")
                      << " | Visible: " << (item.visible ? "yes" : "no")
                      << " | Video: " << (item.source->has_video() ? "yes" : "no")
                      << " | Audio: " << (item.source->has_audio() ? "yes" : "no")
                      << "\n";
        }
    }
    std::cout << "\n";

    // --- Render a single test frame to verify the pipeline ---
    std::cout << "=== Rendering test frame ===\n";
    compositor.set_active_scene("Commentary");
    Frame test = compositor.render_one_frame();
    std::cout << "Test frame: " << test.width << "x" << test.height
              << ", " << test.data.size() << " bytes, "
              << (test.valid() ? "valid" : "invalid") << "\n\n";

    // --- Start outputs ---
    srt->start();
    file->start();

    // --- Run the render loop ---
    std::cout << "=== Starting render loop (auto-stop after 8s for test) ===\n";
    std::cout << "    0-3s: Commentary scene\n";
    std::cout << "    3-3.5s: Fade to Reaction scene\n";
    std::cout << "    3.5-6s: Reaction scene (clip playback)\n";
    std::cout << "    6-6.5s: Fade to BRB scene\n";
    std::cout << "    6.5-8s: BRB scene\n\n";

    // Run in a separate thread so we can handle Ctrl+C
    std::thread render_thread([&]() {
        compositor.run();
    });

    // Scene switching timeline
    auto t0 = std::chrono::steady_clock::now();
    auto fade_to_reaction = t0 + std::chrono::seconds(3);
    auto fade_to_brb = t0 + std::chrono::seconds(6);
    auto stop_time = t0 + std::chrono::seconds(8);

    int phase = 0;
    while (!g_should_stop.load() && std::chrono::steady_clock::now() < stop_time) {
        auto now = std::chrono::steady_clock::now();

        if (phase == 0 && now >= fade_to_reaction) {
            std::cout << "\n=== Fading to Reaction scene (500ms fade) ===\n";
            compositor.set_active_scene("Reaction", TransitionType::Fade,
                                         std::chrono::milliseconds(500));
            phase = 1;
        }
        if (phase == 1 && now >= fade_to_brb) {
            std::cout << "\n=== Fading to BRB scene (500ms fade) ===\n";
            compositor.set_active_scene("BRB", TransitionType::Fade,
                                         std::chrono::milliseconds(500));
            phase = 2;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    std::cout << "\n=== Stopping ===\n";
    compositor.stop();
    render_thread.join();

    srt->stop();
    file->stop();

    std::cout << "\n=== Done ===\n";
    return 0;
}
