/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   W5100 register/memory model for Uthernet II.
 *   Register semantics adapted from AppleWin Uthernet2.cpp (Andrea Odetti, GPL).
 *
 *   FORK: the fused model -- see w5100_chip.hpp and Docs/UPSTREAM_SYNC.md.
 */

#include "w5100_chip.hpp"
#include "u2_rail.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace u2 {
namespace {

uint8_t get_ibyte(uint16_t value, size_t shift) {
    return static_cast<uint8_t>((value >> shift) & 0xFF);
}

// Reported once per distinct thing, then silent: visible, never a flood.
bool first_time(uint64_t key) {
    static std::vector<uint64_t> seen;
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) return false;
    seen.push_back(key);
    return true;
}

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t kInvalidSock = INVALID_SOCKET;
int last_sock_error() { return WSAGetLastError(); }
void close_sock(socket_t s) { closesocket(s); }
bool nonblocking(socket_t fd) { u_long on = 1; return ioctlsocket(fd, FIONBIO, &on) == 0; }
#else
using socket_t = int;
constexpr socket_t kInvalidSock = -1;
int last_sock_error() { return errno; }
void close_sock(socket_t s) { close(s); }
bool nonblocking(socket_t fd) { const int cur = fcntl(fd, F_GETFL); return fcntl(fd, F_SETFL, cur | O_NONBLOCK) == 0; }
#endif

std::string sock_error_text(int err) {
    char buf[160];
#ifdef _WIN32
    if (err == WSAEADDRINUSE) return "address already in use";
    if (err == WSAEACCES) return "permission denied";
    snprintf(buf, sizeof buf, "host socket error %d", err);
#else
    snprintf(buf, sizeof buf, "%s (errno %d)", strerror(err), err);
#endif
    return buf;
}

std::string ip_text(const uint8_t *p) {
    char b[20];
    snprintf(b, sizeof b, "%u.%u.%u.%u", p[0], p[1], p[2], p[3]);
    return b;
}

}  // namespace

W5100Chip::W5100Chip(int slot) : slot_(slot) {
    memory_.assign(W5100_MEM_SIZE, 0);
#ifdef _WIN32
    // LISTEN binds on this thread (see listen_socket), which may run before the
    // worker thread has initialised Winsock. WSAStartup is reference-counted.
    WSADATA wsa{};
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
}

W5100Chip::~W5100Chip() {
    stop();
#ifdef _WIN32
    WSACleanup();
#endif
}

bool W5100Chip::start() {
    reset(true);
    return worker_.start();
}

void W5100Chip::stop() {
    worker_.stop();
}

void W5100Chip::close_host(size_t i, NetMsg how) {
    auto &s = sockets_[i];
    s.gen++;
    NetRequest req;
    req.msg = how;
    req.sock = static_cast<uint8_t>(i);
    req.gen = s.gen;
    worker_.post(std::move(req));
    worker_.set_rx_credit(static_cast<uint8_t>(i), 0);
    s.rx_pending.clear();
}

void W5100Chip::reset(bool power_cycle) {
    // FORK (OPEN question, Docs/UPSTREAM_SYNC.md): the fork's chip held MR bit 0 (IND,
    // indirect bus mode) set as hardwired on the Uthernet II; upstream clears MR on
    // reset. Kept set until card evidence settles it -- a2engine's netcheck asserts it.
    mode_ = W5100_MR_IND;
    if (power_cycle) {
        data_addr_ = 0;
    }

    memory_.assign(W5100_MEM_SIZE, 0);
    for (size_t i = 0; i < sockets_.size(); ++i) {
        const uint32_t gen = sockets_[i].gen;
        sockets_[i] = SocketState{};
        sockets_[i].gen = gen;   // generations only ever grow, across resets too
        close_host(i, NetMsg::CloseHost);
        sockets_[i].registerAddress = static_cast<uint16_t>(W5100_S0_BASE + (i << 8));
        const uint16_t ra = sockets_[i].registerAddress;
        memory_[ra + W5100_SN_DHAR0] = 0xFF;
        memory_[ra + W5100_SN_DHAR1] = 0xFF;
        memory_[ra + W5100_SN_DHAR2] = 0xFF;
        memory_[ra + W5100_SN_DHAR3] = 0xFF;
        memory_[ra + W5100_SN_DHAR4] = 0xFF;
        memory_[ra + W5100_SN_DHAR5] = 0xFF;
        memory_[ra + W5100_SN_TTL] = 0x80;
        sockets_[i].status = W5100_SN_SR_CLOSED;
    }
    memory_[W5100_RTR0] = 0x07;
    memory_[W5100_RTR1] = 0xD0;
    memory_[W5100_RCR] = 0x08;
    memory_[W5100_PTIMER] = 0x28;
    set_rx_sizes(0x55);
    set_tx_sizes(0x55);
    for (size_t i = 0; i < sockets_.size(); ++i) {
        reset_rxtx(i);
    }
}

void W5100Chip::set_tx_sizes(uint8_t value) {
    memory_[W5100_TMSR] = value;
    uint16_t base = W5100_TX_BASE;
    const uint16_t end = W5100_RX_BASE;
    for (auto &socket : sockets_) {
        socket.transmitBase = base;
        const uint8_t bits = value & 0x03;
        value >>= 2;
        const uint16_t size = static_cast<uint16_t>(1u << (10 + bits));
        base = static_cast<uint16_t>(base + size);
        if (base > end) {
            base = end;   // the 8K pool is exhausted: later sockets get what is left, possibly 0
        }
        socket.transmitSize = static_cast<uint16_t>(base - socket.transmitBase);
    }
}

void W5100Chip::set_rx_sizes(uint8_t value) {
    memory_[W5100_RMSR] = value;
    uint16_t base = W5100_RX_BASE;
    const uint16_t end = W5100_MEM_SIZE;
    for (auto &socket : sockets_) {
        socket.receiveBase = base;
        const uint8_t bits = value & 0x03;
        value >>= 2;
        const uint16_t size = static_cast<uint16_t>(1u << (10 + bits));
        base = static_cast<uint16_t>(base + size);
        if (base > end) {
            base = end;
        }
        socket.receiveSize = static_cast<uint16_t>(base - socket.receiveBase);
    }
}

