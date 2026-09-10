// Daemon test — sends synthetic video frames + audio chunks through the
// transport layer. Run with a transport_test receiver to verify.
//
// Usage: daemon_test [--tcp] [port]
//
// The daemon generates 300 frames (5 seconds at 60fps) of synthetic video
// and sends them through the ring buffer + vsock notification channel.

#include "daemon.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <csignal>
#include <atomic>

using namespace securestream;

static std::atomic<bool> g_should_stop{false};

void signal_handler(int) {
    g_should_stop.store(true);
}

int main(int argc, char* argv[]) {
    std::cout.setf(std::ios::unitbuf);

    bool force_tcp = false;
    uint32_t port = 9999;
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--tcp") force_tcp = true;
        else port = std::atoi(argv[i]);
    }

    std::cout << "=== SecureStream Daemon Test ===\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Transport: " << (force_tcp ? "TCP loopback" : "HVSOCKET (with TCP fallback)") << "\n\n";

    std::signal(SIGINT, signal_handler);

    Daemon daemon;
    if (!daemon.init("SecureStream_Daemon", port, force_tcp)) {
        std::cerr << "Failed to initialize daemon\n";
        return 1;
    }

    if (!daemon.start()) {
        std::cerr << "Failed to start daemon\n";
        return 1;
    }

    // Generate synthetic frames
    const uint32_t frame_width = 1920;
    const uint32_t frame_height = 1080;
    const uint32_t frame_size = frame_width * frame_height * 4;  // BGRA8
    std::vector<uint8_t> frame(frame_size);

    // Audio chunk: 48kHz, 2 channels, float32, 2ms = 48000*2*4*0.002 = 768 bytes
    const uint32_t audio_size = 48000 * 2 * 4 * 2 / 1000;  // ~768 bytes per 2ms
    std::vector<uint8_t> audio_chunk(audio_size);

    std::cout << "Sending 300 frames (5s at 60fps) + audio...\n\n";

    auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 300 && !g_should_stop.load(); i++) {
        // Generate a synthetic frame: gradient with frame number
        uint32_t color = i * 8;
        for (uint32_t y = 0; y < frame_height; y++) {
            for (uint32_t x = 0; x < frame_width; x++) {
                size_t idx = (y * frame_width + x) * 4;
                frame[idx + 0] = static_cast<uint8_t>((x + color) & 0xFF);  // B
                frame[idx + 1] = static_cast<uint8_t>((y + color) & 0xFF);  // G
                frame[idx + 2] = static_cast<uint8_t>(color & 0xFF);        // R
                frame[idx + 3] = 255;                                       // A
            }
        }
        // Write frame number in the top-left corner
        *reinterpret_cast<uint32_t*>(frame.data()) = i;

        // Send video frame
        daemon.send_video_frame(frame.data(), frame_size);

        // Send audio chunk (every other frame to simulate ~2ms chunks at 60fps)
        if (i % 2 == 0) {
            // Generate a simple sine wave
            float* samples = reinterpret_cast<float*>(audio_chunk.data());
            for (uint32_t s = 0; s < audio_size / 4; s++) {
                samples[s] = 0.1f * static_cast<float>(sin(i * 0.1 + s * 0.01));
            }
            daemon.send_audio_chunk(audio_chunk.data(), audio_size);
        }

        // 60fps timing
        std::this_thread::sleep_for(std::chrono::microseconds(16667));
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    std::cout << "\n=== Daemon Results ===\n";
    std::cout << "  Elapsed: " << elapsed / 1000.0 << " s\n";
    std::cout << "  Video frames sent: " << daemon.frames_sent() << "\n";
    std::cout << "  Audio chunks sent: " << daemon.audio_sent() << "\n";
    std::cout << "  Frames dropped: " << daemon.frames_dropped() << "\n";

    daemon.stop();
    std::cout << "=== Done ===\n";
    return 0;
}
