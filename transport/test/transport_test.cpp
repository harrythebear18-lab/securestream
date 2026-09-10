// Transport latency test
//
// Two modes:
//   sender   — creates the ring buffer, starts vsock listener, pushes frames
//   receiver — opens the ring buffer, connects to vsock, receives notifications, reads frames
//
// Usage:
//   transport_test sender   [port] [num_frames] [frame_size]
//   transport_test receiver [port]
//
// Default: port 9999, 1000 frames, 8294400 bytes (1080p BGRA8)
//
// The test measures:
//   - Round-trip notification latency (send -> recv)
//   - Frame integrity (CRC32 verification)
//   - Drop count (frames that couldn't be pushed because ring was full)
//   - Throughput (MB/s)

#include "clock.hpp"
#include "ring_buffer.hpp"
#include "transport_packet.hpp"
#include "vsock_channel.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <cstdlib>
#include <string>

using namespace securestream;

static const char* RING_NAME = "SecureStream_TransportRing";
static const uint32_t DEFAULT_PORT = 9999;
static const uint32_t DEFAULT_FRAMES = 1000;
static const uint32_t DEFAULT_FRAME_SIZE = 1920 * 1080 * 4;  // 1080p BGRA8
static const uint32_t RING_SLOTS = 8;  // small ring to test backpressure

void run_sender(uint32_t port, uint32_t num_frames, uint32_t frame_size, bool force_tcp) {
    std::cout << "=== Transport Test: Sender ===\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Frames: " << num_frames << "\n";
    std::cout << "  Frame size: " << frame_size << " bytes ("
              << frame_size / 1024.0 / 1024.0 << " MB)\n\n";

    // Initialize the clock
    Clock::init();

    // Create the ring buffer
    RingBuffer ring;
    if (!ring.create(RING_NAME, frame_size, RING_SLOTS)) {
        std::cerr << "Failed to create ring buffer\n";
        return;
    }

    // Start vsock listener
    VsockChannel channel;
    if (!channel.listen(port, force_tcp)) {
        std::cerr << "Failed to listen\n";
        return;
    }

    std::cout << "Waiting for receiver to connect...\n";
    if (!channel.accept()) {
        std::cerr << "Failed to accept connection\n";
        return;
    }

    std::cout << "Receiver connected. Starting transfer...\n\n";

    // Allocate a test frame with a known pattern
    std::vector<uint8_t> frame(frame_size);
    uint32_t sequence = 0;
    uint32_t dropped = 0;

    auto start_time = std::chrono::steady_clock::now();

    for (uint32_t i = 0; i < num_frames; i++) {
        // Fill frame with a pattern (frame number + sequence)
        std::memset(frame.data(), 0, frame_size);
        *reinterpret_cast<uint32_t*>(frame.data()) = i;  // frame number
        *reinterpret_cast<uint32_t*>(frame.data() + 4) = sequence;  // sequence

        // Try to push into the ring
        void* slot = ring.push_slot();
        if (!slot) {
            dropped++;
            // Wait for the receiver to consume
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            slot = ring.push_slot();
            if (!slot) {
                dropped++;
                continue;
            }
        }

        // Copy frame data into the slot
        std::memcpy(slot, frame.data(), frame_size);

        // Publish the push
        ring.publish_push();

        // Send notification
        TransportPacket pkt = {};
        pkt.magic = PACKET_MAGIC;
        pkt.stream_id = static_cast<uint8_t>(StreamId::Video);
        pkt.flags = (i == 0) ? static_cast<uint8_t>(PacketFlags::Keyframe) : 0;
        pkt.slot = (ring.header()->head - 1) & (RING_SLOTS - 1);
        pkt.sequence = sequence;
        pkt.capture_time = Clock::now_us();
        pkt.presentation_time = pkt.capture_time + 4000;  // 4ms ahead
        pkt.payload_size = frame_size;
        pkt.checksum = crc32(slot, frame_size);

        if (!channel.send(pkt)) {
            std::cerr << "Failed to send notification at frame " << i << "\n";
            break;
        }

        sequence++;

        // Simulate 60fps capture (16.67ms per frame)
        std::this_thread::sleep_for(std::chrono::microseconds(16667));
    }

    auto end_time = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time).count();

    double elapsed_sec = elapsed / 1000000.0;
    double throughput = (num_frames * frame_size) / elapsed_sec / 1024.0 / 1024.0;

    std::cout << "\n=== Sender Results ===\n";
    std::cout << "  Frames sent: " << sequence << "\n";
    std::cout << "  Frames dropped: " << dropped << "\n";
    std::cout << "  Elapsed: " << elapsed_sec << " s\n";
    std::cout << "  Throughput: " << throughput << " MB/s\n";
    std::cout << "  Drop rate: " << (100.0 * dropped / num_frames) << "%\n";

    // Wait a bit for the receiver to finish
    std::this_thread::sleep_for(std::chrono::seconds(1));
    channel.close();
    ring.close();
}