void W5100Chip::reset_rxtx(size_t i) {
    auto &socket = sockets_[i];
    socket.sn_rx_wr = 0;
    socket.sn_rx_rsr = 0;
    memory_[socket.registerAddress + W5100_SN_TX_RD0] = 0;
    memory_[socket.registerAddress + W5100_SN_TX_RD1] = 0;
    memory_[socket.registerAddress + W5100_SN_TX_WR0] = 0;
    memory_[socket.registerAddress + W5100_SN_TX_WR1] = 0;
    memory_[socket.registerAddress + W5100_SN_RX_RD0] = 0;
    memory_[socket.registerAddress + W5100_SN_RX_RD1] = 0;
}

void W5100Chip::set_header_size(size_t i) {
    switch (sockets_[i].status) {
    case W5100_SN_SR_ESTABLISHED:
    case W5100_SN_SR_CLOSE_WAIT:
        sockets_[i].headerSize = 0;
        break;
    case W5100_SN_SR_SOCK_UDP:
        sockets_[i].headerSize = 8; // IP + Port + Size
        break;
    case W5100_SN_SR_SOCK_IPRAW:
        sockets_[i].headerSize = 6; // IP + Size
        break;
    case W5100_SN_SR_SOCK_MACRAW:
        sockets_[i].headerSize = 2;
        break;
    default:
        sockets_[i].headerSize = 0;
        break;
    }
}

void W5100Chip::update_rsr(size_t i) {
    auto &socket = sockets_[i];
    const int size = socket.receiveSize;
    if (size == 0) {
        socket.sn_rx_rsr = 0;
        return;
    }
    const uint16_t mask = static_cast<uint16_t>(size - 1);
    const int sn_rx_rd = read_be16(&memory_[socket.registerAddress + W5100_SN_RX_RD0]) & mask;
    const int sn_rx_wr = socket.sn_rx_wr & mask;
    int dataPresent = sn_rx_wr - sn_rx_rd;
    if (dataPresent < 0) {
        dataPresent += size;
    }
    socket.sn_rx_rsr = static_cast<uint16_t>(dataPresent);
}

uint16_t W5100Chip::tx_data_size(size_t i) const {
    const auto &socket = sockets_[i];
    const uint16_t size = socket.transmitSize;
    if (size == 0) {
        return 0;
    }
    const uint16_t mask = static_cast<uint16_t>(size - 1);
    const int sn_tx_rd = read_be16(&memory_[socket.registerAddress + W5100_SN_TX_RD0]) & mask;
    const int sn_tx_wr = read_be16(&memory_[socket.registerAddress + W5100_SN_TX_WR0]) & mask;
    int present = sn_tx_wr - sn_tx_rd;
    if (present < 0) {
        present += size;
    }
    return static_cast<uint16_t>(present);
}

uint8_t W5100Chip::tx_free_reg(size_t i, size_t shift) const {
    const int size = sockets_[i].transmitSize;
    const uint16_t free = static_cast<uint16_t>(size - tx_data_size(i));
    return get_ibyte(free, shift);
}

uint8_t W5100Chip::rx_size_reg(size_t i, size_t shift) const {
    return get_ibyte(sockets_[i].sn_rx_rsr, shift);
}

bool W5100Chip::room_for(size_t i, size_t len) const {
    const auto &s = sockets_[i];
    return s.receiveSize != 0 && s.sn_rx_rsr + len + s.headerSize < s.receiveSize;
}

size_t W5100Chip::rx_free(size_t i) const {
    const auto &s = sockets_[i];
    const unsigned used = static_cast<unsigned>(s.sn_rx_rsr) + s.headerSize;
    if (used + 1 >= s.receiveSize) {
        return 0;
    }
    return static_cast<size_t>(s.receiveSize - used - 1);
}

size_t W5100Chip::flush_tcp_pending(size_t i) {
    auto &s = sockets_[i];
    size_t moved = 0;
    while (!s.rx_pending.empty()) {
        const size_t room = rx_free(i);
        if (room == 0) {
            break;
        }
        const size_t n = std::min(room, s.rx_pending.size());
        write_rx_data(i, s.rx_pending.data(), n);
        s.rx_pending.erase(s.rx_pending.begin(), s.rx_pending.begin() + static_cast<std::ptrdiff_t>(n));
        moved += n;
    }
    return moved;
}

void W5100Chip::ingest_tcp(size_t i, const uint8_t *data, size_t len) {
    flush_tcp_pending(i);
    if (len == 0) {
        return;
    }
    const size_t room = rx_free(i);
    const size_t n = std::min(room, len);
    if (n > 0) {
        write_rx_data(i, data, n);
    }
    if (n < len) {
        auto &s = sockets_[i];
        s.rx_pending.insert(s.rx_pending.end(), data + n, data + len);
    }
}

void W5100Chip::write_rx_byte(size_t i, uint8_t v) {
    auto &socket = sockets_[i];
    if (socket.receiveSize == 0) {
        return;
    }
    const uint16_t base = socket.receiveBase;
    memory_[base + socket.sn_rx_wr] = v;
    socket.sn_rx_wr = static_cast<uint16_t>((socket.sn_rx_wr + 1) % socket.receiveSize);
    ++socket.sn_rx_rsr;
}

void W5100Chip::write_rx_be16(size_t i, uint16_t v) {
    write_rx_byte(i, get_ibyte(v, 8));
    write_rx_byte(i, get_ibyte(v, 0));
}

void W5100Chip::write_rx_data(size_t i, const uint8_t *data, size_t len) {
    for (size_t c = 0; c < len; ++c) {
        write_rx_byte(i, data[c]);
    }
}

void W5100Chip::raise_ir(size_t i, uint8_t bits) {
    if (bits) {
        memory_[sockets_[i].registerAddress + W5100_SN_IR] |= bits;
    }
}

void W5100Chip::frame_tick() {
    for (size_t i = 0; i < sockets_.size(); ++i) {
        if (!sockets_[i].rx_pending.empty() && flush_tcp_pending(i) > 0) {
            raise_ir(i, W5100_SN_IR_RECV);
        }
    }
    NetEvent ev;
    while (worker_.poll_event(&ev)) {
        apply_event(ev);
    }
}

