// LocalTalk-over-UDP bridge. Additive; default on, loopback/TTL 0 only.
// Never joins a real NIC. See Docs/LToUDP.md.

#include "ltoudp.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <chrono>
#include <deque>
#include <map>
#include <vector>
#include "NClock.hpp"

namespace nclock_hooks {
    uint64_t (*c14m_limit)() = nullptr;
    void (*c14m_wait)(uint64_t) = nullptr;
}

#ifdef __EMSCRIPTEN__
bool ltoudp_open() { return false; }
void ltoudp_close() {}
bool ltoudp_enabled() { return false; }
void ltoudp_send_llap(const uint8_t *, size_t) {}
void ltoudp_send_llap_guest(const uint8_t *, size_t, uint64_t, uint64_t, bool) {}
void ltoudp_send_time(uint8_t, uint64_t, uint64_t, uint64_t, uint64_t) {}
size_t ltoudp_recv_llap(uint8_t *, size_t, int) { return 0; }
int ltoudp_recv_pkt(ltoudp_pkt *out, int) { if (out) *out = {}; return LTOUDP_RECV_NONE; }
void ltoudp_test_inject_drop(int) {}
void ltoudp_test_reset_stats() {}
uint64_t ltoudp_test_gaps() { return 0; }
uint64_t ltoudp_test_nacks_tx() { return 0; }
uint64_t ltoudp_test_nacks_rx() { return 0; }
uint64_t ltoudp_test_resends() { return 0; }
bool ltoudp_test_remote_seq() { return false; }
uint16_t ltoudp_crc16_x25(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return (uint16_t)(crc ^ 0xFFFF);
}
#else

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#  define LTOUDP_CLOSE closesocket
#  define LTOUDP_IOCTL ioctlsocket
#  define LTOUDP_SOCKERR SOCKET_ERROR
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <errno.h>
   typedef int SOCKET;
#  define INVALID_SOCKET (-1)
#  define LTOUDP_CLOSE close
#  define LTOUDP_SOCKERR (-1)
#endif

namespace {

constexpr const char *kGroup = "239.192.76.84";
constexpr const char *kLoop  = "127.0.0.1";
constexpr int kPortDefault   = 1954;
constexpr size_t kMaxLlap    = 603;
constexpr size_t kLtg1Hdr    = 20;
constexpr size_t kSeqTagLen  = 4;
constexpr size_t kSeqWord    = 8;
constexpr size_t kMaxDgram   = 4 + kLtg1Hdr + kSeqTagLen + kSeqWord + kMaxLlap;
constexpr uint8_t kLtg1Magic[4] = { 'L', 'T', 'G', '1' };
constexpr uint8_t kSeqTag[4]    = { 'S', 'E', 'Q', '1' };
constexpr uint8_t kLapTime   = 0xC0;
constexpr int kRcvbufBytes   = 1 << 20;
constexpr size_t kSendWindow = 128;

void put_u64le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
uint64_t get_u64le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}
SOCKET g_sock = INVALID_SOCKET;
uint32_t g_sender = 0;
bool g_wsa = false;
int g_port = kPortDefault;

void udp_send(const uint8_t *buf, size_t n);
void put_sid(uint8_t *buf);

// Sequenced inbox (Docs/LToUDP.md Reliability). One reader, reorder hold,
// NACK/resend. Plain (non-LTG1) datagrams have seq=0 and skip this path.
uint64_t g_out_seq = 0;
bool g_remote_seq = false;
int g_inject_drop = 0;
uint64_t g_gaps = 0, g_nacks_tx = 0, g_nacks_rx = 0, g_resends = 0;
std::map<uint64_t, std::vector<uint8_t>> g_sent;
std::deque<ltoudp_pkt> g_inbox;
std::map<uint32_t, uint64_t> g_last_ok;
std::map<uint32_t, std::map<uint64_t, ltoudp_pkt>> g_hold;
std::map<uint32_t, uint64_t> g_nacked;
std::map<uint32_t, std::chrono::steady_clock::time_point> g_nack_at;

