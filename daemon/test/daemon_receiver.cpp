// Daemon receiver — the VM-side counterpart that receives frames from
// the daemon through the transport layer.
//
// Usage: daemon_receiver [--tcp] [port]
//
// Opens the daemon's ring buffers, connects to the daemon's vsock channel,
// and receives timestamped notifications. Reads frames from shared memory
// (zero-copy) and verifies integrity.

#include "ring_buffer.hpp"
#include "vsock_channel.hpp"
#include "clock.hpp"
#include "transport_packet.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <atomic>
#include <csignal>

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

    std::cout << "=== SecureStream Daemon Receiver ===\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Transport: " << (force_tcp ? "TCP loopback" : "HVSOCKET") << "\n\n";

    std::signal(SIGINT, signal_handler);
    Clock::init();

    // Open the daemon's ring buffers
    std::cout << "Waiting for daemon ring buffers...\n";
    RingBuffer video_ring, audio_ring;
    for (int i = 0; i < 50; i++) {
        if (video_ring.open("SecureStream_Daemon_video")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!video_ring.header()) {
        std::cerr << "Failed to open video ring buffer\n";
        return 1;
    }

    for (int i = 0; i < 50; i++) {
        if (audio_ring.open("SecureStream_Daemon_audio")) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!audio_ring.header()) {
        std::cerr << "Failed to open audio ring buffer\n";
        return 1;
    }

    std::cout << "Ring buffers opened. Connecting to daemon...\n";

    // Connect to the daemon
    VsockChannel channel;
    if (!channel.connect_host(port, force_tcp)) {
        std::cerr << "Failed to connect to daemon\n";
        return 1;
    }

    std::cout << "Connected. Receiving frames...\n\n";

    uint32_t expected_video_seq = 0;
    uint32_t expected_audio_seq = 0;
    uint32_t video_received = 0;
    uint32_t audio_received = 0;
    uint32_t video_dropped = 0;
    uint32_t crc_failures = 0;
    int64_t total_latency_us = 0;
    int64_t max_latency_us = 0;
    int64_t min_latency_us = INT64_MAX;

    while (!g_should_stop.load()) {
        TransportPacket pkt;
        if (!channel.recv(pkt)) {
            std::cout << "Daemon closed connection.\n";
            break;
        }

        if (pkt.magic != PACKET_MAGIC) {
            std::cerr << "Invalid packet magic\n";
            continue;
        }

        // Measure notification latency
        int64_t now = Clock::now_us();
        int64_t latency = now - pkt.capture_time;
        total_latency_us += latency;
        if (latency > max_latency_us) max_latency_us = latency;
        if (latency < min_latency_us) min_latency_us = latency;

        if (pkt.stream_id == static_cast<uint8_t>(StreamId::Video)) {
            // Check sequence
            if (pkt.sequence != expected_video_seq) {
                video_dropped += (pkt.sequence - expected_video_seq);
            }
            expected_video_seq = pkt.sequence + 1;

            // Read from video ring
            const void* slot = video_ring.pop_slot();
            if (!slot) {
                std::cerr << "  [error] video ring empty\n";
                continue;
            }

            // Verify CRC
            uint32_t crc = crc32(slot, pkt.payload_size);
            if (crc != pkt.checksum) {
                crc_failures++;
            }

            // Verify frame number
            uint32_t frame_num = *reinterpret_cast<const uint32_t*>(slot);
            video_ring.release_pop();
            video_received++;

            if (video_received % 60 == 0) {
                std::cout << "  video: " << video_received << " frames"
                          << " (latency: " << latency << " us)"
                          << " frame#" << frame_num << "\n";
            }
        } else if (pkt.stream_id == static_cast<uint8_t>(StreamId::Audio)) {
            // Check sequence
            if (pkt.sequence != expected_audio_seq) {
                // audio sequence gap
            }
            expected_audio_seq = pkt.sequence + 1;

            // Read from audio ring
            const void* slot = audio_ring.pop_slot();
            if (!slot) continue;

            audio_ring.release_pop();
            audio_received++;
        }
    }

    std::cout << "\n=== Receiver Results ===\n";
    std::cout << "  Video frames received: " << video_received << "\n";
    std::cout << "  Audio chunks received: " << audio_received << "\n";
    std::cout << "  Video frames dropped: " << video_dropped << "\n";
    std::cout << "  CRC failures: " << crc_failures << "\n";
    if (video_received > 0) {
        std::cout << "  Avg latency: " << (total_latency_us / video_received) << " us\n";
        std::cout << "  Min latency: " << min_latency_us << " us\n";
        std::cout << "  Max latency: " << max_latency_us << " us\n";
    }

    channel.close();
    video_ring.close();
    audio_ring.close();
    return 0;
}
