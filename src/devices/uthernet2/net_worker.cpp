/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   Network worker thread: host sockets + libslirp. Never called from emu thread.
 *
 *   FORK changes (Docs/UPSTREAM_SYNC.md, the fused Uthernet II):
 *   - every request and event carries the chip's per-socket generation, so an
 *     event from a socket the guest has since closed or reopened is discarded;
 *   - a TCP send that ::send takes only part of is queued and finished later,
 *     and SEND_OK is reported only when the whole buffer has left;
 *   - status events say WHY (connected, remote close, reset, failed) so the chip
 *     raises the interrupt a W5100 would, and an accepted connection reports its
 *     peer address;
 *   - a peer's orderly close is CLOSE_WAIT, not CLOSED, and the socket stays
 *     open until the guest closes it;
 *   - UDP and raw frames wait in the host stack when the event ring is full
 *     instead of being dropped, and what is still dropped is counted;
 *   - LISTEN is bound by the chip synchronously (so a failure is visible in the
 *     same register write) and handed here as an already-listening socket.
 */

#include "net_worker.hpp"

#include "slirp_netpick.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define sock_error() WSAGetLastError()
#define SOCK_EINPROGRESS WSAEINPROGRESS
#define SOCK_EWOULDBLOCK WSAEWOULDBLOCK
#define SOCK_EAGAIN WSAEWOULDBLOCK
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#define sock_error() errno
#define SOCK_EINPROGRESS EINPROGRESS
#define SOCK_EWOULDBLOCK EWOULDBLOCK
#define SOCK_EAGAIN EAGAIN
#endif