void W5100Chip::apply_event(const NetEvent &ev) {
    if (ev.sock >= U2_NUM_SOCKETS) {
        return;
    }
    auto &s = sockets_[ev.sock];
    // Raw frames from slirp belong to no socket generation: ingest_macraw routes them
    // by the sockets' status now, so they are never stale.
    if (ev.evt != NetEvt::MacRawRx && ev.gen != s.gen) {
        // The guest closed or reopened this socket after the host produced the event.
        g_u2_counts.stale_events++;
        u2_record_drop(slot_, ev.sock, "stale-event", ev.payload.size());
        return;
    }
    switch (ev.evt) {
    case NetEvt::Status: {
        const uint8_t from = s.status;
        uint8_t ir = 0;
        const char *cause = "host";
        switch (ev.cause) {
        case NetCause::Connected:
            s.status = W5100_SN_SR_ESTABLISHED;
            ir = W5100_SN_IR_CON;
            cause = "connected";
            if (ev.src_ip != 0 || ev.src_port != 0) {
                // The peer: the address the guest connected to, or who connected in.
                std::memcpy(&memory_[s.registerAddress + W5100_SN_DIPR0], &ev.src_ip, 4);
                write_be16(&memory_[s.registerAddress + W5100_SN_DPORT0], ev.src_port);
            }
            break;
        case NetCause::RemoteClosed:
            s.status = W5100_SN_SR_CLOSE_WAIT;
            ir = W5100_SN_IR_DISCON;
            cause = "remote-closed";
            break;
        case NetCause::Reset:
            s.status = W5100_SN_SR_CLOSED;
            ir = W5100_SN_IR_DISCON;
            cause = "reset";
            worker_.set_rx_credit(ev.sock, 0);
            s.rx_pending.clear();
            break;
        case NetCause::Failed:
            s.status = W5100_SN_SR_CLOSED;
            ir = W5100_SN_IR_TIMEOUT;
            cause = "failed";
            worker_.set_rx_credit(ev.sock, 0);
            s.rx_pending.clear();
            if (first_time((uint64_t)0xF0000000ull | ((uint64_t)from << 8) | ev.sock)) {
                fprintf(stderr, "[Uthernet2] socket %u: the host refused the %s (%s); Sn_SR -> CLOSED, "
                                "Sn_IR TIMEOUT\n", (unsigned)ev.sock,
                        from == W5100_SN_SR_SOCK_SYNSENT ? "connection" : "socket",
                        sock_error_text(ev.host_err).c_str());
            }
            break;
        default:
            s.status = ev.status;
            break;
        }
        set_header_size(ev.sock);
        raise_ir(ev.sock, ir);
        u2_record_state(slot_, ev.sock, from, s.status, cause, ir, ev.host_err);
        break;
    }
    case NetEvt::RxTcp: {
        const uint16_t before = s.sn_rx_rsr;
        ingest_tcp(ev.sock, ev.payload.data(), ev.payload.size());
        if (s.sn_rx_rsr != before) {
            raise_ir(ev.sock, W5100_SN_IR_RECV);
        }
        break;
    }
    case NetEvt::RxUdp:
        if (room_for(ev.sock, ev.payload.size())) {
            // W5100 UDP header: source IP (4) + port (2) + size (2) then payload
            const uint8_t *ip = reinterpret_cast<const uint8_t *>(&ev.src_ip);
            write_rx_byte(ev.sock, ip[0]);
            write_rx_byte(ev.sock, ip[1]);
            write_rx_byte(ev.sock, ip[2]);
            write_rx_byte(ev.sock, ip[3]);
            write_rx_be16(ev.sock, ev.src_port);
            write_rx_be16(ev.sock, static_cast<uint16_t>(ev.payload.size()));
            write_rx_data(ev.sock, ev.payload.data(), ev.payload.size());
            raise_ir(ev.sock, W5100_SN_IR_RECV);
        } else {
            g_u2_counts.rx_dropped++;
            g_u2_counts.rx_dropped_bytes += ev.payload.size();
            u2_record_drop(slot_, ev.sock, "udp-no-room", ev.payload.size());
        }
        break;
    case NetEvt::MacRawRx:
        ingest_macraw(ev);
        break;
    case NetEvt::IpRawRx:
        if (room_for(ev.sock, ev.payload.size())) {
            const uint8_t *ip = reinterpret_cast<const uint8_t *>(&ev.src_ip);
            write_rx_byte(ev.sock, ip[0]);
            write_rx_byte(ev.sock, ip[1]);
            write_rx_byte(ev.sock, ip[2]);
            write_rx_byte(ev.sock, ip[3]);
            write_rx_be16(ev.sock, static_cast<uint16_t>(ev.payload.size()));
            write_rx_data(ev.sock, ev.payload.data(), ev.payload.size());
            raise_ir(ev.sock, W5100_SN_IR_RECV);
        } else {
            g_u2_counts.rx_dropped++;
            g_u2_counts.rx_dropped_bytes += ev.payload.size();
            u2_record_drop(slot_, ev.sock, "ipraw-no-room", ev.payload.size());
        }
        break;
    case NetEvt::SendDone:
        if (ev.cause == NetCause::Failed) {
            raise_ir(ev.sock, W5100_SN_IR_TIMEOUT);
            g_u2_counts.send_timeout++;
        } else {
            raise_ir(ev.sock, W5100_SN_IR_SEND_OK);
            g_u2_counts.send_ok++;
        }
        break;
    default:
        break;
    }
}

