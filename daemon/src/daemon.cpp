#include "daemon.hpp"
#include <iostream>

namespace securestream {

Daemon::Daemon() {}

Daemon::~Daemon() {
    stop();
}

bool Daemon::init(const std::string& ring_name, uint32_t port, bool force_tcp,
                  uint32_t frame_size, uint32_t audio_chunk_size) {
    ring_name_ = ring_name;
    port_ = port;
    force_tcp_ = force_tcp;
    frame_size_ = frame_size;
    audio_chunk_size_ = audio_chunk_size;

    // Initialize the authoritative clock
    Clock::init();
    std::cout << "Daemon: clock initialized\n";

    // Create video ring buffer (8 slots for 1080p BGRA8)
    video_ring_ = std::make_unique<RingBuffer>();
    if (!video_ring_->create(ring_name_ + "_video", frame_size_, 8)) {
        std::cerr << "Daemon: failed to create video ring buffer\n";
        return false;
    }

    // Create audio ring buffer (32 slots for small audio chunks)
    audio_ring_ = std::make_unique<RingBuffer>();
    if (!audio_ring_->create(ring_name_ + "_audio", audio_chunk_size_, 32)) {
        std::cerr << "Daemon: failed to create audio ring buffer\n";
        return false;
    }

    // Create vsock notification channel
    channel_ = std::make_unique<VsockChannel>();
    if (!channel_->listen(port_, force_tcp_)) {
        std::cerr << "Daemon: failed to listen on port " << port_ << "\n";
        return false;
    }

    initialized_.store(true);
    std::cout << "Daemon: initialized (frame_size=" << frame_size_
              << " audio_chunk_size=" << audio_chunk_size_ << ")\n";
    return true;
}

bool Daemon::start() {
    if (!initialized_.load()) {
        std::cerr << "Daemon: not initialized\n";
        return false;
    }
    if (running_.load()) return true;

    std::cout << "Daemon: waiting for VM to connect...\n";
    if (!channel_->accept()) {
        std::cerr << "Daemon: failed to accept connection\n";
        return false;
    }

    running_.store(true);
    std::cout << "Daemon: started, VM connected\n";
    return true;
}

void Daemon::stop() {
    if (!running_.load()) return;
    running_.store(false);

    std::cout << "Daemon: stopping. Sent " << frames_sent_.load()
              << " video frames, " << audio_sent_.load()
              << " audio chunks, dropped " << frames_dropped_.load()
              << " frames.\n";

    if (channel_) channel_->close();
    if (video_ring_) video_ring_->close();
    if (audio_ring_) audio_ring_->close();

    initialized_.store(false);
}

bool Daemon::send_video_frame(const void* frame_data, uint32_t size) {
    if (!running_.load() || !video_ring_ || !channel_) return false;

    // Write frame to ring buffer (zero-copy: get slot, copy, publish)
    void* slot = video_ring_->push_slot();
    if (!slot) {
        frames_dropped_++;
        return false;
    }

    uint32_t copy_size = size < frame_size_ ? size : frame_size_;
    std::memcpy(slot, frame_data, copy_size);

    // Compute which slot we just wrote to
    uint32_t slot_index = (video_ring_->header()->head) & (8 - 1);

    // Publish the push (makes slot visible to consumer)
    video_ring_->publish_push();

    // Send notification packet
    TransportPacket pkt = {};
    pkt.magic = PACKET_MAGIC;
    pkt.stream_id = static_cast<uint8_t>(StreamId::Video);
    pkt.flags = (video_sequence_ == 0) ? static_cast<uint8_t>(PacketFlags::Keyframe) : 0;
    pkt.slot = slot_index;
    pkt.sequence = video_sequence_++;
    pkt.capture_time = Clock::now_us();
    pkt.presentation_time = pkt.capture_time + 4000;  // 4ms ahead
    pkt.payload_size = copy_size;
    pkt.checksum = crc32(slot, copy_size);
    pkt.reserved = 0;

    if (!channel_->send(pkt)) {
        std::cerr << "Daemon: failed to send video notification\n";
        return false;
    }

    frames_sent_++;
    return true;
}

bool Daemon::send_audio_chunk(const void* chunk_data, uint32_t size) {
    if (!running_.load() || !audio_ring_ || !channel_) return false;

    void* slot = audio_ring_->push_slot();
    if (!slot) {
        return false;  // audio ring full, drop
    }

    uint32_t copy_size = size < audio_chunk_size_ ? size : audio_chunk_size_;
    std::memcpy(slot, chunk_data, copy_size);

    uint32_t slot_index = (audio_ring_->header()->head) & (32 - 1);
    audio_ring_->publish_push();

    TransportPacket pkt = {};
    pkt.magic = PACKET_MAGIC;
    pkt.stream_id = static_cast<uint8_t>(StreamId::Audio);
    pkt.flags = 0;
    pkt.slot = slot_index;
    pkt.sequence = audio_sequence_++;
    pkt.capture_time = Clock::now_us();
    pkt.presentation_time = pkt.capture_time + 4000;
    pkt.payload_size = copy_size;
    pkt.checksum = crc32(slot, copy_size);
    pkt.reserved = 0;

    if (!channel_->send(pkt)) {
        std::cerr << "Daemon: failed to send audio notification\n";
        return false;
    }

    audio_sent_++;
    return true;
}

} // namespace securestream