void mux_reset() {
    g_out_seq = 0;
    g_remote_seq = false;
    g_inject_drop = 0;
    g_gaps = g_nacks_tx = g_nacks_rx = g_resends = 0;
    g_sent.clear();
    g_inbox.clear();
    g_last_ok.clear();
    g_hold.clear();
    g_nacked.clear();
    g_nack_at.clear();
}
uint64_t mux_next_seq() { return ++g_out_seq; }
void mux_note_sent(uint64_t seq, const uint8_t *raw, size_t n) {
    g_sent[seq] = std::vector<uint8_t>(raw, raw + n);
    if (g_sent.size() > kSendWindow) {
        const uint64_t cutoff = (g_out_seq > kSendWindow) ? (g_out_seq - kSendWindow) : 0;
        for (auto it = g_sent.begin(); it != g_sent.end(); ) {
            if (it->first <= cutoff) it = g_sent.erase(it);
            else ++it;
        }
    }
}
void mux_resend_from(uint64_t seq) {
    for (auto &kv : g_sent) {
        if (kv.first >= seq) {
            udp_send(kv.second.data(), kv.second.size());
            g_resends++;
        }
    }
}
void mux_nack(uint64_t missing) {
    g_nacks_tx++;
    const uint64_t seq = mux_next_seq();
    uint8_t buf[4 + 3 + 4 + 1 + 48];
    put_sid(buf);
    buf[4] = 0; buf[5] = 0; buf[6] = kLapTime;
    std::memcpy(buf + 7, kLtg1Magic, 4);
    buf[11] = LTOUDP_TIME_NACK;
    put_u64le(buf + 12, missing);
    put_u64le(buf + 20, 0);
    put_u64le(buf + 28, 0);
    put_u64le(buf + 36, 0);
    put_u64le(buf + 44, 0);
    put_u64le(buf + 52, seq);
    mux_note_sent(seq, buf, sizeof(buf));
    udp_send(buf, sizeof(buf));
}
// pkt == nullptr accounts seq without delivering a guest-visible packet (NACK).
void mux_deliver(uint32_t sid, uint64_t seq, const ltoudp_pkt *pkt) {
    uint64_t last = g_last_ok.count(sid) ? g_last_ok[sid] : 0;
    if (seq <= last) return; // duplicate
    if (seq == last + 1) {
        g_last_ok[sid] = seq;
        if (pkt) g_inbox.push_back(*pkt);
        auto &held = g_hold[sid];
        while (held.count(g_last_ok[sid] + 1)) {
            const uint64_t nxt = g_last_ok[sid] + 1;
            g_last_ok[sid] = nxt;
            g_inbox.push_back(held[nxt]);
            held.erase(nxt);
        }
        auto ni = g_nacked.find(sid);
        if (ni != g_nacked.end() && g_last_ok[sid] >= ni->second) {
            g_nacked.erase(ni);
            g_nack_at.erase(sid);
        }
        return;
    }
    if (pkt) g_hold[sid][seq] = *pkt;
    const uint64_t missing = last ? (last + 1) : 1;
    g_gaps++;
    // One NACK per missing seq; re-NACK after 50 ms if the hole remains
    // (lost-NACK recovery). NACK-on-every-later-datagram is a send storm.
    const auto now = std::chrono::steady_clock::now();
    auto ni = g_nacked.find(sid);
    if (ni != g_nacked.end() && ni->second == missing) {
        auto ti = g_nack_at.find(sid);
        if (ti != g_nack_at.end()
            && now - ti->second < std::chrono::milliseconds(50))
            return;
    }
    g_nacked[sid] = missing;
    g_nack_at[sid] = now;
    mux_nack(missing);
}

void udp_send(const uint8_t *buf, size_t n) {
    if (g_sock == INVALID_SOCKET || !buf || n == 0) return;
    sockaddr_in dest {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, kGroup, &dest.sin_addr) != 1) return;
    sendto(g_sock, (const char *)buf, (int)n, 0, (sockaddr *)&dest, sizeof(dest));
}
void put_sid(uint8_t *buf) {
    buf[0] = (uint8_t)(g_sender);
    buf[1] = (uint8_t)(g_sender >> 8);
    buf[2] = (uint8_t)(g_sender >> 16);
    buf[3] = (uint8_t)(g_sender >> 24);
}


bool env_off(const char *e) {
    // 0 / no / false / off. "on" stays enabled.
    if (!e || !*e) return false;
    if (e[0] == '0' && e[1] == 0) return true;
    if ((e[0] == 'n' || e[0] == 'N') && (e[1] == 'o' || e[1] == 'O' || e[1] == 0)) return true;
    if (e[0] == 'f' || e[0] == 'F') return true;
    if ((e[0] == 'o' || e[0] == 'O') && (e[1] == 'f' || e[1] == 'F')) return true;
    return false;
}
bool env_disabled() {
    return env_off(std::getenv("A2GSPU_LTOUDP"));
}

int env_port() {
    const char *e = std::getenv("A2GSPU_LTOUDP_PORT");
    if (!e || !*e) return kPortDefault;
    int p = std::atoi(e);
    return (p > 0 && p < 65536) ? p : kPortDefault;
}