void W5100Chip::ingest_macraw(const NetEvent &ev) {
    if (ev.payload.size() < ETH_MINIMUM_SIZE) {
        return;
    }
    const uint8_t *frame = ev.payload.data();
    const uint8_t *mac = &memory_[W5100_SHAR0];
    const bool to_us = (frame[0] == mac[0] && frame[1] == mac[1] && frame[2] == mac[2] &&
                        frame[3] == mac[3] && frame[4] == mac[4] && frame[5] == mac[5]);
    const bool bcast = (frame[0] == 0xFF && frame[1] == 0xFF && frame[2] == 0xFF &&
                        frame[3] == 0xFF && frame[4] == 0xFF && frame[5] == 0xFF);

    // Prefer IPRAW sockets matching protocol if frame is IPv4
    if ((to_us || bcast) && frame[12] == 0x08 && frame[13] == 0x00 && ev.payload.size() >= 34) {
        const uint8_t proto = frame[23];
        const uint32_t src_ip = *reinterpret_cast<const uint32_t *>(frame + 26);
        const size_t ip_hdr = (frame[14] & 0x0F) * 4;
        if (14 + ip_hdr <= ev.payload.size()) {
            const uint8_t *payload = frame + 14 + ip_hdr;
            const size_t plen = ev.payload.size() - (14 + ip_hdr);
            for (size_t i = 0; i < sockets_.size(); ++i) {
                if (sockets_[i].status != W5100_SN_SR_SOCK_IPRAW) {
                    continue;
                }
                if (memory_[sockets_[i].registerAddress + W5100_SN_PROTO] != proto) {
                    continue;
                }
                if (!room_for(i, plen)) {
                    g_u2_counts.rx_dropped++;
                    g_u2_counts.rx_dropped_bytes += plen;
                    u2_record_drop(slot_, (int)i, "ipraw-no-room", plen);
                    break;
                }
                const uint8_t *ipb = reinterpret_cast<const uint8_t *>(&src_ip);
                write_rx_byte(i, ipb[0]);
                write_rx_byte(i, ipb[1]);
                write_rx_byte(i, ipb[2]);
                write_rx_byte(i, ipb[3]);
                write_rx_be16(i, static_cast<uint16_t>(plen));
                write_rx_data(i, payload, plen);
                raise_ir(i, W5100_SN_IR_RECV);
                return;
            }
        }
    }

    // MACRAW only on socket 0
    if (sockets_[0].status != W5100_SN_SR_SOCK_MACRAW) {
        return;
    }
    const uint8_t mr = memory_[sockets_[0].registerAddress + W5100_SN_MR];
    const bool filter = (mr & W5100_SN_MR_MF) != 0;
    if (filter && !to_us && !bcast) {
        return;
    }
    if (!room_for(0, ev.payload.size())) {
        g_u2_counts.rx_dropped++;
        g_u2_counts.rx_dropped_bytes += ev.payload.size();
        u2_record_drop(slot_, 0, "macraw-no-room", ev.payload.size());
        return;
    }
    // W5100 MACRAW header is big-endian length including the 2-byte header itself
    // (same as AppleWin writeDataMacRaw). Contiki subtracts 2 after reading it.
    write_rx_be16(0, static_cast<uint16_t>(ev.payload.size() + 2));
    write_rx_data(0, frame, ev.payload.size());
    raise_ir(0, W5100_SN_IR_RECV);
}

const char *W5100Chip::open_socket(size_t i) {
    auto &socket = sockets_[i];
    const uint8_t mr = memory_[socket.registerAddress + W5100_SN_MR];
    const uint8_t protocol = mr & W5100_SN_MR_PROTO_MASK;
    const char *result = "done";

    // A new generation, and any prior host socket dropped without a Status event.
    close_host(i, NetMsg::CloseHost);

    switch (protocol) {
    case W5100_SN_MR_IPRAW:
        socket.status = W5100_SN_SR_SOCK_IPRAW;
        break;
    case W5100_SN_MR_MACRAW:
        if (i == 0) {
            socket.status = W5100_SN_SR_SOCK_MACRAW;
        } else {
            // The chip only has MACRAW on socket 0. The OPEN fails in the register the
            // guest reads next, and the operator is told once why.
            socket.status = W5100_SN_SR_CLOSED;
            result = "refused";
            if (first_time(0xA0000000ull | i)) {
                fprintf(stderr, "[Uthernet2] socket %zu: MACRAW is socket 0 only (W5100 datasheet); "
                                "OPEN refused, Sn_SR left CLOSED.\n", i);
            }
        }
        break;
    case W5100_SN_MR_TCP: {
        NetRequest req;
        req.msg = NetMsg::OpenTcp;
        req.sock = static_cast<uint8_t>(i);
        req.gen = socket.gen;
        worker_.post(std::move(req));
        socket.status = W5100_SN_SR_SOCK_INIT;
        break;
    }
    case W5100_SN_MR_UDP: {
        NetRequest req;
        req.msg = NetMsg::OpenUdp;
        req.sock = static_cast<uint8_t>(i);
        req.gen = socket.gen;
        req.local_port = read_be16(&memory_[socket.registerAddress + W5100_SN_PORT0]);
        worker_.post(std::move(req));
        socket.status = W5100_SN_SR_SOCK_UDP;
        break;
    }
    case W5100_SN_MR_PPPOE:
        socket.status = W5100_SN_SR_CLOSED;
        result = "not-emulated";
        if (first_time(0xB0000005ull)) {
            fprintf(stderr, "[Uthernet2] socket %zu: Sn_MR protocol PPPoE ($05) is NOT EMULATED -- the OPEN "
                            "leaves the socket CLOSED and no PPPoE session exists.\n", i);
        }
        break;
    default:
        socket.status = W5100_SN_SR_CLOSED;
        if (protocol != W5100_SN_MR_CLOSED) {
            result = "refused";
        }
        break;
    }
    set_header_size(i);
    reset_rxtx(i);
    if (protocol == W5100_SN_MR_TCP) {
        const uint16_t sz = sockets_[i].receiveSize;
        worker_.set_rx_credit(static_cast<uint8_t>(i), sz > 0 ? static_cast<int>(sz - 1) : 0);
    } else {
        worker_.set_rx_credit(static_cast<uint8_t>(i), 0);
    }
    return result;
}

