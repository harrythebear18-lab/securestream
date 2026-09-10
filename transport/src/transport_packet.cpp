#include "transport_packet.hpp"
#include <cstring>

namespace securestream {

// CRC32 using the IEEE 802.3 polynomial (same as in the RDMA project's
// udp_memory_bridge.py, but implemented in C++ for speed)
static uint32_t crc_table[256];
static bool crc_table_init = false;

static void init_crc_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            if (c & 1)
                c = 0xEDB88320 ^ (c >> 1);
            else
                c = c >> 1;
        }
        crc_table[i] = c;
    }
    crc_table_init = true;
}

uint32_t crc32(const void* data, uint32_t size) {
    if (!crc_table_init) init_crc_table();

    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint32_t crc = 0xFFFFFFFF;

    for (uint32_t i = 0; i < size; i++) {
        crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}

} // namespace securestream