uint32_t make_sender() {
#if defined(_WIN32)
    LARGE_INTEGER q = {};
    QueryPerformanceCounter(&q);
    uint32_t id = (uint32_t)GetCurrentProcessId() ^ (uint32_t)q.LowPart ^ (uint32_t)q.HighPart;
#else
    uint32_t id = (uint32_t)getpid() ^ (uint32_t)std::time(nullptr) ^ (uint32_t)clock();
#endif
    return id ? id : 1u;
}

bool configure_socket(SOCKET s, const char *bind_addr) {
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    int rcv = kRcvbufBytes;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, (const char *)&rcv, sizeof(rcv));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, (const char *)&rcv, sizeof(rcv));

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)g_port);
    if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1) return false;
    if (bind(s, (sockaddr *)&addr, sizeof(addr)) != 0) return false;

    in_addr loop {};
    if (inet_pton(AF_INET, kLoop, &loop) != 1) return false;
    // Send and join only on loopback. TTL 0: must not leave the host.
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&loop, sizeof(loop)) != 0)
        return false;
    int ttl = 0;
    if (setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl)) != 0)
        return false;
    int loopback = 1;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&loopback, sizeof(loopback));

    ip_mreq mreq {};
    if (inet_pton(AF_INET, kGroup, &mreq.imr_multiaddr) != 1) return false;
    mreq.imr_interface = loop;
    if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) != 0)
        return false;

#if defined(_WIN32)
    u_long nb = 1;
    if (LTOUDP_IOCTL(s, FIONBIO, &nb) != 0) return false;
#else
    int fl = fcntl(s, F_GETFL, 0);
    if (fl < 0 || fcntl(s, F_SETFL, fl | O_NONBLOCK) < 0) return false;
#endif
    return true;
}

} // namespace

uint16_t ltoudp_crc16_x25(const uint8_t *p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0x8408) : (uint16_t)(crc >> 1);
    }
    return (uint16_t)(crc ^ 0xFFFF);
}

bool ltoudp_enabled() {
    return g_sock != INVALID_SOCKET;
}

bool ltoudp_open() {
    if (g_sock != INVALID_SOCKET) return true;
    if (env_disabled()) {
        std::fprintf(stderr, "LToUDP: disabled (A2GSPU_LTOUDP)\n");
        return false;
    }
    g_port = env_port();
    g_sender = make_sender();

#if defined(_WIN32)
    if (!g_wsa) {
        WSADATA wsa {};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            std::fprintf(stderr, "LToUDP: WSAStartup failed\n");
            return false;
        }
        g_wsa = true;
    }
#endif

    // Prefer a 127.0.0.1 bind (proven on this host). Fall back to INADDR_ANY
    // only if that bind fails, still joining and sending on 127.0.0.1 with TTL 0.
    const char *binds[] = { kLoop, "0.0.0.0" };
    const char *used = nullptr;
    for (const char *b : binds) {
        SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET) continue;
        if (configure_socket(s, b)) {
            g_sock = s;
            used = b;
            break;
        }
        LTOUDP_CLOSE(s);
    }
    if (g_sock == INVALID_SOCKET) {
        std::fprintf(stderr, "LToUDP: could not bind loopback/local port %d\n", g_port);
        return false;
    }
    mux_reset();
    std::fprintf(stderr,
        "LToUDP: enabled, bind %s:%d, group %s, IF=%s, TTL=0, sender=%08X\n",
        used, g_port, kGroup, kLoop, g_sender);
    return true;
}

void ltoudp_close() {
    if (g_sock != INVALID_SOCKET) {
        LTOUDP_CLOSE(g_sock);
        g_sock = INVALID_SOCKET;
    }
    mux_reset();
#if defined(_WIN32)
    if (g_wsa) {
        WSACleanup();
        g_wsa = false;
    }
#endif
}

void ltoudp_send_llap(const uint8_t *llap, size_t n) {
    ltoudp_send_llap_guest(llap, n, 0, 0, false);
}

void ltoudp_send_llap_guest(const uint8_t *llap, size_t n,
                            uint64_t now_14m, uint64_t due_14m, bool ext) {
    if (g_sock == INVALID_SOCKET || !llap || n < 3 || n > kMaxLlap) return;
    uint8_t buf[kMaxDgram];
    put_sid(buf);
    size_t off = 4;
    uint64_t seq = 0;
    if (ext) {
        std::memcpy(buf + off, kLtg1Magic, 4); off += 4;
        put_u64le(buf + off, now_14m); off += 8;
        put_u64le(buf + off, due_14m); off += 8;
        seq = mux_next_seq();
        if (g_remote_seq) {
            std::memcpy(buf + off, kSeqTag, 4); off += 4;
            put_u64le(buf + off, seq); off += 8;
        }
    }
    std::memcpy(buf + off, llap, n);
    const size_t total = off + n;
    if (seq) mux_note_sent(seq, buf, total);
    udp_send(buf, total);
}

