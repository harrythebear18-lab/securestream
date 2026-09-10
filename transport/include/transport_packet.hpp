#pragma once

#include <cstdint>

namespace securestream {

// TransportPacket — the notification sent over vsock when a frame/audio
// chunk is available in the shared memory ring buffer.
//
// This is NOT the frame data. The frame data lives in the ring buffer slot.
// This packet only tells the receiver "go look at slot N."
//
// Adapted from the RDMA project's MemoryPacket (see DESIGN.md §5.7).
// 40 bytes — fits in a single vsock message.

constexpr uint32_t PACKET_MAGIC = 0x53535452;  // 'SSTR'

enum class StreamId : uint8_t {
    Video = 0,
    Audio = 1,
    Control = 2,
};

enum class PacketFlags : uint8_t {
    None = 0,
    Keyframe = 1 << 0,    // video keyframe
    Last = 1 << 1,        // last packet in a stream
    Resync = 1 << 2,      // clock resync request
    Reset = 1 << 3,       // ring buffer reset
};

#pragma pack(push, 1)
struct TransportPacket {
    uint32_t magic;          // PACKET_MAGIC
    uint8_t  stream_id;      // StreamId enum
    uint8_t  flags;          // PacketFlags bitmask
    uint16_t slot;           // ring buffer slot index
    uint32_t sequence;       // monotonic, for drop/reorder detection
    int64_t  capture_time;   // RDTSC-derived microseconds (host timeline)
    int64_t  presentation_time; // when compositor should present (host timeline)
    uint32_t payload_size;   // size of data in the ring slot
    uint32_t checksum;       // CRC32 of the ring slot data
    uint32_t reserved;       // padding to 40 bytes (future use)
};
#pragma pack(pop)
static_assert(sizeof(TransportPacket) == 40, "TransportPacket must be 40 bytes");

// Compute CRC32 (used for slot data integrity verification)
uint32_t crc32(const void* data, uint32_t size);

} // namespace securestream