const char *W5100Chip::listen_socket(size_t i) {
    auto &socket = sockets_[i];
    const uint16_t port = read_be16(&memory_[socket.registerAddress + W5100_SN_PORT0]);

    // Bound HERE, synchronously, not on the worker: a LISTEN that silently failed
    // left the guest polling a socket that said LISTEN while nothing was bound.
    // A real chip owns its own stack and cannot fail to bind, so there is no
    // hardware behaviour to imitate for a failure: close, raise TIMEOUT, and say why.
    int err = 0;
    socket_t fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    bool ok = fd != kInvalidSock;
    if (ok) {
#ifdef _WIN32
        int one = 1;   // refuse to share a port something else holds
        setsockopt(fd, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&one), sizeof one);
#else
        int one = 1;   // allow a quick re-LISTEN over a TIME_WAIT
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port = htons(port);
        ok = ::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) == 0 &&
             ::listen(fd, 4) == 0 && nonblocking(fd);
    }
    if (!ok) {
        err = last_sock_error();
    }
    if (ok) {
        NetRequest req;
        req.msg = NetMsg::AdoptListen;
        req.sock = static_cast<uint8_t>(i);
        req.gen = socket.gen;
        req.fd = static_cast<intptr_t>(fd);
        if (worker_.post(std::move(req))) {
            socket.status = W5100_SN_SR_SOCK_LISTEN;
            return "done";
        }
        err = -1;   // the worker never received it; the fd is still ours to close
    }
    if (fd != kInvalidSock) {
        close_sock(fd);
    }
    fprintf(stderr, "[Uthernet2] socket %zu: LISTEN on port %u failed: %s -- Sn_SR -> CLOSED, Sn_IR TIMEOUT\n",
            i, (unsigned)port, err == -1 ? "the network worker's request ring is full" : sock_error_text(err).c_str());
    close_host(i, NetMsg::CloseHost);   // and the INIT socket the worker holds
    socket.status = W5100_SN_SR_CLOSED;
    set_header_size(i);
    raise_ir(i, W5100_SN_IR_TIMEOUT);
    return "refused";
}

bool W5100Chip::send_data(size_t i) {
    auto &socket = sockets_[i];
    const bool sendable = socket.status == W5100_SN_SR_ESTABLISHED || socket.status == W5100_SN_SR_CLOSE_WAIT ||
                          socket.status == W5100_SN_SR_SOCK_UDP || socket.status == W5100_SN_SR_SOCK_MACRAW ||
                          socket.status == W5100_SN_SR_SOCK_IPRAW;
    if (!sendable) {
        return false;
    }
    const uint16_t size = socket.transmitSize;
    if (size == 0) {
        return true;
    }
    const uint16_t mask = static_cast<uint16_t>(size - 1);
    const uint16_t sn_tx_rd = read_be16(&memory_[socket.registerAddress + W5100_SN_TX_RD0]) & mask;
    const uint16_t sn_tx_wr = read_be16(&memory_[socket.registerAddress + W5100_SN_TX_WR0]) & mask;
    int present = sn_tx_wr - sn_tx_rd;
    if (present < 0) {
        present += size;
    }
    if (present <= 0) {
        return true;
    }

    std::vector<uint8_t> packet(static_cast<size_t>(present));
    for (int c = 0; c < present; ++c) {
        packet[static_cast<size_t>(c)] =
            memory_[socket.transmitBase + ((sn_tx_rd + c) & mask)];
    }

    // Advance TX_RD to TX_WR (consumed)
    memory_[socket.registerAddress + W5100_SN_TX_RD0] = memory_[socket.registerAddress + W5100_SN_TX_WR0];
    memory_[socket.registerAddress + W5100_SN_TX_RD1] = memory_[socket.registerAddress + W5100_SN_TX_WR1];

    NetRequest req;
    req.sock = static_cast<uint8_t>(i);
    req.gen = socket.gen;
    req.payload = std::move(packet);

    switch (socket.status) {
    case W5100_SN_SR_ESTABLISHED:
    case W5100_SN_SR_CLOSE_WAIT:
        req.msg = NetMsg::SendTcp;
        worker_.post(std::move(req));
        break;
    case W5100_SN_SR_SOCK_UDP:
        req.msg = NetMsg::SendUdp;
        req.dest_ip = *reinterpret_cast<uint32_t *>(&memory_[socket.registerAddress + W5100_SN_DIPR0]);
        req.dest_port = read_be16(&memory_[socket.registerAddress + W5100_SN_DPORT0]);
        worker_.post(std::move(req));
        break;
    case W5100_SN_SR_SOCK_MACRAW:
        req.msg = NetMsg::MacRawTx;
        worker_.post(std::move(req));
        break;
    case W5100_SN_SR_SOCK_IPRAW: {
        // Build a minimal Ethernet+IPv4 frame for slirp (AppleWin sends via NetworkBackend).
        std::vector<uint8_t> frame;
        frame.resize(14 + 20 + req.payload.size());
        // Dest MAC broadcast; src from SHAR
        std::memset(frame.data(), 0xFF, 6);
        std::memcpy(frame.data() + 6, &memory_[W5100_SHAR0], 6);
        frame[12] = 0x08;
        frame[13] = 0x00;
        uint8_t *ip = frame.data() + 14;
        ip[0] = 0x45;
        ip[1] = 0;
        const uint16_t tot = static_cast<uint16_t>(20 + req.payload.size());
        ip[2] = static_cast<uint8_t>(tot >> 8);
        ip[3] = static_cast<uint8_t>(tot & 0xFF);
        ip[8] = memory_[socket.registerAddress + W5100_SN_TTL];
        ip[9] = memory_[socket.registerAddress + W5100_SN_PROTO];
        std::memcpy(ip + 12, &memory_[W5100_SIPR0], 4);
        std::memcpy(ip + 16, &memory_[socket.registerAddress + W5100_SN_DIPR0], 4);
        uint32_t sum = 0;
        for (int w = 0; w < 10; ++w) {
            sum += (ip[w * 2] << 8) | ip[w * 2 + 1];
        }
        while (sum >> 16) {
            sum = (sum & 0xFFFF) + (sum >> 16);
        }
        const uint16_t csum = static_cast<uint16_t>(~sum);
        ip[10] = static_cast<uint8_t>(csum >> 8);
        ip[11] = static_cast<uint8_t>(csum & 0xFF);
        std::memcpy(ip + 20, req.payload.data(), req.payload.size());
        req.msg = NetMsg::IpRawTx;
        req.payload = std::move(frame);
        worker_.post(std::move(req));
        break;
    }
    default:
        break;
    }
    return true;
}