void ltoudp_send_time(uint8_t cmd, uint64_t now_14m, uint64_t idle_14m,
                      uint64_t delivered_14m, uint64_t grant_14m,
                      uint64_t accounted_14m) {
    if (g_sock == INVALID_SOCKET) return;
    uint8_t buf[4 + 3 + 4 + 1 + 48];
    put_sid(buf);
    buf[4] = 0; buf[5] = 0; buf[6] = kLapTime;
    std::memcpy(buf + 7, kLtg1Magic, 4);
    buf[11] = cmd;
    put_u64le(buf + 12, now_14m);
    put_u64le(buf + 20, idle_14m);
    put_u64le(buf + 28, delivered_14m);
    put_u64le(buf + 36, grant_14m);
    put_u64le(buf + 44, accounted_14m);
    const uint64_t seq = mux_next_seq();
    put_u64le(buf + 52, seq);
    mux_note_sent(seq, buf, sizeof(buf));
    udp_send(buf, sizeof(buf));
}

static int parse_payload(const uint8_t *p, size_t ln, ltoudp_pkt *out) {
    *out = {};
    if (ln >= 3 + 4 + 1 + 16 && p[0] == 0 && p[1] == 0 && p[2] == kLapTime
        && std::memcmp(p + 3, kLtg1Magic, 4) == 0) {
        out->kind = LTOUDP_RECV_TIME;
        out->has_guest = true;
        out->time_cmd = p[7];
        out->now_14m = get_u64le(p + 8);
        out->idle_14m = get_u64le(p + 16);
        if (ln >= 3 + 4 + 1 + 24)
            out->delivered_14m = get_u64le(p + 24);
        else if (out->time_cmd == 2)
            out->delivered_14m = out->now_14m;
        if (ln >= 3 + 4 + 1 + 32)
            out->grant_14m = get_u64le(p + 32);
        if (ln >= 3 + 4 + 1 + 40)
            out->accounted_14m = get_u64le(p + 40);
        if (ln >= 3 + 4 + 1 + 48)
            out->seq = get_u64le(p + 48);
        return LTOUDP_RECV_TIME;
    }
    if (ln >= 20 + 4 + 8 + 3 && std::memcmp(p, kLtg1Magic, 4) == 0
        && std::memcmp(p + 20, kSeqTag, 4) == 0) {
        size_t body = ln - 32;
        if (body > sizeof(out->llap)) body = sizeof(out->llap);
        std::memcpy(out->llap, p + 32, body);
        out->n = body;
        out->has_guest = true;
        out->now_14m = get_u64le(p + 4);
        out->due_14m = get_u64le(p + 12);
        out->seq = get_u64le(p + 24);
        out->kind = LTOUDP_RECV_LLAP;
        return LTOUDP_RECV_LLAP;
    }
    if (ln >= 20 + 3 && std::memcmp(p, kLtg1Magic, 4) == 0) {
        size_t body = ln - 20;
        if (body > sizeof(out->llap)) body = sizeof(out->llap);
        std::memcpy(out->llap, p + 20, body);
        out->n = body;
        out->has_guest = true;
        out->now_14m = get_u64le(p + 4);
        out->due_14m = get_u64le(p + 12);
        out->kind = LTOUDP_RECV_LLAP;
        return LTOUDP_RECV_LLAP;
    }
    if (ln < 3) return LTOUDP_RECV_NONE;
    if (ln > sizeof(out->llap)) ln = sizeof(out->llap);
    if (p[0] == 0 && p[1] == 0 && p[2] == kLapTime) return LTOUDP_RECV_NONE;
    std::memcpy(out->llap, p, ln);
    out->n = ln;
    out->kind = LTOUDP_RECV_LLAP;
    return LTOUDP_RECV_LLAP;
}