namespace u2 {
namespace {

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalid = INVALID_SOCKET;
void closesock(socket_t s) { closesocket(s); }
bool set_nonblocking(socket_t fd) {
    u_long on = 1;
    return ioctlsocket(fd, FIONBIO, &on) == 0;
}
#else
using socket_t = int;
constexpr socket_t kInvalid = -1;
void closesock(socket_t s) { close(s); }
bool set_nonblocking(socket_t fd) {
    const int cur = fcntl(fd, F_GETFL);
    return fcntl(fd, F_SETFL, cur | O_NONBLOCK) == 0;
}
#endif

bool would_block(int err) {
    return err == SOCK_EWOULDBLOCK || err == SOCK_EAGAIN || err == SOCK_EINPROGRESS;
}

}  // namespace

NetWorker::NetWorker() = default;

NetWorker::~NetWorker() {
    stop();
}

bool NetWorker::start() {
    if (running_.load()) {
        return true;
    }
    wake_ = SDL_CreateSemaphore(0);
    if (!wake_) {
        return false;
    }
    running_.store(true);
    thread_ = SDL_CreateThread(thread_main, "uthernet2", this);
    if (!thread_) {
        running_.store(false);
        SDL_DestroySemaphore(wake_);
        wake_ = nullptr;
        return false;
    }
    return true;
}

void NetWorker::stop() {
    if (!running_.load() && !thread_) {
        return;
    }
    running_.store(false);
    NetRequest req;
    req.msg = NetMsg::Shutdown;
    requests_.send(std::move(req));
    if (wake_) {
        SDL_SignalSemaphore(wake_);
    }
    if (thread_) {
        SDL_WaitThread(thread_, nullptr);
        thread_ = nullptr;
    }
    if (wake_) {
        SDL_DestroySemaphore(wake_);
        wake_ = nullptr;
    }
}

bool NetWorker::post(NetRequest req) {
    if (!requests_.send(std::move(req))) {
        const uint64_t n = ++stats_.request_ring_full;
        if (n == 1 || (n & (n - 1)) == 0) {   // 1, 2, 4, 8 ... -- visible, never a flood
            fprintf(stderr, "[Uthernet2] request ring full: a host request was DROPPED (%llu so far)\n",
                    (unsigned long long)n);
        }
        return false;
    }
    wake();
    return true;
}

bool NetWorker::poll_event(NetEvent *out) {
    return events_.get(out);
}

void NetWorker::wake() {
    if (wake_) {
        SDL_SignalSemaphore(wake_);
    }
}

void NetWorker::set_rx_credit(uint8_t sock, int credit) {
    if (sock < U2_NUM_SOCKETS) {
        rx_credit_[sock].store(credit < 0 ? 0 : credit, std::memory_order_release);
    }
}

void NetWorker::add_rx_credit(uint8_t sock, int n) {
    if (sock < U2_NUM_SOCKETS && n > 0) {
        rx_credit_[sock].fetch_add(n, std::memory_order_acq_rel);
        wake();
    }
}

int NetWorker::thread_main(void *userdata) {
    static_cast<NetWorker *>(userdata)->run();
    return 0;
}

void NetWorker::post_event(NetEvent ev) {
    if (!events_.send(std::move(ev))) {
        const uint64_t n = ++stats_.event_ring_full;
        if (n == 1 || (n & (n - 1)) == 0) {
            fprintf(stderr, "[Uthernet2] event ring full: a network event was DROPPED (%llu so far)\n",
                    (unsigned long long)n);
        }
    }
}

void NetWorker::post_status(uint8_t sock, uint8_t status, NetCause cause, int err,
                            uint32_t peer_ip, uint16_t peer_port) {
    NetEvent ev;
    ev.evt = NetEvt::Status;
    ev.sock = sock;
    ev.status = status;
    ev.gen = socks_[sock].gen;
    ev.cause = cause;
    ev.host_err = err;
    ev.src_ip = peer_ip;
    ev.src_port = peer_port;
    post_event(std::move(ev));
}

void NetWorker::close_state(SockState &st) {
    if (st.fd != kInvalid) {
        closesock(st.fd);
        st.fd = kInvalid;
    }
    st.is_tcp = false;
    st.is_udp = false;
    st.listening = false;
    st.eof = false;
    st.tx_pending.clear();
    st.status = W5100_SN_SR_CLOSED;
}

void NetWorker::run() {
#ifdef _WIN32
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    const SlirpNetConfig cfg = pick_slirp_network();
    slirp_ok_ = slirp_.init(cfg);
    slirp_cfg_ = cfg;
    slirp_ready_.store(true, std::memory_order_release);
    {
        // FORK: name the virtual network once. The subnet is chosen per host (to
        // avoid the host's own networks), so it is a run input a guest with a static
        // IP depends on; a run that cannot say which one it had is a black box.
        char net[INET_ADDRSTRLEN] = "?", host[INET_ADDRSTRLEN] = "?", dns[INET_ADDRSTRLEN] = "?", dhcp[INET_ADDRSTRLEN] = "?";
        inet_ntop(AF_INET, &cfg.vnetwork, net, sizeof net);
        inet_ntop(AF_INET, &cfg.vhost, host, sizeof host);
        inet_ntop(AF_INET, &cfg.vnameserver, dns, sizeof dns);
        inet_ntop(AF_INET, &cfg.vdhcp_start, dhcp, sizeof dhcp);
        printf("Uthernet II: slirp %s network %s/24 gateway %s dns %s dhcp-start %s\n",
               slirp_ok_ ? "up," : "FAILED to start;", net, host, dns, dhcp);
    }
    slirp_.set_rx_callback([this](const uint8_t *pkt, size_t len) {
        if (!events_.can_send()) {
            ++stats_.raw_rx_dropped;   // slirp cannot be paused; count what the ring could not take
            return;
        }
        ++stats_.raw_rx;
        NetEvent ev;
        ev.evt = NetEvt::MacRawRx;
        ev.sock = 0;
        ev.gen = socks_[0].gen;
        ev.payload.assign(pkt, pkt + len);
        post_event(std::move(ev));
    });

    while (running_.load(std::memory_order_acquire)) {
        // Wait briefly for wake or timeout to poll sockets/slirp.
        SDL_WaitSemaphoreTimeout(wake_, 5);

        NetRequest req;
        while (requests_.get(&req)) {
            if (req.msg == NetMsg::Shutdown) {
                goto done;
            }
            handle_request(req);
        }
        poll_sockets();
        if (slirp_ok_) {
            slirp_.poll(0);
        }
    }

done:
    for (auto &s : socks_) {
        close_state(s);
    }
    slirp_.shutdown();
#ifdef _WIN32
    WSACleanup();
#endif
}

void NetWorker::flush_tcp_tx(uint8_t sock) {
    SockState &st = socks_[sock];
    if (st.fd == kInvalid || st.tx_pending.empty()) {
        return;
    }
    while (!st.tx_pending.empty()) {
        const int n = ::send(st.fd, reinterpret_cast<const char *>(st.tx_pending.data()),
                             static_cast<int>(st.tx_pending.size()), 0);
        if (n > 0) {
            stats_.tcp_tx_bytes += static_cast<uint64_t>(n);
            st.tx_pending.erase(st.tx_pending.begin(), st.tx_pending.begin() + n);
            continue;
        }
        const int err = sock_error();
        if (n < 0 && would_block(err)) {
            return;   // the rest goes on a later poll
        }
        // The host refused the rest: the connection is gone.
        close_state(st);
        post_status(sock, W5100_SN_SR_CLOSED, NetCause::Reset, err);
        return;
    }
    NetEvent done;
    done.evt = NetEvt::SendDone;
    done.sock = sock;
    done.gen = st.gen;
    post_event(std::move(done));
}

void NetWorker::handle_request(const NetRequest &req) {
    if (req.sock >= U2_NUM_SOCKETS) {
        return;
    }
    SockState &st = socks_[req.sock];

    // Opening and closing define a generation; everything else must belong to the
    // current one, or it is a leftover from a socket the guest already replaced.
    const bool defines_gen = req.msg == NetMsg::OpenTcp || req.msg == NetMsg::OpenUdp ||
                             req.msg == NetMsg::Close || req.msg == NetMsg::CloseHost ||
                             req.msg == NetMsg::AdoptListen;
    if (defines_gen) {
        st.gen = req.gen;
    } else if (req.gen != st.gen && req.msg != NetMsg::MacRawTx && req.msg != NetMsg::IpRawTx) {
        return;
    }

    switch (req.msg) {
    case NetMsg::OpenTcp: {
        close_state(st);
        socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (fd == kInvalid || !set_nonblocking(fd)) {
            const int err = sock_error();
            if (fd != kInvalid) {
                closesock(fd);
            }
            post_status(req.sock, W5100_SN_SR_CLOSED, NetCause::Failed, err);
            break;
        }
        st.fd = fd;
        st.is_tcp = true;
        st.status = W5100_SN_SR_SOCK_INIT;
        break;
    }
    case NetMsg::OpenUdp: {
        close_state(st);
        socket_t fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd == kInvalid || !set_nonblocking(fd)) {
            const int err = sock_error();
            if (fd != kInvalid) {
                closesock(fd);
            }
            post_status(req.sock, W5100_SN_SR_CLOSED, NetCause::Failed, err);
            break;
        }
        if (req.local_port != 0) {
            sockaddr_in bind_addr{};
            bind_addr.sin_family = AF_INET;
            bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
            bind_addr.sin_port = htons(req.local_port);
            if (::bind(fd, reinterpret_cast<sockaddr *>(&bind_addr), sizeof(bind_addr)) != 0) {
                const int err = sock_error();
                fprintf(stderr, "[Uthernet2] socket %u: UDP bind to port %u failed (host error %d); "
                                "Sn_SR -> CLOSED\n", (unsigned)req.sock, (unsigned)req.local_port, err);
                closesock(fd);
                post_status(req.sock, W5100_SN_SR_CLOSED, NetCause::Failed, err);
                break;
            }
        }
        st.fd = fd;
        st.is_udp = true;
        st.status = W5100_SN_SR_SOCK_UDP;
        break;
    }
    case NetMsg::AdoptListen: {
        close_state(st);
        st.fd = static_cast<socket_t>(req.fd);
        st.is_tcp = true;
        st.listening = true;
        st.status = W5100_SN_SR_SOCK_LISTEN;
        break;
    }
    case NetMsg::Connect: {
        if (st.fd == kInvalid || !st.is_tcp) {
            break;
        }
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = req.dest_ip;
        dest.sin_port = htons(req.dest_port);
        const int res = ::connect(st.fd, reinterpret_cast<sockaddr *>(&dest), sizeof(dest));
        if (res == 0) {
            st.status = W5100_SN_SR_ESTABLISHED;
            post_status(req.sock, st.status, NetCause::Connected, 0, req.dest_ip, req.dest_port);
        } else {
            const int err = sock_error();
            if (would_block(err)) {
                st.status = W5100_SN_SR_SOCK_SYNSENT;
            } else {
                close_state(st);
                post_status(req.sock, W5100_SN_SR_CLOSED, NetCause::Failed, err);
            }
        }
        break;
    }
    case NetMsg::Close:
    case NetMsg::CloseHost:
        // The chip has already set its own status; nothing is reported back.
        close_state(st);
        break;
    case NetMsg::SendTcp: {
        if (st.fd == kInvalid || !st.is_tcp || req.payload.empty()) {
            break;
        }
        st.tx_pending.insert(st.tx_pending.end(), req.payload.begin(), req.payload.end());
        flush_tcp_tx(req.sock);
        break;
    }
    case NetMsg::SendUdp: {
        if (st.fd == kInvalid || req.payload.empty()) {
            break;
        }
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = req.dest_ip;
        dest.sin_port = htons(req.dest_port);
        const int n = ::sendto(st.fd, reinterpret_cast<const char *>(req.payload.data()),
                               static_cast<int>(req.payload.size()), 0, reinterpret_cast<sockaddr *>(&dest),
                               sizeof(dest));
        NetEvent done;
        done.evt = NetEvt::SendDone;
        done.sock = req.sock;
        done.gen = st.gen;
        if (n < 0) {
            done.cause = NetCause::Failed;   // a W5100 reports an undeliverable datagram as TIMEOUT
            done.host_err = sock_error();
        } else {
            ++stats_.udp_tx;
        }
        post_event(std::move(done));
        break;
    }
    case NetMsg::MacRawTx:
    case NetMsg::IpRawTx: {
        if (slirp_ok_ && !req.payload.empty()) {
            slirp_.input(req.payload.data(), req.payload.size());
            ++stats_.raw_tx;
        }
        NetEvent done;
        done.evt = NetEvt::SendDone;
        done.sock = req.sock;
        done.gen = req.gen;
        if (!slirp_ok_) {
            done.cause = NetCause::Failed;
        }
        post_event(std::move(done));
        break;
    }
    default:
        break;
    }
}