void W5100Chip::handle_command(size_t i, uint8_t cmd) {
    auto &socket = sockets_[i];
    const uint16_t ra = socket.registerAddress;
    const uint8_t sr_before = socket.status;
    const uint8_t ir_before = memory_[ra + W5100_SN_IR];
    const uint8_t protocol = memory_[ra + W5100_SN_MR] & W5100_SN_MR_PROTO_MASK;
    const char *result = "done";

    switch (cmd) {
    case W5100_SN_CR_OPEN:
        result = open_socket(i);
        break;
    case W5100_SN_CR_LISTEN:
        // Valid only from SOCK_INIT on a TCP socket; the chip ignores it otherwise.
        if (socket.status == W5100_SN_SR_SOCK_INIT && protocol == W5100_SN_MR_TCP) {
            result = listen_socket(i);
        } else {
            result = "ignored-state";
        }
        break;
    case W5100_SN_CR_CONNECT:
        if (socket.status == W5100_SN_SR_SOCK_INIT && protocol == W5100_SN_MR_TCP) {
            NetRequest req;
            req.msg = NetMsg::Connect;
            req.sock = static_cast<uint8_t>(i);
            req.gen = socket.gen;
            req.dest_ip = *reinterpret_cast<uint32_t *>(&memory_[ra + W5100_SN_DIPR0]);
            req.dest_port = read_be16(&memory_[ra + W5100_SN_DPORT0]);
            worker_.post(std::move(req));
            socket.status = W5100_SN_SR_SOCK_SYNSENT;
        } else {
            result = "ignored-state";
        }
        break;
    case W5100_SN_CR_DISCON:
        // A graceful close: valid on a connection. The disconnect completes at once
        // here (the host owns the TCP teardown) and reports DISCON like the chip does
        // when its FIN exchange finishes.
        if (socket.status == W5100_SN_SR_ESTABLISHED || socket.status == W5100_SN_SR_CLOSE_WAIT) {
            close_host(i, NetMsg::Close);
            socket.status = W5100_SN_SR_CLOSED;
            set_header_size(i);
            raise_ir(i, W5100_SN_IR_DISCON);
        } else {
            result = "ignored-state";
        }
        break;
    case W5100_SN_CR_CLOSE:
        // Unconditional; no interrupt.
        close_host(i, NetMsg::Close);
        socket.status = W5100_SN_SR_CLOSED;
        set_header_size(i);
        break;
    case W5100_SN_CR_SEND:
        if (!send_data(i)) {
            result = "ignored-state";
        }
        break;
    case W5100_SN_CR_SEND_KEEP:
        // TCP keepalive. The host owns the connection, so no packet is emitted from
        // here (a NAT binding is NOT refreshed -- recorded as a limitation); what the
        // guest can observe is reproduced: SEND_OK on a live connection.
        if (socket.status == W5100_SN_SR_ESTABLISHED && protocol == W5100_SN_MR_TCP) {
            raise_ir(i, W5100_SN_IR_SEND_OK);
        } else {
            result = "ignored-state";
        }
        break;
    case W5100_SN_CR_RECV: {
        const uint16_t old_rsr = sockets_[i].sn_rx_rsr;
        update_rsr(i);
        const uint16_t new_rsr = sockets_[i].sn_rx_rsr;
        if (old_rsr > new_rsr) {
            worker_.add_rx_credit(static_cast<uint8_t>(i), static_cast<int>(old_rsr - new_rsr));
        }
        if (flush_tcp_pending(i) > 0) {
            raise_ir(i, W5100_SN_IR_RECV);
        }
        break;
    }
    default: {
        // AN UNEMULATED COMMAND MUST NOT BE ACKNOWLEDGED SILENTLY. Sn_CR self-clears
        // either way -- which is exactly how the chip reports "accepted and done" --
        // so the guest cannot tell. The operator is told, once per command code.
        result = "not-emulated";
        if (first_time(0xC0000000ull | cmd)) {
            fprintf(stderr, "[Uthernet2] socket %zu: command $%02X %s is NOT EMULATED -- it did nothing, and "
                            "Sn_CR still self-clears, so the guest cannot tell. Treat any behaviour that "
                            "depends on it as unimplemented, not as working.\n",
                    i, cmd, cmd == W5100_SN_CR_SEND_MAC ? "SEND_MAC (MACRAW send with Sn_DHAR)" : u2_cmd_name(cmd));
        }
        break;
    }
    }

    if (!strcmp(result, "ignored-state")) g_u2_counts.cmds_ignored++;
    else if (!strcmp(result, "refused")) g_u2_counts.cmds_refused++;
    else if (!strcmp(result, "not-emulated")) g_u2_counts.cmds_not_emulated++;
    u2_record_cmd(slot_, (int)i, cmd, sr_before, socket.status,
                  (uint8_t)(memory_[ra + W5100_SN_IR] & ~ir_before), result);
}

uint8_t W5100Chip::read_socket_reg(uint16_t address) const {
    const uint16_t i = (address >> 8) - 0x04;
    const uint16_t loc = address & 0xFF;
    switch (loc) {
    case W5100_SN_SR:
        return sockets_[i].status;
    case W5100_SN_TX_FSR0:
        return tx_free_reg(i, 8);
    case W5100_SN_TX_FSR1:
        return tx_free_reg(i, 0);
    case W5100_SN_RX_RSR0:
        return rx_size_reg(i, 8);
    case W5100_SN_RX_RSR1:
        return rx_size_reg(i, 0);
    default:
        return memory_[address];
    }
}

void W5100Chip::write_socket_reg(uint16_t address, uint8_t value) {
    const uint16_t i = (address >> 8) - 0x04;
    const uint16_t loc = address & 0xFF;
    switch (loc) {
    case W5100_SN_CR:
        if (value != 0) {
            handle_command(i, value);
        }
        memory_[address] = 0; // CR clears after command
        break;
    case W5100_SN_IR:
        memory_[address] &= static_cast<uint8_t>(~value);   // write 1 to clear
        break;
    case W5100_SN_SR:
    case W5100_SN_TX_FSR0:
    case W5100_SN_TX_FSR1:
    case W5100_SN_RX_RSR0:
    case W5100_SN_RX_RSR1:
        break;   // read-only
    default:
        memory_[address] = value;
        break;
    }
}

uint8_t W5100Chip::read_at(uint16_t address) const {
    if (address == W5100_MR) {
        return mode_;
    }
    if (address == W5100_IR) {
        // Bits 3-0 mirror "Sn_IR != 0" for each socket; derived, so clearing Sn_IR
        // clears the mirror exactly as the chip does.
        uint8_t ir = memory_[W5100_IR] & 0xE0;
        for (size_t i = 0; i < sockets_.size(); ++i) {
            if (memory_[sockets_[i].registerAddress + W5100_SN_IR] != 0) {
                ir |= static_cast<uint8_t>(1u << i);
            }
        }
        return ir;
    }
    if (address >= W5100_GAR0 && address <= W5100_UPORT1) {
        return memory_[address];
    }
    if (address >= W5100_S0_BASE && address <= W5100_S3_MAX) {
        return read_socket_reg(address);
    }
    if (address >= W5100_TX_BASE && address <= W5100_MEM_MAX) {
        return memory_[address];
    }
    return memory_[address & W5100_MEM_MAX];
}