static int recv_one(ltoudp_pkt *out, int timeout_ms) {
    if (g_sock == INVALID_SOCKET || !out) return LTOUDP_RECV_NONE;
    *out = {};
    const bool wait = timeout_ms > 0;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(wait ? timeout_ms : 0);
    for (;;) {
        if (wait) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return LTOUDP_RECV_NONE;
            const auto rem = std::chrono::duration_cast<std::chrono::microseconds>(deadline - now);
            if (rem.count() <= 0) return LTOUDP_RECV_NONE;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(g_sock, &rfds);
            timeval tv {};
            tv.tv_sec = (long)(rem.count() / 1000000);
            tv.tv_usec = (long)(rem.count() % 1000000);
            if (tv.tv_sec == 0 && tv.tv_usec <= 0) tv.tv_usec = 1;
#if defined(_WIN32)
            int nf = select(0, &rfds, nullptr, nullptr, &tv);
#else
            int nf = select((int)g_sock + 1, &rfds, nullptr, nullptr, &tv);
#endif
            if (nf == 0) return LTOUDP_RECV_NONE;
            if (nf < 0) return LTOUDP_RECV_NONE;
        }
        uint8_t buf[kMaxDgram];
        sockaddr_in from {};
#if defined(_WIN32)
        int flen = sizeof(from);
#else
        socklen_t flen = sizeof(from);
#endif
        int n = recvfrom(g_sock, (char *)buf, (int)sizeof(buf), 0, (sockaddr *)&from, &flen);
        if (n < 0) {
#if defined(_WIN32)
            const int err = WSAGetLastError();
            if (wait && (err == WSAEWOULDBLOCK || err == WSAEINTR)) continue;
#else
            if (wait && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) continue;
#endif
            return LTOUDP_RECV_NONE;
        }
        if (n < 7) {
            if (!wait) return LTOUDP_RECV_NONE;
            continue;
        }
        uint32_t sid = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
                       ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
        if (sid == g_sender) {
            if (!wait) return LTOUDP_RECV_NONE;
            continue;
        }
        if (g_inject_drop > 0) {
            g_inject_drop--;
            if (!wait) return LTOUDP_RECV_NONE;
            continue;
        }
        ltoudp_pkt pkt {};
        int kind = parse_payload(buf + 4, (size_t)n - 4, &pkt);
        if (kind == LTOUDP_RECV_NONE) {
            if (!wait) return LTOUDP_RECV_NONE;
            continue;
        }
        pkt.sid = sid;
        if (pkt.seq)
            g_remote_seq = true;
        if (kind == LTOUDP_RECV_TIME && pkt.time_cmd == LTOUDP_TIME_NACK) {
            const uint64_t missing = pkt.now_14m;
            g_nacks_rx++;
            if (missing) mux_resend_from(missing);
            if (pkt.seq) mux_deliver(sid, pkt.seq, nullptr);
            if (!g_inbox.empty()) {
                *out = g_inbox.front();
                g_inbox.pop_front();
                return out->kind;
            }
            if (!wait) return LTOUDP_RECV_NONE;
            continue;
        }
        if (pkt.seq)
            mux_deliver(sid, pkt.seq, &pkt);
        else
            g_inbox.push_back(pkt);
        if (!g_inbox.empty()) {
            *out = g_inbox.front();
            g_inbox.pop_front();
            return out->kind;
        }
        if (!wait) return LTOUDP_RECV_NONE;
    }
}

int ltoudp_recv_pkt(ltoudp_pkt *out, int timeout_ms) {
    if (!out) return LTOUDP_RECV_NONE;
    if (!g_inbox.empty()) {
        *out = g_inbox.front();
        g_inbox.pop_front();
        return out->kind;
    }
    return recv_one(out, timeout_ms);
}

void ltoudp_test_inject_drop(int n) { g_inject_drop = n > 0 ? n : 0; }
void ltoudp_test_reset_stats() {
    mux_reset();
}
uint64_t ltoudp_test_gaps() { return g_gaps; }
uint64_t ltoudp_test_nacks_tx() { return g_nacks_tx; }
uint64_t ltoudp_test_nacks_rx() { return g_nacks_rx; }
uint64_t ltoudp_test_resends() { return g_resends; }
bool ltoudp_test_remote_seq() { return g_remote_seq; }

size_t ltoudp_recv_llap(uint8_t *llap, size_t max, int timeout_ms) {
    if (!llap || max < 3) return 0;
    const bool wait = timeout_ms > 0;
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::milliseconds(wait ? timeout_ms : 0);
    for (;;) {
        int rem_ms = 0;
        if (wait) {
            const auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return 0;
            rem_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (rem_ms < 1) rem_ms = 1;
        }
        ltoudp_pkt pkt {};
        int kind = ltoudp_recv_pkt(&pkt, rem_ms);
        if (kind == LTOUDP_RECV_LLAP && pkt.n >= 3) {
            size_t ln = pkt.n > max ? max : pkt.n;
            std::memcpy(llap, pkt.llap, ln);
            return ln;
        }
        if (!wait) return 0;
        if (kind == LTOUDP_RECV_NONE) return 0;
    }
}


#endif // !EMSCRIPTEN
