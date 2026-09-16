#pragma once

#include "w5100.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace u2 {

constexpr int kRequestRingDepth = 64;
constexpr int kEventRingDepth = 64;

enum class NetMsg : uint8_t {
    None = 0,
    Shutdown,
    OpenTcp,
    OpenUdp,
    Listen,
    Connect,
    Close,      // close host fd and report Status CLOSED
    CloseHost,  // close host fd only (no Status) — used before reopen
    SendTcp,
    SendUdp,
    MacRawTx,
    IpRawTx,
    AdoptListen, // FORK: take over a socket the chip bound and listened on synchronously
};

enum class NetEvt : uint8_t {
    None = 0,
    Status,
    RxTcp,
    RxUdp,
    MacRawRx,
    IpRawRx,
    SendDone,    // FORK: everything posted for this socket has left for the host stack
};

// FORK: why a Status event moved a socket, so the chip raises the interrupt the
// W5100 would: CON on a connection, DISCON on a remote close or reset, TIMEOUT
// on a connect or send the host refused.
enum class NetCause : uint8_t {
    None = 0,
    Connected,     // outbound connect completed, or an inbound one accepted
    RemoteClosed,  // orderly FIN from the peer (-> CLOSE_WAIT)
    Reset,         // connection lost (reset, receive error)
    Failed,        // connect/open/send refused by the host
};

struct NetRequest {
    NetMsg msg = NetMsg::None;
    uint8_t sock = 0;
    uint32_t gen = 0;       // FORK: the socket's generation when the chip posted this
    intptr_t fd = -1;       // FORK: AdoptListen's already-listening host socket
    uint16_t local_port = 0;
    uint32_t dest_ip = 0;   // network byte order
    uint16_t dest_port = 0; // host order
    uint8_t ip_proto = 0;
    std::vector<uint8_t> payload;
};

struct NetEvent {
    NetEvt evt = NetEvt::None;
    uint8_t sock = 0;
    uint8_t status = 0;
    uint32_t gen = 0;       // FORK: stale events (an older generation) are discarded
    NetCause cause = NetCause::None;
    int host_err = 0;       // FORK: the host socket error behind a Failed/Reset, for the record
    uint32_t src_ip = 0;   // network byte order
    uint16_t src_port = 0; // host order
    uint8_t ip_proto = 0;
    std::vector<uint8_t> payload;
};

template <typename T, int Depth>
class SpscRing {
    static_assert((Depth & (Depth - 1)) == 0, "Depth must be power of two");
    T slots_[Depth]{};
    std::atomic<uint32_t> head_{0};
    std::atomic<uint32_t> tail_{0};

public:
    bool send(T item) {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t next = (h + 1) & (Depth - 1);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        slots_[h] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    bool get(T *out) {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) {
            return false;
        }
        *out = std::move(slots_[t]);
        tail_.store((t + 1) & (Depth - 1), std::memory_order_release);
        return true;
    }

    bool can_send() const {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        const uint32_t next = (h + 1) & (Depth - 1);
        return next != tail_.load(std::memory_order_acquire);
    }
};

inline uint16_t read_be16(const uint8_t *p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

inline uint32_t read_be32(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void write_be16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

}  // namespace u2
