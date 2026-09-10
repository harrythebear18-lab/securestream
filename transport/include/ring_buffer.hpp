#pragma once

#include <cstdint>
#include <string>

namespace securestream {

// RingBuffer — lock-free single-producer single-consumer ring buffer
// backed by Windows shared memory (file mapping).
//
// Adapted from the software-defined RDMA project's MemoryMappedRingBuffer
// and LOCKFREE_RING (see DESIGN.md §5.7).
//
// Properties:
//   - Power-of-2 sizing (mask instead of modulo for speed)
//   - Head/tail in separate cache lines (no false sharing)
//   - Memory barriers before publishing indices (correct visibility)
//   - Drop-on-full, never block (producer never stalls)
//   - Zero-copy: producer writes directly to slot, consumer reads directly
//
// Single producer (daemon) pushes frames.
// Single consumer (VM compositor) pops frames.
// This is the classic SPSC lock-free ring — no locks needed.
//
// Layout in shared memory:
//   [Header (cache-line aligned)] [Slot 0] [Slot 1] ... [Slot N-1]
//
// Header contains head/tail indices and metadata.
// Each slot is item_size bytes.

constexpr uint32_t TRANSPORT_MAGIC = 0x53535452;  // 'SSTR'

// Ring buffer header — lives at the start of the shared memory region.
// Padded to 128 bytes (two cache lines) to keep head and tail on
// separate cache lines.
struct RingHeader {
    // First cache line (64 bytes): metadata + head
    uint32_t magic;           // TRANSPORT_MAGIC for validation
    uint32_t num_slots;       // number of slots (power of 2)
    uint32_t slot_size;       // bytes per slot
    uint32_t total_size;      // total shared memory size
    uint64_t rdtsc_freq;      // host RDTSC frequency (for VM clock correlation)
    volatile uint32_t head;   // write index (daemon updates)
    uint8_t pad1[36];         // pad to 64-byte cache line boundary

    // Second cache line (64 bytes): tail only
    volatile uint32_t tail;   // read index (VM updates)
    uint8_t pad2[60];         // pad to 64-byte cache line boundary
};
static_assert(sizeof(RingHeader) == 128, "RingHeader must be 128 bytes (2 cache lines)");

class RingBuffer {
public:
    RingBuffer();
    ~RingBuffer();

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    // Create a new shared memory ring buffer (producer side).
    // name is the Windows file mapping name.
    // slot_size is the size of each slot in bytes.
    // num_slots must be a power of 2.
    bool create(const std::string& name, uint32_t slot_size, uint32_t num_slots);

    // Open an existing shared memory ring buffer (consumer side).
    bool open(const std::string& name);

    // Close and unmap.
    void close();

    // Push a frame into the ring. Returns false if the ring is full (drop).
    // The data is copied into the slot. For zero-copy, use push_slot() instead.
    bool push(const void* data, uint32_t size);

    // Get a pointer to the next available slot for writing (zero-copy).
    // Returns nullptr if the ring is full.
    // After writing, call publish_push() to make the slot visible to the consumer.
    void* push_slot();

    // Publish the slot after writing (advances head).
    void publish_push();

    // Pop a frame from the ring. Returns nullptr if empty.
    // The returned pointer is valid until release_pop() is called.
    const void* pop_slot();

    // Release the popped slot (advances tail).
    void release_pop();

    // Ring state
    bool is_full() const;
    bool is_empty() const;
    uint32_t count() const;
    uint32_t slot_size() const { return header_ ? header_->slot_size : 0; }
    uint32_t num_slots() const { return header_ ? header_->num_slots : 0; }

    // Get the header (for clock correlation, metadata)
    const RingHeader* header() const { return header_; }

private:
    void* mapped_ = nullptr;       // MapViewOfFile
    void* file_mapping_ = nullptr;  // CreateFileMapping handle
    RingHeader* header_ = nullptr;  // Points to start of mapped_
    uint8_t* slots_ = nullptr;      // Points to first slot
    bool owns_ = false;             // true if we created it, false if opened
    uint32_t mask_ = 0;             // num_slots - 1

    uint32_t cached_head_ = 0;      // cached head for producer
    uint32_t cached_tail_ = 0;      // cached tail for consumer
};

} // namespace securestream