uint8_t W5100Chip::peek(uint16_t address) const {
    return read_at(static_cast<uint16_t>(address & W5100_MEM_MAX));
}

void W5100Chip::write_mode(uint8_t value) {
    if (value & W5100_MR_RST) {
        reset(false);   // self-clearing: deliberately not stored
        return;
    }
    if ((value & W5100_MR_PPOE) && first_time(0xD0000008ull)) {
        fprintf(stderr, "[Uthernet2] MR bit PPPOE ($08) is NOT EMULATED -- the bit is accepted and readable, "
                        "but no PPPoE session is established. Treat PPPoE as unimplemented.\n");
    }
    mode_ = static_cast<uint8_t>(value | W5100_MR_IND);   // IND held set: see reset()
}

void W5100Chip::write_at(uint16_t address, uint8_t value) {
    if (address == W5100_MR) {
        write_mode(value);
        return;
    }
    if (address == W5100_IR) {
        memory_[W5100_IR] &= static_cast<uint8_t>(~(value & 0xE0));   // socket bits clear via Sn_IR
        return;
    }
    if (address == W5100_RMSR) {
        set_rx_sizes(value);
        return;
    }
    if (address == W5100_TMSR) {
        set_tx_sizes(value);
        return;
    }
    if (address >= W5100_MR && address <= W5100_UPORT1) {
        memory_[address] = value;
        return;
    }
    if (address >= W5100_S0_BASE && address <= W5100_S3_MAX) {
        write_socket_reg(address, value);
        return;
    }
    if (address >= W5100_TX_BASE && address <= W5100_MEM_MAX) {
        memory_[address] = value;
    }
}

void W5100Chip::auto_increment() {
    if (mode_ & W5100_MR_AI) {
        ++data_addr_;
        if (data_addr_ == W5100_RX_BASE || data_addr_ == W5100_MEM_SIZE) {
            data_addr_ = static_cast<uint16_t>(data_addr_ - 0x2000);
        }
    }
}

uint8_t W5100Chip::read_value() {
    const uint8_t v = read_at(data_addr_);
    auto_increment();
    return v;
}

void W5100Chip::write_value(uint8_t value) {
    write_at(data_addr_, value);
    auto_increment();
}

uint8_t W5100Chip::io_read(uint8_t loc) {
    switch (loc & U2_C0X_MASK) {
    case U2_C0X_MODE_REGISTER:
        return mode_;
    case U2_C0X_ADDRESS_HIGH:
        return get_ibyte(data_addr_, 8);
    case U2_C0X_ADDRESS_LOW:
        return get_ibyte(data_addr_, 0);
    case U2_C0X_DATA_PORT:
        return read_value();
    default:
        return 0;
    }
}

void W5100Chip::io_write(uint8_t loc, uint8_t value) {
    switch (loc & U2_C0X_MASK) {
    case U2_C0X_MODE_REGISTER:
        write_mode(value);
        break;
    case U2_C0X_ADDRESS_HIGH:
        data_addr_ = static_cast<uint16_t>((value << 8) | (data_addr_ & 0x00FF));
        break;
    case U2_C0X_ADDRESS_LOW:
        data_addr_ = static_cast<uint16_t>((value << 0) | (data_addr_ & 0xFF00));
        break;
    case U2_C0X_DATA_PORT:
        write_value(value);
        break;
    default:
        break;
    }
}

void W5100Chip::describe(std::vector<std::string> &out) const {
    char b[256];
    snprintf(b, sizeof b, "Uthernet II (W5100) slot %d: MR=$%02X addr=$%04X IR=$%02X IMR=$%02X RMSR=$%02X TMSR=$%02X",
             slot_, mode_, data_addr_, read_at(W5100_IR), memory_[W5100_IMR], memory_[W5100_RMSR], memory_[W5100_TMSR]);
    out.push_back(b);
    snprintf(b, sizeof b, "  SHAR=%02X:%02X:%02X:%02X:%02X:%02X SIPR=%s GAR=%s SUBR=%s",
             memory_[W5100_SHAR0], memory_[W5100_SHAR0 + 1], memory_[W5100_SHAR0 + 2], memory_[W5100_SHAR0 + 3],
             memory_[W5100_SHAR0 + 4], memory_[W5100_SHAR0 + 5], ip_text(&memory_[W5100_SIPR0]).c_str(),
             ip_text(&memory_[W5100_GAR0]).c_str(), ip_text(&memory_[W5100_SUBR0]).c_str());
    out.push_back(b);
    const NetWorkerStats &st = worker_.stats();
    if (worker_.slirp_ready()) {
        const SlirpNetConfig &c = worker_.slirp_config();
        snprintf(b, sizeof b, "  host: slirp network %s gateway %s dns %s",
                 ip_text(reinterpret_cast<const uint8_t *>(&c.vnetwork.s_addr)).c_str(),
                 ip_text(reinterpret_cast<const uint8_t *>(&c.vhost.s_addr)).c_str(),
                 ip_text(reinterpret_cast<const uint8_t *>(&c.vnameserver.s_addr)).c_str());
    } else {
        snprintf(b, sizeof b, "  host: slirp not up yet");
    }
    out.push_back(b);
    snprintf(b, sizeof b, "  host counters: tcp_tx=%llu tcp_rx=%llu udp_tx=%llu udp_rx=%llu raw_tx=%llu raw_rx=%llu "
                          "raw_rx_dropped=%llu event_ring_full=%llu request_ring_full=%llu",
             (unsigned long long)st.tcp_tx_bytes.load(), (unsigned long long)st.tcp_rx_bytes.load(),
             (unsigned long long)st.udp_tx.load(), (unsigned long long)st.udp_rx.load(),
             (unsigned long long)st.raw_tx.load(), (unsigned long long)st.raw_rx.load(),
             (unsigned long long)st.raw_rx_dropped.load(), (unsigned long long)st.event_ring_full.load(),
             (unsigned long long)st.request_ring_full.load());
    out.push_back(b);
    for (size_t i = 0; i < sockets_.size(); ++i) {
        const auto &s = sockets_[i];
        const uint16_t ra = s.registerAddress;
        snprintf(b, sizeof b,
                 "  S%zu MR=$%02X SR=$%02X(%s) IR=$%02X PORT=%u peer=%s:%u TX rd=$%04X wr=$%04X free=%u size=%u "
                 "RX rd=$%04X wr=$%04X rsr=%u size=%u pending=%zu gen=%u",
                 i, memory_[ra + W5100_SN_MR], s.status, u2_status_name(s.status), memory_[ra + W5100_SN_IR],
                 (unsigned)read_be16(&memory_[ra + W5100_SN_PORT0]), ip_text(&memory_[ra + W5100_SN_DIPR0]).c_str(),
                 (unsigned)read_be16(&memory_[ra + W5100_SN_DPORT0]),
                 (unsigned)read_be16(&memory_[ra + W5100_SN_TX_RD0]), (unsigned)read_be16(&memory_[ra + W5100_SN_TX_WR0]),
                 (unsigned)(s.transmitSize - tx_data_size(i)), (unsigned)s.transmitSize,
                 (unsigned)read_be16(&memory_[ra + W5100_SN_RX_RD0]), (unsigned)s.sn_rx_wr, (unsigned)s.sn_rx_rsr,
                 (unsigned)s.receiveSize, s.rx_pending.size(), (unsigned)s.gen);
        out.push_back(b);
    }
}

