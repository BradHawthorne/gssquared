#pragma once

#include "net_protocol.hpp"
#include "slirp_backend.hpp"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace u2 {

// FORK: host-side counters, written by the worker thread and read (racy but
// monotonic) by the chip's debug view and the u2 rail reader.
struct NetWorkerStats {
    std::atomic<uint64_t> tcp_tx_bytes{0};
    std::atomic<uint64_t> tcp_rx_bytes{0};
    std::atomic<uint64_t> udp_tx{0};
    std::atomic<uint64_t> udp_rx{0};
    std::atomic<uint64_t> raw_tx{0};
    std::atomic<uint64_t> raw_rx{0};
    std::atomic<uint64_t> raw_rx_dropped{0};   // slirp frames with no room in the event ring
    std::atomic<uint64_t> event_ring_full{0};
    std::atomic<uint64_t> request_ring_full{0};
};

class NetWorker {
public:
    NetWorker();
    ~NetWorker();

    NetWorker(const NetWorker &) = delete;
    NetWorker &operator=(const NetWorker &) = delete;

    bool start();
    void stop();

    /** Non-blocking: post work for the worker thread. */
    bool post(NetRequest req);
    /** Non-blocking: drain one event if available. */
    bool poll_event(NetEvent *out);

    /** TCP RX backpressure: only recv this many bytes from the host socket. */
    void set_rx_credit(uint8_t sock, int credit);
    void add_rx_credit(uint8_t sock, int n);

    void wake();

    // FORK: instrumentation.
    const NetWorkerStats &stats() const { return stats_; }
    /** The virtual network libslirp was given; valid once slirp_ready() is true. */
    bool slirp_ready() const { return slirp_ready_.load(std::memory_order_acquire); }
    const SlirpNetConfig &slirp_config() const { return slirp_cfg_; }

private:
    static int thread_main(void *userdata);
    void run();
    void handle_request(const NetRequest &req);
    void poll_sockets();
    void post_event(NetEvent ev);
    void post_status(uint8_t sock, uint8_t status, NetCause cause, int err = 0,
                     uint32_t peer_ip = 0, uint16_t peer_port = 0);
    void flush_tcp_tx(uint8_t sock);

#ifdef _WIN32
    using socket_t = SOCKET;
    static constexpr socket_t kInvalid = INVALID_SOCKET;
#else
    using socket_t = int;
    static constexpr socket_t kInvalid = -1;
#endif

    struct SockState {
        socket_t fd = kInvalid;
        uint8_t status = W5100_SN_SR_CLOSED;
        uint32_t gen = 0;                 // FORK: the chip generation this state belongs to
        bool is_tcp = false;
        bool is_udp = false;
        bool listening = false;
        bool eof = false;                 // FORK: peer closed; stop reading, keep fd until the guest closes
        std::vector<uint8_t> tx_pending;  // FORK: bytes a short ::send has not taken yet
    };

    void close_state(SockState &st);

    SDL_Thread *thread_ = nullptr;
    SDL_Semaphore *wake_ = nullptr;
    std::atomic<bool> running_{false};

    SpscRing<NetRequest, kRequestRingDepth> requests_;
    SpscRing<NetEvent, kEventRingDepth> events_;

    SockState socks_[U2_NUM_SOCKETS]{};
    std::atomic<int> rx_credit_[U2_NUM_SOCKETS]{};
    SlirpBackend slirp_;
    bool slirp_ok_ = false;
    SlirpNetConfig slirp_cfg_{};
    std::atomic<bool> slirp_ready_{false};
    NetWorkerStats stats_;
};

}  // namespace u2