void NetWorker::poll_sockets() {
    for (uint8_t i = 0; i < U2_NUM_SOCKETS; ++i) {
        SockState &st = socks_[i];
        if (st.fd == kInvalid) {
            continue;
        }

        if (st.status == W5100_SN_SR_SOCK_SYNSENT) {
#ifdef _WIN32
            fd_set wfds, efds;
            FD_ZERO(&wfds);
            FD_ZERO(&efds);
            FD_SET(st.fd, &wfds);
            FD_SET(st.fd, &efds);
            timeval tv{0, 0};
            if (select(0, nullptr, &wfds, &efds, &tv) > 0) {
#else
            pollfd pfd{st.fd, POLLOUT, 0};
            if (::poll(&pfd, 1, 0) > 0) {
#endif
                int err = 0;
                socklen_t elen = sizeof(err);
                getsockopt(st.fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&err), &elen);
                if (err == 0) {
                    st.status = W5100_SN_SR_ESTABLISHED;
                    post_status(i, st.status, NetCause::Connected);
                } else {
                    close_state(st);
                    post_status(i, W5100_SN_SR_CLOSED, NetCause::Failed, err);
                }
            }
            continue;
        }

        if (st.listening && st.status == W5100_SN_SR_SOCK_LISTEN) {
            sockaddr_in peer{};
            socklen_t plen = sizeof(peer);
            socket_t client = ::accept(st.fd, reinterpret_cast<sockaddr *>(&peer), &plen);
            if (client != kInvalid) {
                set_nonblocking(client);
                closesock(st.fd);   // one W5100 socket carries one connection
                st.fd = client;
                st.listening = false;
                st.status = W5100_SN_SR_ESTABLISHED;
                post_status(i, st.status, NetCause::Connected, 0, peer.sin_addr.s_addr, ntohs(peer.sin_port));
            }
        }

        if (st.is_tcp && !st.tx_pending.empty()) {
            flush_tcp_tx(i);
            if (st.fd == kInvalid) {
                continue;
            }
        }

        if (st.status == W5100_SN_SR_ESTABLISHED && st.is_tcp && !st.eof) {
            if (!events_.can_send()) {
                continue;
            }
            const int cred = rx_credit_[i].load(std::memory_order_acquire);
            if (cred <= 0) {
                continue;
            }
            uint8_t buf[2048];
            const int want = cred < static_cast<int>(sizeof(buf)) ? cred : static_cast<int>(sizeof(buf));
            const int n = ::recv(st.fd, reinterpret_cast<char *>(buf), want, 0);
            if (n > 0) {
                stats_.tcp_rx_bytes += static_cast<uint64_t>(n);
                const int prev = rx_credit_[i].fetch_sub(n, std::memory_order_acq_rel);
                if (prev < n) {
                    rx_credit_[i].store(0, std::memory_order_release);
                }
                NetEvent ev;
                ev.evt = NetEvt::RxTcp;
                ev.sock = i;
                ev.gen = st.gen;
                ev.payload.assign(buf, buf + n);
                post_event(std::move(ev));
            } else if (n == 0) {
                // Orderly close from the peer: the W5100 reports CLOSE_WAIT and waits
                // for the guest to DISCON or CLOSE. Keep the socket until then.
                st.eof = true;
                st.status = W5100_SN_SR_CLOSE_WAIT;
                post_status(i, st.status, NetCause::RemoteClosed);
            } else {
                const int err = sock_error();
                if (!would_block(err)) {
                    close_state(st);
                    post_status(i, W5100_SN_SR_CLOSED, NetCause::Reset, err);
                }
            }
        }

        if (st.status == W5100_SN_SR_SOCK_UDP && st.is_udp) {
            if (!events_.can_send()) {
                continue;   // leave the datagram in the host stack until there is room
            }
            uint8_t buf[2048];
            sockaddr_in src{};
            socklen_t slen = sizeof(src);
            const int n = ::recvfrom(st.fd, reinterpret_cast<char *>(buf), sizeof(buf), 0,
                                     reinterpret_cast<sockaddr *>(&src), &slen);
            if (n > 0) {
                ++stats_.udp_rx;
                NetEvent ev;
                ev.evt = NetEvt::RxUdp;
                ev.sock = i;
                ev.gen = st.gen;
                ev.src_ip = src.sin_addr.s_addr;
                ev.src_port = ntohs(src.sin_port);
                ev.payload.assign(buf, buf + n);
                post_event(std::move(ev));
            }
        }
    }
}

}  // namespace u2