// ---- A2GSPU snapshot (M5) ---------------------------------------------------
// The chip's 32K memory, mode and address latch, and each socket's ring state and
// generation. A host connection cannot be saved: on restore a socket that was
// connected (or connecting) comes back CLOSED with DISCON raised, a listener or an
// INIT/UDP socket is re-opened on the host, and the count of lost connections is
// returned so the restore can say so.
void W5100Chip::A2GSPU_snapshot(FILE *f) const {
    uint8_t hdr[4] = { static_cast<uint8_t>(slot_), mode_, get_ibyte(data_addr_, 8), get_ibyte(data_addr_, 0) };
    fwrite(hdr, 1, 4, f);
    fwrite(memory_.data(), 1, W5100_MEM_SIZE, f);
    for (const auto &s : sockets_) {
        uint16_t w[2] = { s.sn_rx_wr, s.sn_rx_rsr };
        fwrite(w, 2, 2, f);
        uint8_t b[2] = { s.status, s.headerSize };
        fwrite(b, 1, 2, f);
        uint32_t g = s.gen, plen = static_cast<uint32_t>(s.rx_pending.size());
        fwrite(&g, 4, 1, f);
        fwrite(&plen, 4, 1, f);
        if (plen) fwrite(s.rx_pending.data(), 1, plen, f);
    }
}

bool W5100Chip::A2GSPU_restore(FILE *f, int *lost_connections) {
    uint8_t hdr[4];
    if (fread(hdr, 1, 4, f) != 4) return false;
    std::vector<uint8_t> mem(W5100_MEM_SIZE);
    if (fread(mem.data(), 1, W5100_MEM_SIZE, f) != W5100_MEM_SIZE) return false;
    struct Saved { uint16_t wr, rsr; uint8_t status, header; uint32_t gen; std::vector<uint8_t> pending; };
    std::array<Saved, U2_NUM_SOCKETS> saved{};
    for (auto &sv : saved) {
        uint16_t w[2]; uint8_t b[2]; uint32_t g = 0, plen = 0;
        if (fread(w, 2, 2, f) != 2 || fread(b, 1, 2, f) != 2 || fread(&g, 4, 1, f) != 1 || fread(&plen, 4, 1, f) != 1) return false;
        if (plen > 0x100000) return false;
        sv.wr = w[0]; sv.rsr = w[1]; sv.status = b[0]; sv.header = b[1]; sv.gen = g;
        sv.pending.resize(plen);
        if (plen && fread(sv.pending.data(), 1, plen, f) != plen) return false;
    }

    mode_ = static_cast<uint8_t>(hdr[1] | W5100_MR_IND);
    data_addr_ = static_cast<uint16_t>((hdr[2] << 8) | hdr[3]);
    memory_ = std::move(mem);
    set_rx_sizes(memory_[W5100_RMSR]);
    set_tx_sizes(memory_[W5100_TMSR]);
    int lost = 0;
    for (size_t i = 0; i < sockets_.size(); ++i) {
        auto &s = sockets_[i];
        const Saved &sv = saved[i];
        s.gen = std::max(s.gen, sv.gen);
        close_host(i, NetMsg::CloseHost);   // whatever this machine's host side held, and a new generation
        s.sn_rx_wr = sv.wr;
        s.sn_rx_rsr = sv.rsr;
        s.status = sv.status;
        s.headerSize = sv.header;
        s.rx_pending = sv.pending;
        switch (sv.status) {
        case W5100_SN_SR_SOCK_SYNSENT:
        case W5100_SN_SR_ESTABLISHED:
        case W5100_SN_SR_CLOSE_WAIT:
            s.status = W5100_SN_SR_CLOSED;
            set_header_size(i);
            raise_ir(i, W5100_SN_IR_DISCON);
            s.rx_pending.clear();
            lost++;
            break;
        case W5100_SN_SR_SOCK_INIT: {
            NetRequest req;
            req.msg = NetMsg::OpenTcp;
            req.sock = static_cast<uint8_t>(i);
            req.gen = s.gen;
            worker_.post(std::move(req));
            break;
        }
        case W5100_SN_SR_SOCK_LISTEN:
            s.status = W5100_SN_SR_SOCK_INIT;
            listen_socket(i);   // CLOSED + TIMEOUT, reported, if the port is not free on this host
            break;
        case W5100_SN_SR_SOCK_UDP: {
            NetRequest req;
            req.msg = NetMsg::OpenUdp;
            req.sock = static_cast<uint8_t>(i);
            req.gen = s.gen;
            req.local_port = read_be16(&memory_[s.registerAddress + W5100_SN_PORT0]);
            worker_.post(std::move(req));
            break;
        }
        default:
            break;   // CLOSED, IPRAW, MACRAW: no host socket to re-create
        }
    }
    if (lost_connections) *lost_connections = lost;
    return true;
}

}  // namespace u2
