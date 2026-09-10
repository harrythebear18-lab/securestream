#include "ring_buffer.hpp"
#include "clock.hpp"

#include <windows.h>
#include <cstring>
#include <iostream>

namespace securestream {

RingBuffer::RingBuffer() {}

RingBuffer::~RingBuffer() {
    close();
}

bool RingBuffer::create(const std::string& name, uint32_t slot_size, uint32_t num_slots) {
    // num_slots must be a power of 2
    if (num_slots == 0 || (num_slots & (num_slots - 1)) != 0) {
        std::cerr << "RingBuffer: num_slots must be power of 2\n";
        return false;
    }

    uint32_t total_size = sizeof(RingHeader) + static_cast<uint64_t>(slot_size) * num_slots;

    // Create file mapping
    std::wstring wname(name.begin(), name.end());
    HANDLE hMap = CreateFileMappingW(
        INVALID_HANDLE_VALUE,  // backed by pagefile
        nullptr,
        PAGE_READWRITE,
        0,
        total_size,
        wname.c_str());

    if (!hMap || hMap == INVALID_HANDLE_VALUE) {
        std::cerr << "RingBuffer: CreateFileMapping failed ("
                  << GetLastError() << ")\n";
        return false;
    }
    file_mapping_ = hMap;

    // Map view
    mapped_ = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, total_size);
    if (!mapped_) {
        std::cerr << "RingBuffer: MapViewOfFile failed (" << GetLastError() << ")\n";
        CloseHandle(hMap);
        file_mapping_ = nullptr;
        return false;
    }

    // Initialize header
    header_ = static_cast<RingHeader*>(mapped_);
    header_->magic = TRANSPORT_MAGIC;
    header_->num_slots = num_slots;
    header_->slot_size = slot_size;
    header_->total_size = total_size;
    header_->rdtsc_freq = Clock::rdtsc_frequency();
    header_->head = 0;
    header_->tail = 0;

    slots_ = reinterpret_cast<uint8_t*>(mapped_) + sizeof(RingHeader);
    mask_ = num_slots - 1;
    cached_head_ = 0;
    cached_tail_ = 0;
    owns_ = true;

    std::cout << "RingBuffer: created '" << name << "' "
              << num_slots << " slots x " << slot_size << " bytes = "
              << total_size << " bytes total\n";
    return true;
}

bool RingBuffer::open(const std::string& name) {
    std::wstring wname(name.begin(), name.end());

    HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
    if (!hMap || hMap == INVALID_HANDLE_VALUE) {
        std::cerr << "RingBuffer: OpenFileMapping failed (" << GetLastError() << ")\n";
        return false;
    }
    file_mapping_ = hMap;

    // Map view — first just map the header to read sizes
    mapped_ = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!mapped_) {
        std::cerr << "RingBuffer: MapViewOfFile failed (" << GetLastError() << ")\n";
        CloseHandle(hMap);
        file_mapping_ = nullptr;
        return false;
    }

    header_ = static_cast<RingHeader*>(mapped_);

    // Validate
    if (header_->magic != TRANSPORT_MAGIC) {
        std::cerr << "RingBuffer: invalid magic (0x" << std::hex
                  << header_->magic << ")\n";
        UnmapViewOfFile(mapped_);
        mapped_ = nullptr;
        header_ = nullptr;
        CloseHandle(hMap);
        file_mapping_ = nullptr;
        return false;
    }

    slots_ = reinterpret_cast<uint8_t*>(mapped_) + sizeof(RingHeader);
    mask_ = header_->num_slots - 1;
    cached_head_ = header_->head;
    cached_tail_ = header_->tail;
    owns_ = false;

    std::cout << "RingBuffer: opened '" << name << "' "
              << header_->num_slots << " slots x " << header_->slot_size << " bytes\n";
    return true;
}

void RingBuffer::close() {
    if (mapped_) {
        UnmapViewOfFile(mapped_);
        mapped_ = nullptr;
    }
    if (file_mapping_) {
        CloseHandle(file_mapping_);
        file_mapping_ = nullptr;
    }
    header_ = nullptr;
    slots_ = nullptr;
    owns_ = false;
}

bool RingBuffer::push(const void* data, uint32_t size) {
    void* slot = push_slot();
    if (!slot) return false;

    uint32_t copy_size = size;
    if (copy_size > header_->slot_size) copy_size = header_->slot_size;
    std::memcpy(slot, data, copy_size);
    publish_push();
    return true;
}

void* RingBuffer::push_slot() {
    if (!header_) return nullptr;

    uint32_t head = header_->head;
    uint32_t next_head = (head + 1) & mask_;

    // Check if full (next_head would catch up to tail)
    if (next_head == header_->tail) {
        return nullptr;  // full, drop
    }

    // Return pointer to the slot at current head
    return slots_ + static_cast<uint64_t>(head) * header_->slot_size;
}

void RingBuffer::publish_push() {
    if (!header_) return;

    uint32_t head = header_->head;
    uint32_t next_head = (head + 1) & mask_;

    // Memory barrier: ensure the slot data is visible before the head update
    _WriteBarrier();
    MemoryBarrier();

    header_->head = next_head;
    cached_head_ = next_head;
}

const void* RingBuffer::pop_slot() {
    if (!header_) return nullptr;

    uint32_t tail = header_->tail;

    // Check if empty
    if (tail == header_->head) {
        return nullptr;  // empty
    }

    // Return pointer to the slot at current tail
    return slots_ + static_cast<uint64_t>(tail) * header_->slot_size;
}

void RingBuffer::release_pop() {
    if (!header_) return;

    uint32_t tail = header_->tail;
    uint32_t next_tail = (tail + 1) & mask_;

    // Memory barrier: ensure we've finished reading before releasing
    _ReadBarrier();
    MemoryBarrier();

    header_->tail = next_tail;
    cached_tail_ = next_tail;
}

bool RingBuffer::is_full() const {
    if (!header_) return false;
    uint32_t next_head = (header_->head + 1) & mask_;
    return next_head == header_->tail;
}

bool RingBuffer::is_empty() const {
    if (!header_) return true;
    return header_->head == header_->tail;
}

uint32_t RingBuffer::count() const {
    if (!header_) return 0;
    return (header_->head - header_->tail) & mask_;
}

} // namespace securestream