void run_receiver(uint32_t port, bool force_tcp) {
    std::cout << "=== Transport Test: Receiver ===\n";
    std::cout << "  Port: " << port << "\n\n";

    // Initialize the clock (for measuring latency)
    Clock::init();

    // Wait for the ring buffer to be created by the sender
    std::cout << "Waiting for ring buffer...\n";
    RingBuffer ring;
    for (int i = 0; i < 50; i++) {
        if (ring.open(RING_NAME)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!ring.header()) {
        std::cerr << "Failed to open ring buffer\n";
        return;
    }

    std::cout << "Ring buffer opened. Connecting to sender...\n";

    // Connect to the sender's vsock channel
    VsockChannel channel;
    if (!channel.connect_host(port, force_tcp)) {
        std::cerr << "Failed to connect\n";
        return;
    }

    std::cout << "Connected. Receiving frames...\n\n";

    uint32_t expected_sequence = 0;
    uint32_t received = 0;
    uint32_t dropped = 0;
    uint32_t crc_failures = 0;
    int64_t total_latency_us = 0;
    int64_t max_latency_us = 0;
    int64_t min_latency_us = INT64_MAX;

    while (true) {
        TransportPacket pkt;
        if (!channel.recv(pkt)) {
            std::cout << "Connection closed by sender.\n";
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

        // Check sequence for drop detection
        if (pkt.sequence != expected_sequence) {
            uint32_t missed = pkt.sequence - expected_sequence;
            dropped += missed;
            std::cout << "  [seq gap] expected=" << expected_sequence
                      << " got=" << pkt.sequence
                      << " missed=" << missed << "\n";
        }
        expected_sequence = pkt.sequence + 1;

        // Read the frame from the ring buffer slot
        const void* slot = ring.pop_slot();
        if (!slot) {
            std::cerr << "  [error] ring empty but notification received\n";
            continue;
        }

        // Verify CRC
        uint32_t computed_crc = crc32(slot, pkt.payload_size);
        if (computed_crc != pkt.checksum) {
            crc_failures++;
            std::cerr << "  [crc] mismatch at seq=" << pkt.sequence << "\n";
        }

        // Verify frame content
        uint32_t frame_num = *reinterpret_cast<const uint32_t*>(slot);
        uint32_t frame_seq = *reinterpret_cast<const uint32_t*>(
            static_cast<const uint8_t*>(slot) + 4);

        if (frame_seq != pkt.sequence) {
            std::cerr << "  [data] seq mismatch: packet=" << pkt.sequence
                      << " data=" << frame_seq << "\n";
        }

        // Release the slot
        ring.release_pop();
        received++;

        if (received % 100 == 0) {
            std::cout << "  received " << received << " frames"
                      << " (latency: " << latency << " us)"
                      << " frame#" << frame_num << "\n";
        }
    }

    std::cout << "\n=== Receiver Results ===\n";
    std::cout << "  Frames received: " << received << "\n";
    std::cout << "  Frames dropped (seq gaps): " << dropped << "\n";
    std::cout << "  CRC failures: " << crc_failures << "\n";
    if (received > 0) {
        std::cout << "  Avg latency: " << (total_latency_us / received) << " us\n";
        std::cout << "  Min latency: " << min_latency_us << " us\n";
        std::cout << "  Max latency: " << max_latency_us << " us\n";
    }

    channel.close();
    ring.close();
}

int main(int argc, char* argv[]) {
    std::cout.setf(std::ios::unitbuf);

    if (argc < 2) {
        std::cout << "Usage: transport_test sender|receiver [port] [num_frames] [frame_size] [--tcp]\n";
        std::cout << "\n";
        std::cout << "  sender   [port] [num_frames] [frame_size] [--tcp]\n";
        std::cout << "  receiver [port] [--tcp]\n";
        std::cout << "\n";
        std::cout << "  --tcp    force TCP loopback (for testing without a VM)\n";
        std::cout << "\n";
        std::cout << "Defaults: port=" << DEFAULT_PORT
                  << " frames=" << DEFAULT_FRAMES
                  << " frame_size=" << DEFAULT_FRAME_SIZE << "\n";
        return 1;
    }

    std::string mode = argv[1];
    uint32_t port = (argc > 2) ? std::atoi(argv[2]) : DEFAULT_PORT;

    // Check for --tcp flag
    bool force_tcp = false;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--tcp") force_tcp = true;
    }

    uint32_t num_frames = DEFAULT_FRAMES;
    uint32_t frame_size = DEFAULT_FRAME_SIZE;

    // Parse numeric args (skip --tcp)
    int num_arg = 0;
    for (int i = 2; i < argc; i++) {
        if (std::string(argv[i]) == "--tcp") continue;
        num_arg++;
        if (num_arg == 2) num_frames = std::atoi(argv[i]);
        else if (num_arg == 3) frame_size = std::atoi(argv[i]);
    }

    if (mode == "sender") {
        run_sender(port, num_frames, frame_size, force_tcp);
    } else if (mode == "receiver") {
        run_receiver(port, force_tcp);
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    return 0;
}
