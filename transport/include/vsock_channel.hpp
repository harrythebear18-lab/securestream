#pragma once

#include "transport_packet.hpp"
#include <cstdint>
#include <string>

namespace securestream {

// VsockChannel — notification channel over Hyper-V vsock (HVSOCKET).
//
// The frame data lives in the shared memory ring buffer. This channel
// only carries 40-byte TransportPacket notifications ("new frame at
// slot N, pts=X, seq=Y").
//
// On Windows, Hyper-V vsock uses the HVSOCKET address family.
// The host daemon is the server (listens), the VM is the client (connects).
//
// For testing without a VM, we fall back to TCP loopback on the same
// port number. The protocol is identical — only the transport differs.

class VsockChannel {
public:
    VsockChannel();
    ~VsockChannel();

    VsockChannel(const VsockChannel&) = delete;
    VsockChannel& operator=(const VsockChannel&) = delete;

    // Server: listen on the given vsock port (or TCP port for testing).
    // force_tcp = true to skip HVSOCKET and use TCP loopback (for local testing).
    // Returns true if listening started successfully.
    bool listen(uint32_t port, bool force_tcp = false);

    // Server: accept a connection. Blocks until a client connects.
    bool accept();

    // Client: connect to the given vsock port on the host.
    // force_tcp = true to skip HVSOCKET and use TCP loopback (for local testing).
    bool connect_host(uint32_t port, bool force_tcp = false);

    // Send a notification packet. Returns true on success.
    bool send(const TransportPacket& packet);

    // Receive a notification packet. Blocks until a packet arrives.
    // Returns true on success, false on connection closed.
    bool recv(TransportPacket& packet);

    // Close the channel.
    void close();

    bool is_connected() const { return connected_; }

private:
    int64_t socket_ = -1;       // SOCKET handle
    int64_t listen_socket_ = -1; // listen socket (server)
    bool connected_ = false;
    bool is_server_ = false;

    // Try to use HVSOCKET (Hyper-V vsock). Falls back to TCP loopback.
    bool try_hvsocket_listen(uint32_t port);
    bool try_hvsocket_connect(uint32_t port);
    bool try_tcp_listen(uint32_t port);
    bool try_tcp_connect(uint32_t port);
};

} // namespace securestream
