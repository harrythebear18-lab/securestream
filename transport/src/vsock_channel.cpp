#include "vsock_channel.hpp"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <hvsocket.h>  // Hyper-V vsock
#include <initguid.h>  // for GUID definitions
#include <iostream>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

// Define GUID_NULL and GUID_PARENT if not available
#ifndef GUID_NULL
static const GUID GUID_NULL_ = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };
#define GUID_NULL GUID_NULL_
#endif

// GUID_PARENT for Hyper-V: the host partition from the guest's perspective
// {00000000-0000-0000-0000-000000000000}
static const GUID GUID_PARENT_LOCAL = { 0, 0, 0, { 0, 0, 0, 0, 0, 0, 0, 0 } };
#define GUID_PARENT GUID_PARENT_LOCAL

namespace securestream {

// Check if HVSOCKET is available on this system
static bool hvsocket_available() {
    SOCKET s = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET) return false;
    closesocket(s);
    return true;
}

VsockChannel::VsockChannel() {}

VsockChannel::~VsockChannel() {
    close();
}

bool VsockChannel::try_hvsocket_listen(uint32_t port) {
    SOCKET s = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET) {
        return false;
    }

    // Bind to the wildcard VM ID (accept connections from any VM)
    SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    // GUID_NULL = {00000000-0000-0000-0000-000000000000} = wildcard
    addr.VmId = GUID_NULL;
    addr.ServiceId = GUID_NULL;
    // Set the service ID to the port number
    // HVSOCKET uses GUIDs for service IDs; we use a deterministic GUID
    // based on the port number for simplicity.
    // For now, we use a fixed GUID and encode the port in the bind.
    // Actually, HVSOCKET service IDs are GUIDs. We'll use a simple mapping:
    // port -> GUID {port, 0, 0, ...}
    memcpy(&addr.ServiceId, &port, sizeof(port));

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    if (::listen(s, 1) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    listen_socket_ = static_cast<int64_t>(s);
    return true;
}

bool VsockChannel::try_hvsocket_connect(uint32_t port) {
    SOCKET s = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET) {
        return false;
    }

    // Connect to the host (parent partition)
    // The host's VM ID from the guest's perspective is the parent's GUID.
    // We use GUID_PARENT {00000000-0000-0000-0000-000000000000} which
    // on Windows means "the host partition."
    SOCKADDR_HV addr = {};
    addr.Family = AF_HYPERV;
    addr.VmId = GUID_PARENT;  // connect to host
    memcpy(&addr.ServiceId, &port, sizeof(port));

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    socket_ = static_cast<int64_t>(s);
    connected_ = true;
    return true;
}

bool VsockChannel::try_tcp_listen(uint32_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    // Allow address reuse
    BOOL opt = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char*>(&opt), sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    if (::listen(s, 1) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    listen_socket_ = static_cast<int64_t>(s);
    return true;
}

bool VsockChannel::try_tcp_connect(uint32_t port) {
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) return false;

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        return false;
    }

    socket_ = static_cast<int64_t>(s);
    connected_ = true;
    return true;
}

bool VsockChannel::listen(uint32_t port, bool force_tcp) {
    is_server_ = true;

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "VsockChannel: WSAStartup failed\n";
        return false;
    }

    // Try HVSOCKET first (real vsock to VM), unless force_tcp
    if (!force_tcp && hvsocket_available()) {
        if (try_hvsocket_listen(port)) {
            std::cout << "VsockChannel: listening on HVSOCKET port " << port << "\n";
            return true;
        }
    }

    // Fall back to TCP loopback (for testing without a VM)
    if (try_tcp_listen(port)) {
        std::cout << "VsockChannel: listening on TCP 127.0.0.1:" << port
                  << (force_tcp ? " (forced TCP)" : " (HVSOCKET unavailable, using TCP fallback)") << "\n";
        return true;
    }

    std::cerr << "VsockChannel: could not listen on port " << port << "\n";
    return false;
}

bool VsockChannel::accept() {
    if (listen_socket_ < 0) return false;

    SOCKET client = ::accept(static_cast<SOCKET>(listen_socket_), nullptr, nullptr);
    if (client == INVALID_SOCKET) {
        std::cerr << "VsockChannel: accept failed\n";
        return false;
    }

    socket_ = static_cast<int64_t>(client);
    connected_ = true;
    std::cout << "VsockChannel: client connected\n";
    return true;
}

bool VsockChannel::connect_host(uint32_t port, bool force_tcp) {
    is_server_ = false;

    // Initialize Winsock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "VsockChannel: WSAStartup failed\n";
        return false;
    }

    // Try HVSOCKET first (real vsock to host), unless force_tcp
    if (!force_tcp && hvsocket_available()) {
        if (try_hvsocket_connect(port)) {
            std::cout << "VsockChannel: connected via HVSOCKET port " << port << "\n";
            return true;
        }
    }

    // Fall back to TCP loopback
    if (try_tcp_connect(port)) {
        std::cout << "VsockChannel: connected via TCP 127.0.0.1:" << port
                  << (force_tcp ? " (forced TCP)" : " (HVSOCKET unavailable, using TCP fallback)") << "\n";
        return true;
    }

    std::cerr << "VsockChannel: could not connect to port " << port << "\n";
    return false;
}

bool VsockChannel::send(const TransportPacket& packet) {
    if (!connected_ || socket_ < 0) return false;

    int sent = ::send(static_cast<SOCKET>(socket_),
                      reinterpret_cast<const char*>(&packet),
                      sizeof(TransportPacket), 0);
    return sent == sizeof(TransportPacket);
}

bool VsockChannel::recv(TransportPacket& packet) {
    if (!connected_ || socket_ < 0) return false;

    int total = 0;
    char* buf = reinterpret_cast<char*>(&packet);
    int remaining = sizeof(TransportPacket);

    while (remaining > 0) {
        int r = ::recv(static_cast<SOCKET>(socket_), buf + total, remaining, 0);
        if (r <= 0) {
            connected_ = false;
            return false;
        }
        total += r;
        remaining -= r;
    }

    return true;
}

void VsockChannel::close() {
    if (socket_ >= 0) {
        closesocket(static_cast<SOCKET>(socket_));
        socket_ = -1;
    }
    if (listen_socket_ >= 0) {
        closesocket(static_cast<SOCKET>(listen_socket_));
        listen_socket_ = -1;
    }
    connected_ = false;
    WSACleanup();
}

} // namespace securestream
