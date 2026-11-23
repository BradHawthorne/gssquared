// sccltoudptest -- host regression for the guest-time LToUDP cable
// (Docs/LToUDP.md): solicited replies are collected with the guest
// frozen, then delivered at TX EOF + IFG (2864 C14M ticks, ~200 us),
// inside TIFGap. Injection at the WR3 RX-enable edge is not the
// solicited-reply path. EOF is still a special-condition IP; first-
// character mode is re-armed only by WR0 command 100 ($20).

#include "util/InterruptController.hpp"
#include "a2gspu_faketime.hpp"
#include <thread>
#include <atomic>
#include "devices/scc8530/Z85C30.hpp"
#include "devices/scc8530/ltoudp.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <fcntl.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif
#include <thread>

int debug_level = 0;

static int failures = 0;
static void check(bool ok, const std::string &what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
}

void iigs_line_activity_bind(void *) {}
void iigs_line_activity_unbind(void *) {}
void iigs_line_activity_pulse(bool) {}

static const uint8_t kSDLCMode = 0x20; // WR4: sync mode 10
static const uint8_t kDisRx    = 0xD0; // WR3: receiver off
static const uint8_t kEnbRx    = 0xDD; // WR3: EnbRxSlv+EnterHunt (RecvInit)
static const uint8_t kEnbRxInt = 0x08; // WR1: first char or special
static const uint8_t kMIE      = 0x08; // WR9 D3
static const uint8_t kTxEnb    = 0x68; // WR5: Tx enable + 8 bits
static const uint8_t kReEnbInt = 0x20; // WR0 cmd 100

struct Scc : Z85C30 {
    using Z85C30::Z85C30;
    using Z85C30::ltoudp_inbox;
    using Z85C30::ltoudp_due;
    using Z85C30::ltoudp_test_now;
    using Z85C30::LTOUDP_IFG_14M;
    using Z85C30::LTOUDP_BYTE_14M;
    using Z85C30::rx_fifo;
    using Z85C30::rx_first_armed;
    using Z85C30::ltoudp_advance_14m;
    using Z85C30::ltoudp_schedule_unsolicited;
    using Z85C30::ltoudp_schedule_due;
    using Z85C30::ltoudp_deliver_due;
    using Z85C30::ltoudp_collect;
    using Z85C30::ltoudp_on_tx_llap;
    using Z85C30::ltoudp_peer_ltg1;
    using Z85C30::ltoudp_last_collect_reason;
    using Z85C30::LTOUDP_LTG1_SAFETY_MS;
    using Z85C30::LTOUDP_IDG_14M;
    using Z85C30::ltoudp_rx_eof_14m;
    using Z85C30::ltoudp_lockstep;
    using Z85C30::ltoudp_grant_14m;
    using Z85C30::ltoudp_horizon_faults;
    using Z85C30::ltoudp_tx_eof_14m;
    using Z85C30::ltoudp_clamp_14m;
    using Z85C30::LTOUDP_TIFGAP1_14M;
};


// One LocalTalk character time at 230.4 kbps FM0 (~34.7 us).
static void clock_rx_bytes(Scc &scc, size_t n) {
    for (size_t i = 0; i < n; i++)
        scc.ltoudp_advance_14m(Scc::LTOUDP_BYTE_14M);
}

static uint8_t rr0(Scc &scc, scc_channel_t ch) {
    scc.writeCmd(ch, 0);
    return scc.readCmd(ch);
}

static uint8_t rr3(Scc &scc) {
    scc.writeCmd(SCC_CHANNEL_A, 3);
    return scc.readCmd(SCC_CHANNEL_A);
}

static void program_sdlc(Scc &scc, scc_channel_t ch, uint8_t wr6, uint8_t wr3) {
    scc.writeCmd(ch, 4); scc.writeCmd(ch, kSDLCMode);
    scc.writeCmd(ch, 6); scc.writeCmd(ch, wr6);
    scc.writeCmd(ch, 1); scc.writeCmd(ch, kEnbRxInt);
    scc.writeCmd(ch, 9); scc.writeCmd(ch, kMIE);
    scc.writeCmd(ch, 5); scc.writeCmd(ch, kTxEnb);
    scc.writeCmd(ch, 3); scc.writeCmd(ch, wr3);
}

// No event timer: each writeData completes the character immediately.
static void tx_llap(Scc &scc, scc_channel_t ch, const uint8_t *llap, size_t n) {
    for (size_t i = 0; i < n; i++)
        scc.writeData(ch, llap[i]);
}

static bool fifo_empty(Scc &scc, scc_channel_t ch) {
    return scc.rx_fifo[ch].count == 0 && (rr0(scc, ch) & 0x01) == 0;
}


#ifdef _WIN32
using peer_sock_t = SOCKET;
static const peer_sock_t kInvalidPeer = INVALID_SOCKET;
#else
using peer_sock_t = int;
static const peer_sock_t kInvalidPeer = -1;
#endif

static void put_u64le(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

// Send a datagram with a foreign sender id so ltoudp_recv keeps it.
static bool send_peer_payload(const uint8_t *payload, size_t n) {
    peer_sock_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == kInvalidPeer) return false;
    const int one = 1;
#ifdef _WIN32
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#else
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
    sockaddr_in loc {};
    loc.sin_family = AF_INET;
    loc.sin_port = htons(0);
    inet_pton(AF_INET, "127.0.0.1", &loc.sin_addr);
    if (bind(s, (sockaddr *)&loc, sizeof(loc)) != 0) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return false;
    }
    const uint8_t ttl = 0;
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl));
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&one, sizeof(one));
    sockaddr_in iface {};
    iface.sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &iface.sin_addr);
    setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&iface.sin_addr, sizeof(iface.sin_addr));
    uint8_t buf[512];
    if (n + 4 > sizeof(buf)) {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        return false;
    }
    // Foreign SID, not the test process's g_sender.
    buf[0] = 0x11; buf[1] = 0x22; buf[2] = 0x33; buf[3] = 0x44;
    std::memcpy(buf + 4, payload, n);
    sockaddr_in dest {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(19541);
    inet_pton(AF_INET, "239.192.76.84", &dest.sin_addr);
    const int sent = sendto(s, (const char *)buf, (int)(n + 4), 0,
                            (sockaddr *)&dest, sizeof(dest));
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
    return sent == (int)(n + 4);
}

static bool send_ltg1_llap(const uint8_t *llap, size_t n, uint64_t now, uint64_t due) {
    uint8_t body[20 + 256];
    if (n > 256) return false;
    std::memcpy(body, "LTG1", 4);
    put_u64le(body + 4, now);
    put_u64le(body + 12, due);
    std::memcpy(body + 20, llap, n);
    return send_peer_payload(body, 20 + n);
}

static bool send_ltg1_time(uint8_t cmd, uint64_t now, uint64_t idle) {
    uint8_t body[3 + 4 + 1 + 16];
    body[0] = 0; body[1] = 0; body[2] = 0xC0;
    std::memcpy(body + 3, "LTG1", 4);
    body[7] = cmd;
    put_u64le(body + 8, now);
    put_u64le(body + 16, idle);
    return send_peer_payload(body, sizeof(body));
}

static bool send_ltg1_time_grant(uint8_t cmd, uint64_t now, uint64_t idle,
                                 uint64_t delivered, uint64_t grant,
                                 uint64_t accounted = 0,
                                 bool with_accounted = true) {
    uint8_t body[3 + 4 + 1 + 40];
    body[0] = 0; body[1] = 0; body[2] = 0xC0;
    std::memcpy(body + 3, "LTG1", 4);
    body[7] = cmd;
    put_u64le(body + 8, now);
    put_u64le(body + 16, idle);
    put_u64le(body + 24, delivered);
    put_u64le(body + 32, grant);
    size_t n = 3 + 4 + 1 + 32;
    if (with_accounted) {
        put_u64le(body + 40, accounted);
        n += 8;
    }
    return send_peer_payload(body, n);
}

static uint64_t get_u64le(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

// Sequenced test peer: SEQ1 + time seq, one inbox, resend on NACK,
// NACK on a gap, optional outbound drop. Speaks the Reliability rule.
struct SeqPeer {
    peer_sock_t s = kInvalidPeer;
    uint32_t sid = 0x55667788u;
    uint64_t out_seq = 0;
    std::map<uint64_t, std::vector<uint8_t>> sent;
    uint64_t last_ok = 0;
    int drop_next_tx = 0;
    int drop_next_rx = 0;
    int resends = 0;
    int nacks_rx = 0;
    int nacks_tx = 0;
    std::vector<uint8_t> last_llap;

    bool open() {
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == kInvalidPeer) return false;
        const int one = 1;
#ifdef _WIN32
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof(one));
#else
        setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
        sockaddr_in loc {};
        loc.sin_family = AF_INET;
        // Must bind the group port or Windows will not deliver multicast
        // datagrams addressed to 239.192.76.84:19541.
        loc.sin_port = htons(19541);
        inet_pton(AF_INET, "127.0.0.1", &loc.sin_addr);
        if (bind(s, (sockaddr *)&loc, sizeof(loc)) != 0) { close_sock(); return false; }
        const uint8_t ttl = 0;
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl));
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (const char *)&one, sizeof(one));
        in_addr iface {};
        inet_pton(AF_INET, "127.0.0.1", &iface);
        setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&iface, sizeof(iface));
        ip_mreq mreq {};
        inet_pton(AF_INET, "239.192.76.84", &mreq.imr_multiaddr);
        mreq.imr_interface = iface;
        if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq)) != 0) {
            close_sock();
            return false;
        }
#ifdef _WIN32
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
#else
        int fl = fcntl(s, F_GETFL, 0);
        fcntl(s, F_SETFL, fl | O_NONBLOCK);
#endif
        return true;
    }
    void close_sock() {
        if (s == kInvalidPeer) return;
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        s = kInvalidPeer;
    }
    sockaddr_in dest() const {
        sockaddr_in d {};
        d.sin_family = AF_INET;
        d.sin_port = htons(19541);
        inet_pton(AF_INET, "239.192.76.84", &d.sin_addr);
        return d;
    }
    bool emit(std::vector<uint8_t> dgram, bool may_drop) {
        const uint64_t seq = out_seq;
        sent[seq] = dgram;
        if (may_drop && drop_next_tx > 0) {
            drop_next_tx--;
            return true;
        }
        auto d = dest();
        const int n = sendto(s, (const char *)dgram.data(), (int)dgram.size(), 0,
                             (sockaddr *)&d, sizeof(d));
        return n == (int)dgram.size();
    }
    void resend_from(uint64_t missing) {
        auto d = dest();
        for (auto &kv : sent) {
            if (kv.first >= missing) {
                sendto(s, (const char *)kv.second.data(), (int)kv.second.size(), 0,
                       (sockaddr *)&d, sizeof(d));
                resends++;
            }
        }
    }
    void send_nack(uint64_t missing) {
        nacks_tx++;
        out_seq++;
        std::vector<uint8_t> buf(4 + 3 + 4 + 1 + 48, 0);
        buf[0] = (uint8_t)(sid); buf[1] = (uint8_t)(sid >> 8);
        buf[2] = (uint8_t)(sid >> 16); buf[3] = (uint8_t)(sid >> 24);
        buf[6] = 0xC0;
        std::memcpy(buf.data() + 7, "LTG1", 4);
        buf[11] = LTOUDP_TIME_NACK;
        put_u64le(buf.data() + 12, missing);
        put_u64le(buf.data() + 52, out_seq);
        emit(std::move(buf), false);
    }
    bool send_time(uint8_t cmd, uint64_t now, uint64_t idle,
                   uint64_t delivered, uint64_t grant, uint64_t accounted) {
        out_seq++;
        std::vector<uint8_t> buf(4 + 3 + 4 + 1 + 48, 0);
        buf[0] = (uint8_t)(sid); buf[1] = (uint8_t)(sid >> 8);
        buf[2] = (uint8_t)(sid >> 16); buf[3] = (uint8_t)(sid >> 24);
        buf[6] = 0xC0;
        std::memcpy(buf.data() + 7, "LTG1", 4);
        buf[11] = cmd;
        put_u64le(buf.data() + 12, now);
        put_u64le(buf.data() + 20, idle);
        put_u64le(buf.data() + 28, delivered);
        put_u64le(buf.data() + 36, grant);
        put_u64le(buf.data() + 44, accounted);
        put_u64le(buf.data() + 52, out_seq);
        return emit(std::move(buf), true);
    }
    bool send_llap(const uint8_t *llap, size_t n, uint64_t now, uint64_t due) {
        out_seq++;
        std::vector<uint8_t> buf(4 + 20 + 4 + 8 + n);
        buf[0] = (uint8_t)(sid); buf[1] = (uint8_t)(sid >> 8);
        buf[2] = (uint8_t)(sid >> 16); buf[3] = (uint8_t)(sid >> 24);
        std::memcpy(buf.data() + 4, "LTG1", 4);
        put_u64le(buf.data() + 8, now);
        put_u64le(buf.data() + 16, due);
        std::memcpy(buf.data() + 24, "SEQ1", 4);
        put_u64le(buf.data() + 28, out_seq);
        std::memcpy(buf.data() + 36, llap, n);
        return emit(std::move(buf), true);
    }
    // Drain up to timeout_ms. Handle official NACK (resend) and official
    // sequenced frames (NACK a gap). Returns true if an LLAP was seen.
    bool pump(int timeout_ms) {
        bool saw_llap = false;
        const auto deadline = std::chrono::steady_clock::now()
            + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) break;
            int rem = (int)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
            if (rem < 1) rem = 1;
            fd_set rfds;
            FD_ZERO(&rfds);
            FD_SET(s, &rfds);
            timeval tv {};
            tv.tv_sec = rem / 1000;
            tv.tv_usec = (rem % 1000) * 1000;
#ifdef _WIN32
            int nsel = select(0, &rfds, nullptr, nullptr, &tv);
#else
            int nsel = select((int)s + 1, &rfds, nullptr, nullptr, &tv);
#endif
            if (nsel <= 0) continue;
            uint8_t buf[640];
            sockaddr_in from {};
            socklen_t flen = sizeof(from);
            int n = recvfrom(s, (char *)buf, (int)sizeof(buf), 0, (sockaddr *)&from, &flen);
            if (n < 7) continue;
            uint32_t rs = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8)
                | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
            if (rs == sid) continue;
            if (drop_next_rx > 0) {
                drop_next_rx--;
                continue;
            }
            const uint8_t *p = buf + 4;
            const int ln = n - 4;
            uint64_t seq = 0;
            bool is_nack = false;
            bool is_llap = false;
            if (ln >= 3 + 4 + 1 + 16 && p[0] == 0 && p[1] == 0 && p[2] == 0xC0
                && std::memcmp(p + 3, "LTG1", 4) == 0) {
                if (ln >= 3 + 4 + 1 + 48) seq = get_u64le(p + 48);
                if (p[7] == LTOUDP_TIME_NACK) {
                    is_nack = true;
                    nacks_rx++;
                    const uint64_t missing = get_u64le(p + 8);
                    if (missing) resend_from(missing);
                }
            } else if (ln >= 20 + 4 + 8 + 3 && std::memcmp(p, "LTG1", 4) == 0
                       && std::memcmp(p + 20, "SEQ1", 4) == 0) {
                seq = get_u64le(p + 24);
                size_t body = (size_t)ln - 32;
                last_llap.assign(p + 32, p + 32 + body);
                is_llap = true;
            } else if (ln >= 20 + 3 && std::memcmp(p, "LTG1", 4) == 0) {
                size_t body = (size_t)ln - 20;
                last_llap.assign(p + 20, p + 20 + body);
                is_llap = true;
            } else if (ln >= 3 && !(p[0] == 0 && p[1] == 0 && p[2] == 0xC0)) {
                last_llap.assign(p, p + ln);
                is_llap = true;
            }
            if (is_llap) saw_llap = true;
            if (seq) {
                if (seq <= last_ok) continue;
                if (seq == last_ok + 1) last_ok = seq;
                else if (!is_nack) {
                    const uint64_t missing = last_ok ? (last_ok + 1) : 1;
                    send_nack(missing);
                } else if (seq == last_ok + 1) {
                    last_ok = seq;
                }
            }
        }
        return saw_llap;
    }
};


int main() {
    _putenv_s("A2GSPU_LTOUDP_PORT", "19541");
    if (!ltoudp_open()) {
        std::fprintf(stderr, "FAIL: ltoudp_open on port 19541\n");
        return 1;
    }

    InterruptController irq;
    const uint8_t atp[] = { 0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
                            0x60, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00, 0x02 };
    const uint8_t cts[] = { 0x80, 0x01, 0x85 };
    const uint8_t enq[] = { 0x7F, 0x7F, 0x81 };

    // --- 1. Solicited CTS reply is invisible until TX EOF + 2864 ticks ---
    // fXmitLAP DisRx's, transmits CTS, RecvInit re-enables RX, RecvPacket
    // waits TIFGap. The ATP must not appear at the WR3-enable edge.
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
        scc.ltoudp_inbox.emplace_back(atp, atp + sizeof(atp));
        scc.ltoudp_on_tx_llap(cts, sizeof(cts));
        check(scc.ltoudp_due.size() == 1,
              "CTS reply is on the due queue after TX complete");
        check(!scc.ltoudp_due.empty() && scc.ltoudp_due[0].due_14m == Scc::LTOUDP_IFG_14M,
              "due time is TX EOF + 2864 C14M ticks");
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "FIFO empty immediately after CTS TX (not injected at host poll)");

        scc.writeCmd(SCC_CHANNEL_A, 3);
        scc.writeCmd(SCC_CHANNEL_A, kEnbRx); // RecvInit
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "WR3 RX-enable does not inject the solicited ATP");
        check(scc.ltoudp_due.size() == 1,
              "due queue still holds the ATP after RecvInit");

        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M - 1);
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "RR0 RxAvail is 0 at IFG-1");
        scc.ltoudp_advance_14m(1);
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "frame starts at IFG but first byte is still shifting");
        check(scc.ltoudp_due.empty(),
              "due queue consumed at IFG");
        clock_rx_bytes(scc, 1);
        check((rr0(scc, SCC_CHANNEL_A) & 0x01) != 0,
              "RR0 RxAvail set one LocalTalk byte after IFG");
        check(scc.rx_fifo[SCC_CHANNEL_A].count > 0,
              "FIFO holds the first ATP byte after one character time");
        check(scc.rx_fifo[SCC_CHANNEL_A].data[0] == 0x01,
              "FIFO head is dest $01");
        check(scc.rx_fifo[SCC_CHANNEL_A].count <= 3,
              "FIFO is at most 3 bytes deep");
    }

    // --- 2. Host timeout with no peer injects nothing (no-answer / free ENQ) ---
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x7F, kEnbRx);
        const auto t0 = std::chrono::steady_clock::now();
        scc.ltoudp_on_tx_llap(enq, sizeof(enq));
        const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        check(wait_ms >= 4,
              "no-answer collect waits ~5 ms of host time (own multicast loopback does not abort)");
        check(scc.ltoudp_due.empty(),
              "no-answer ENQ leaves the due queue empty");
        check(scc.ltoudp_inbox.empty(),
              "no-answer ENQ leaves the inbox empty");
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "no-answer ENQ does not inject a frame");
        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M);
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "advancing IFG after a no-answer still injects nothing");
    }

    // --- 3. RX-off at fire time loses the solicited frame ---
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
        scc.ltoudp_inbox.emplace_back(atp, atp + sizeof(atp));
        scc.ltoudp_on_tx_llap(cts, sizeof(cts));
        check(scc.ltoudp_due.size() == 1, "reply queued before RX-off fire");
        // Stay DisRx through the IFG, as if RecvInit never ran.
        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M);
        check(scc.ltoudp_due.empty(),
              "RX-off at fire time drops the due frame");
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "lost frame does not appear in the FIFO");
    }

    // --- 4. Unsolicited frame waits for an idle line, then one IFG ---
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
        const uint8_t first[] = { 0x01, 0x80, 0x84 };
        const uint8_t second[] = { 0x01, 0x80, 0x01, 0x00, 0x0d };
        check(scc.ltoudp_try_inject(SCC_CHANNEL_A, first, sizeof(first)),
              "first unsolicited frame occupies the line");
        scc.ltoudp_inbox.emplace_back(second, second + sizeof(second));
        scc.ltoudp_schedule_unsolicited();
        check(scc.ltoudp_due.empty(),
              "unsolicited second frame waits while the line is busy");
        check(scc.ltoudp_inbox.size() == 1,
              "second frame stays in the inbox until the line is idle");
        // Drain the first frame at 230.4 kbps (3 data + 2 FCS).
        for (int i = 0; i < 5; i++) {
            clock_rx_bytes(scc, 1);
            if (scc.rx_fifo[SCC_CHANNEL_A].count > 0)
                (void)scc.readData(SCC_CHANNEL_A);
        }
        check(fifo_empty(scc, SCC_CHANNEL_A), "first frame has left the FIFO");
        scc.ltoudp_schedule_unsolicited();
        check(scc.ltoudp_due.size() == 1,
              "second frame is scheduled one IFG after the line is idle");
        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M - 1);
        check((rr0(scc, SCC_CHANNEL_A) & 0x01) == 0,
              "second frame is not visible before its IFG");
        scc.ltoudp_advance_14m(1);
        check((rr0(scc, SCC_CHANNEL_A) & 0x01) == 0,
              "second frame starts at IFG, first byte still shifting");
        clock_rx_bytes(scc, 1);
        check((rr0(scc, SCC_CHANNEL_A) & 0x01) != 0,
              "second frame appears one LocalTalk byte after IFG on an idle line");
        check(scc.rx_fifo[SCC_CHANNEL_A].data[0] == 0x01,
              "second frame dest $01");
    }

    // --- 5. EOF is special; it does not re-arm first-character mode ---
    {
        Scc scc2(&irq, nullptr, nullptr);
        program_sdlc(scc2, SCC_CHANNEL_A, 0x01, kEnbRx);
        const uint8_t shortfrm[] = { 0x01, 0x80, 0x84 };
        check(scc2.ltoudp_try_inject(SCC_CHANNEL_A, shortfrm, sizeof(shortfrm)),
              "first short frame injects on an enabled SDLC channel");
        check(scc2.rx_fifo[SCC_CHANNEL_A].count == 0,
              "FIFO empty at inject; first byte is still on the wire");
        clock_rx_bytes(scc2, 1);
        (void)scc2.readData(SCC_CHANNEL_A);
        check(!scc2.rx_first_armed[SCC_CHANNEL_A],
              "first-character mode disarmed after reading the first byte");
        clock_rx_bytes(scc2, 1);
        (void)scc2.readData(SCC_CHANNEL_A);
        clock_rx_bytes(scc2, 1);
        (void)scc2.readData(SCC_CHANNEL_A);
        // 3 data + 2 FCS. Clock the two CRC bytes; RecvBytes LastByte leaves EOF.
        clock_rx_bytes(scc2, 1);
        (void)scc2.readData(SCC_CHANNEL_A); // first FCS
        clock_rx_bytes(scc2, 1);
        check(scc2.rx_fifo[SCC_CHANNEL_A].count == 1 && scc2.rx_fifo[SCC_CHANNEL_A].eof[0],
              "last FCS / EOF is at the FIFO head (RecvBytes LastByte leaves it)");
        check(!scc2.rx_first_armed[SCC_CHANNEL_A],
              "still disarmed at EOF (EOF does not write WR0 $20)");
        check((rr3(scc2) & 0x20) != 0,
              "EOF is a special condition: Channel A RX IP is set while disarmed");
        check(irq.get_irq(IRQ_ID_SCC),
              "SCC IRQ asserted for the EOF special condition");

        const uint8_t nextfrm[] = { 0x01, 0x80, 0x01, 0x00, 0x0d };
        check(scc2.ltoudp_try_inject(SCC_CHANNEL_A, nextfrm, sizeof(nextfrm)),
              "next frame injects, clearing the leftover EOF");
        clock_rx_bytes(scc2, 1);
        check(!scc2.rx_first_armed[SCC_CHANNEL_A],
              "first-character mode still disarmed after EOF and the next inject");
        check((rr3(scc2) & 0x20) == 0,
              "first character of the next frame does not set RX IP without WR0 $20");

        scc2.writeCmd(SCC_CHANNEL_A, kReEnbInt);
        check(scc2.rx_first_armed[SCC_CHANNEL_A],
              "WR0 command 100 ($20) re-arms first-character mode");
        check((rr3(scc2) & 0x20) != 0,
              "after WR0 $20, first character waiting in the FIFO sets RX IP");
    }

    // --- 6. 3-byte FIFO overruns if bytes arrive faster than the CPU drains ---
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
        const uint8_t longfrm[] = {
            0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
            0x60, 0x01, 0x01, 0x02
        };
        check(scc.ltoudp_try_inject(SCC_CHANNEL_A, longfrm, sizeof(longfrm)),
              "long frame queued");
        clock_rx_bytes(scc, 3);
        check(scc.rx_fifo[SCC_CHANNEL_A].count == 3,
              "FIFO is full after three character times with no reads");
        clock_rx_bytes(scc, 1);
        scc.writeCmd(SCC_CHANNEL_A, 1);
        check((scc.readCmd(SCC_CHANNEL_A) & 0x20) != 0,
              "fourth byte with a full FIFO sets RR1 Rx Overrun");
    }

    // --- 7. One peer frame on the cable; due is max(requested, line idle) ---
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
        const uint8_t f1[] = { 0x01, 0x80, 0x84 };
        const uint8_t f2[] = { 0x01, 0x80, 0x85 };
        scc.ltoudp_test_now = 0;
        check(scc.ltoudp_schedule_due(f1, sizeof(f1), 5000),
              "first frame schedules");
        check(scc.ltoudp_due.size() == 1, "one frame on the cable");
        check(!scc.ltoudp_schedule_due(f2, sizeof(f2), 1),
              "second frame is refused while the first occupies the cable");
        check(scc.ltoudp_due.size() == 1, "still one frame");
        check(scc.ltoudp_due[0].llap[2] == 0x84, "the first frame is the one kept");
    }

    // --- 8. Solicited CTS owns the line: it is not held behind an unsolicited
    //     credit, and it is due at rts_eof+IFG even if the datagram carries a
    //     later due stamp (peer line-idle / queued SendData).
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
        const uint8_t credit[] = { 0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
                                   0x60, 0x01, 0x01, 0x01, 0x01, 0x03, 0x00, 0x01 };
        const uint8_t peer_cts[] = { 0x01, 0x80, 0x85 };
        const uint8_t rts[] = { 0x80, 0x01, 0x84 };
        scc.ltoudp_test_now = 10000;
        check(scc.ltoudp_schedule_due(credit, sizeof(credit), 50000),
              "unsolicited SendData credit occupies the cable");
        check(scc.ltoudp_due.size() == 1 && scc.ltoudp_due[0].llap[2] == 0x01,
              "credit is the occupant");
        scc.ltoudp_inbox.emplace_back(peer_cts, peer_cts + sizeof(peer_cts));
        scc.ltoudp_inbox.back().due_14m = 200000;
        scc.ltoudp_inbox.back().has_due = true;
        scc.ltoudp_on_tx_llap(rts, sizeof(rts));
        check(scc.ltoudp_due.size() == 1, "one frame on the cable after RTS");
        check(!scc.ltoudp_due.empty() && scc.ltoudp_due[0].llap[2] == 0x85,
              "solicited CTS preempts the unsolicited credit");
        check(!scc.ltoudp_due.empty() &&
              scc.ltoudp_due[0].due_14m == 10000 + Scc::LTOUDP_IFG_14M,
              "CTS is due at rts_eof+IFG, not the later stamp or the credit due");
        check(!scc.ltoudp_inbox.empty() && scc.ltoudp_inbox.back().llap[2] == 0x01,
              "the credit is parked in the inbox, not dropped");

        scc.writeCmd(SCC_CHANNEL_A, 3);
        scc.writeCmd(SCC_CHANNEL_A, kEnbRx);
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "preempted CTS is not injected at WR3 RX-enable");
        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M);
        check(scc.ltoudp_due.empty(), "CTS consumed at eof+IFG");
        clock_rx_bytes(scc, 1);
        check((rr0(scc, SCC_CHANNEL_A) & 0x01) != 0,
              "CTS first byte is visible one character after eof+IFG");
        check(scc.rx_fifo[SCC_CHANNEL_A].data[0] == 0x01,
              "FIFO head is the CTS dest");
    }


    // --- 9. Late solicited reply under LTG1 is still due at cts_eof+IFG ---
    // A 5 ms host collect would miss this (Windows timer granularity is
    // ~15.6 ms). The LTG1 collect must wait for guest-time evidence.
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
        scc.ltoudp_peer_ltg1 = true;
        scc.ltoudp_test_now = 100000;
        const uint64_t due = 100000ull + Scc::LTOUDP_IFG_14M;
        std::thread late([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            (void)send_ltg1_llap(atp, sizeof(atp), 100000, due);
        });
        const auto t0 = std::chrono::steady_clock::now();
        scc.ltoudp_on_tx_llap(cts, sizeof(cts));
        const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        late.join();
        check(wait_ms >= 15 && wait_ms < 500,
              "late LTG1 solicited reply waited past 5 ms but not the 2 s safety bound");
        check(scc.ltoudp_last_collect_reason &&
              std::strcmp(scc.ltoudp_last_collect_reason, "reply") == 0,
              "late LTG1 collect ended on a stamped reply, not a timeout or peer-fault");
        check(scc.ltoudp_due.size() == 1,
              "late LTG1 reply is on the due queue");
        check(!scc.ltoudp_due.empty() && scc.ltoudp_due[0].due_14m == due,
              "late LTG1 reply is due at cts_eof+IFG");
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "late LTG1 reply is not injected at host poll end");
        scc.writeCmd(SCC_CHANNEL_A, 3);
        scc.writeCmd(SCC_CHANNEL_A, kEnbRx);
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "late LTG1 reply is not injected at WR3 RX-enable");
        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M);
        check(scc.ltoudp_due.empty(),
              "late LTG1 reply is consumed at cts_eof+IFG");
        clock_rx_bytes(scc, 1);
        check((rr0(scc, SCC_CHANNEL_A) & 0x01) != 0,
              "late LTG1 reply first byte is visible one character after eof+IFG");
    }

    // --- 10. A no-reply beacon (cmd 3, now past the window) ends the collect ---
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
        scc.ltoudp_peer_ltg1 = true;
        scc.ltoudp_test_now = 100000;
        const uint64_t past = 100000ull + Scc::LTOUDP_IFG_14M + 1;
        std::thread nr([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            (void)send_ltg1_time(LTOUDP_TIME_NOREPLY, past, past);
        });
        const auto t0 = std::chrono::steady_clock::now();
        scc.ltoudp_on_tx_llap(cts, sizeof(cts));
        const auto wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        nr.join();
        check(wait_ms >= 5 && wait_ms < 500,
              "no-reply beacon ended the LTG1 collect without waiting the 2 s safety bound");
        check(scc.ltoudp_last_collect_reason &&
              std::strcmp(scc.ltoudp_last_collect_reason, "noreply") == 0,
              "LTG1 collect reason is noreply");
        check(scc.ltoudp_due.empty(),
              "no-reply leaves the due queue empty");
        check(fifo_empty(scc, SCC_CHANNEL_A),
              "no-reply injects nothing");
    }

        // --- 11. An unsolicited RTS after a solicited data frame waits IDG, not IFG ---
    // A new dialog must not pile onto PacketIRQ of the previous frame.
    {
        Scc scc(&irq, nullptr, nullptr);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
        scc.ltoudp_inbox.emplace_back(atp, atp + sizeof(atp));
        scc.ltoudp_test_now = 100000;
        scc.ltoudp_on_tx_llap(cts, sizeof(cts));
        check(scc.ltoudp_due.size() == 1 &&
              scc.ltoudp_due[0].due_14m == 100000 + Scc::LTOUDP_IFG_14M,
              "solicited data is still due at eof+IFG");
        scc.writeCmd(SCC_CHANNEL_A, 3);
        scc.writeCmd(SCC_CHANNEL_A, kEnbRx);
        scc.ltoudp_advance_14m(Scc::LTOUDP_IFG_14M);
        // Clock and drain data+FCS so the line is truly idle (any_ready).
        for (size_t i = 0; i < sizeof(atp) + 2; i++) {
            clock_rx_bytes(scc, 1);
            if (scc.rx_fifo[SCC_CHANNEL_A].count > 0)
                (void)scc.readData(SCC_CHANNEL_A);
        }
        check(scc.ltoudp_due.empty(), "solicited data has left the due queue");
        check(fifo_empty(scc, SCC_CHANNEL_A), "solicited data has left the FIFO");
        const uint64_t data_eof = scc.ltoudp_rx_eof_14m;
        check(data_eof != 0, "RX eof of the solicited data is recorded");
        const uint8_t rts[] = { 0x01, 0x80, 0x84 };
        scc.ltoudp_inbox.emplace_back(rts, rts + sizeof(rts));
        scc.ltoudp_schedule_unsolicited();
        check(scc.ltoudp_due.size() == 1, "unsolicited RTS is scheduled after the data");
        check(!scc.ltoudp_due.empty() &&
              scc.ltoudp_due[0].due_14m == data_eof + Scc::LTOUDP_IDG_14M,
              "unsolicited RTS is due at the previous frame eof+IDG");
        const uint64_t now = scc.ltoudp_test_now;
        if (data_eof + Scc::LTOUDP_IFG_14M > now) {
            scc.ltoudp_advance_14m(data_eof + Scc::LTOUDP_IFG_14M - now);
            check((rr0(scc, SCC_CHANNEL_A) & 0x01) == 0,
                  "unsolicited RTS is not visible at one IFG after the previous frame");
        }
    }

    // --- Guest-visible clocks tick with C14M, not host wall-clock ---
    // A 2 s freeze (LToUDP collect-peer-fault, rail HALT) must not advance
    // the seconds AppleTalk QtrSched catch-up reads. Host sleep with C14M
    // held still is 0 ticks; advancing C14M by 2 s of 14M time is +2.
    {
        static uint64_t fake_c14m = 0;
        auto c14m_of = [](void *ctx) -> uint64_t { return *static_cast<uint64_t *>(ctx); };
        a2gspu_timesource_install(c14m_of, &fake_c14m);
        g_a2gspu_unpinned_origin_latched = false;
        long long t0 = 0, t1 = 0, t2 = 0;
        a2gspu_guest_now_unix(&t0);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        a2gspu_guest_now_unix(&t1);
        check(t1 == t0,
              "2 s host freeze with C14M held still advances guest now by 0");
        fake_c14m = 2 * A2GSPU_C14M_PER_SECOND;
        a2gspu_guest_now_unix(&t2);
        check(t2 == t0 + 2,
              "2 s of emulated 14M time advances guest now by exactly 2");
        a2gspu_timesource_install(nullptr, nullptr);
    }

    // --- LTG1 conservative lockstep ---
    // The guest must never run past the peer's granted horizon, so a
    // deliberately slow peer (host sleep before sendto) delivers a frame
    // at the same guest tick as a fast one. A2GSPU_PACE=fast is the
    // advance loop trying to run well past T during that sleep.
    {
        const uint8_t frm[] = { 0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
                                0x60, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00, 0x02 };
        const uint64_t T = 2 * Scc::LTOUDP_IFG_14M;
        const uint64_t T2 = T + 20 * Scc::LTOUDP_IFG_14M;
        uint64_t due_fast = 0, due_slow = 0, now_fast = 0, now_slow = 0;

        auto run = [&](int delay_ms, uint64_t *due_out, uint64_t *now_out) {
            Scc scc(&irq);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
            scc.ltoudp_test_now = 0;
            check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, 0, 0, 0, T),
                  "lockstep grant T sent");
            scc.ltoudp_collect(200);
            check(scc.ltoudp_lockstep && scc.ltoudp_grant_14m == T,
                  "LTG1 grant T enabled lockstep");
            std::atomic<bool> sent{false};
            std::thread peer([&]() {
                if (delay_ms > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
                (void)send_ltg1_llap(frm, sizeof(frm), 0, T);
                (void)send_ltg1_time_grant(LTOUDP_TIME_BEACON, T, T, 0, T2);
                sent = true;
            });
            // Try to run well past T (fast pace during the peer's host delay).
            scc.ltoudp_advance_14m(100 * Scc::LTOUDP_IFG_14M);
            peer.join();
            scc.ltoudp_collect(0);
            scc.ltoudp_schedule_unsolicited();
            scc.ltoudp_deliver_due();
            uint64_t due = 0;
            if (!scc.ltoudp_due.empty())
                due = scc.ltoudp_due[0].due_14m;
            else
                due = T; // already consumed at T
            *due_out = due;
            *now_out = scc.ltoudp_test_now;
            check(scc.ltoudp_test_now >= T,
                  "lockstep advance reached the grant");
            // During the host delay the guest must not have run past T
            // before the frame was on the due queue; after the later
            // grant it may go further, but the frame's due is T.
            (void)sent;
        };

        run(0, &due_fast, &now_fast);
        run(200, &due_slow, &now_slow);
        check(due_fast == T && due_slow == T,
              "slow and fast peers deliver at the same guest due tick T");
        // Identical delivery trace: the frame is due at T in both runs.
        check(due_fast == due_slow,
              "slow-peer guest-tick delivery matches the fast peer");
    }

    // Tight grant=due only, no follow-up beacon: the in-hand LLAP must
    // extend the horizon through the frame plus IDG so the guest can
    // receive and reply without a 2 s horizon-peer-fault.
    {
        const uint8_t frm[] = { 0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
                                0x60, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00, 0x02 };
        const uint64_t T = 2 * Scc::LTOUDP_IFG_14M;
        Scc scc(&irq);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
        scc.ltoudp_test_now = 0;
        scc.ltoudp_horizon_faults = 0;
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, 0, 0, 0, T),
              "tight-grant T sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_lockstep && scc.ltoudp_grant_14m == T,
              "tight grant T armed lockstep");
        const auto t0 = std::chrono::steady_clock::now();
        std::thread peer([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            (void)send_ltg1_llap(frm, sizeof(frm), 0, T);
            // Deliberately no follow-up grant beacon.
        });
        // Run to T (blocks until the LLAP extends the horizon). Do not
        // ask to run past the implicit post-frame grant, or the 2 s
        // safety would fire and hide the hole we are testing.
        scc.ltoudp_advance_14m(T);
        peer.join();
        scc.ltoudp_advance_14m(Scc::LTOUDP_IDG_14M);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        scc.ltoudp_collect(0);
        scc.ltoudp_schedule_unsolicited();
        scc.ltoudp_deliver_due();
        check(ms < 1000,
              "tight grant without a follow-up beacon does not wait the 2 s safety");
        check(scc.ltoudp_horizon_faults == 0,
              "tight-grant delivery does not record a horizon-peer-fault");
        check(scc.ltoudp_test_now >= T,
              "guest advanced to the frame due");
        const uint64_t implicit = T + (sizeof(frm) + 2) * Scc::LTOUDP_BYTE_14M
            + Scc::LTOUDP_IDG_14M;
        check(scc.ltoudp_grant_14m >= implicit || scc.ltoudp_test_now >= T,
              "in-hand LLAP extends the horizon past due");
    }

    // A stale grant (behind C14M) must be ignored, not installed as
    // 'now'. Capping at now freezes the guest to one cycle per
    // heartbeat and makes a rail 'run N' miss its host deadline
    // (seen in a harness run: run 2100 never acked, faults==0).
    {
        Scc scc(&irq);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
        scc.ltoudp_test_now = 0;
        scc.ltoudp_horizon_faults = 0;
        const uint64_t T = 20 * Scc::LTOUDP_IFG_14M;
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, 0, 0, 0, T),
              "stale-grant setup T sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_lockstep && scc.ltoudp_grant_14m == T,
              "stale-grant setup armed lockstep at T");
        scc.ltoudp_advance_14m(T);
        check(scc.ltoudp_test_now == T, "guest reached T");
        // Lift as the safety bound does, let the guest run ahead, then
        // inject a heartbeat whose grant is already behind now.
        // ltoudp_advance_14m takes a delta, not an absolute tick.
        scc.ltoudp_grant_14m = ~uint64_t(0);
        const uint64_t delta = 20 * Scc::LTOUDP_IFG_14M;
        scc.ltoudp_advance_14m(delta);
        check(scc.ltoudp_test_now == T + delta, "guest ran ahead under a lifted cap");
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, T, T, 0, T),
              "stale grant behind now sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_grant_14m == ~uint64_t(0),
              "stale grant does not cap the horizon at now");
        const auto t0 = std::chrono::steady_clock::now();
        scc.ltoudp_advance_14m(delta);
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        check(ms < 1000,
              "after an ignored stale grant the guest is not held for 2 s");
        check(scc.ltoudp_test_now == T + 2 * delta,
              "guest kept running after the stale heartbeat");
        check(scc.ltoudp_horizon_faults == 0,
              "stale-grant case records no horizon-peer-fault");
    }

    // Guest-frozen collect after an ENQ: the peer's clock.now is also
    // frozen and cannot walk past the ACK window. A TIME_NOREPLY (or a
    // grant already past the deadline) must end collect without the
    // 2 s host safety / collect-peer-fault (seen in a harness run).
    {
        Scc scc(&irq);
        program_sdlc(scc, SCC_CHANNEL_A, 0x01, kEnbRx);
        const uint64_t eof = 20 * Scc::LTOUDP_IFG_14M;
        scc.ltoudp_test_now = eof;
        scc.ltoudp_tx_eof_14m = eof;
        const uint64_t deadline = eof + Scc::LTOUDP_TIFGAP1_14M
            + 5ull * Scc::LTOUDP_BYTE_14M;
        const uint64_t G = deadline + 20 * Scc::LTOUDP_IFG_14M;
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, eof, eof, 0, eof),
              "noreply-collect setup grant sent");
        scc.ltoudp_collect(0);
        check(scc.ltoudp_lockstep, "noreply-collect armed lockstep");
        const uint8_t enq_tx[] = { 0x01, 0x01, 0x81 };
        std::thread peer([&]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            // Frozen now (== eof) is still behind the deadline; the
            // NOREPLY plus a grant past the window must be enough.
            (void)send_ltg1_time_grant(LTOUDP_TIME_NOREPLY, eof, eof, 0, G, eof);
        });
        const auto t0 = std::chrono::steady_clock::now();
        scc.ltoudp_collect(Scc::LTOUDP_LTG1_SAFETY_MS, enq_tx, sizeof(enq_tx),
                           /*ltg1_reply=*/true);
        peer.join();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        check(ms < 1000,
              "TIME_NOREPLY ends ENQ collect without the 2 s safety");
        check(std::strcmp(scc.ltoudp_last_collect_reason, "noreply") == 0,
              "ENQ collect reason is noreply, not peer-fault");
        check(scc.ltoudp_grant_14m == G,
              "NOREPLY grant past the window lifts the horizon");
    }

    // Uncollected guest TX (NBP-style data): leftover idle lookahead
    // cannot lift the eof+IDG clamp until a grant accounts for that TX.
    // Do not ask to run past the cap, or the 2 s safety fires.
    {
        Scc scc(&irq);
        program_sdlc(scc, SCC_CHANNEL_A, 0x80, kEnbRx);
        const uint64_t eof = 20 * Scc::LTOUDP_IFG_14M;
        const uint64_t idle_far = eof + (MASTER_CLOCK / 2); // 500 ms idle lookahead
        scc.ltoudp_test_now = eof;
        scc.ltoudp_horizon_faults = 0;
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, 0, 0, 0, idle_far),
              "uncollected-TX leftover grant sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_lockstep && scc.ltoudp_grant_14m == idle_far,
              "uncollected-TX armed lockstep at leftover 500 ms grant");
        // Short DDP data (NBP-like), not ENQ/RTS/CTS/TReq, so no collect
        // and the clamp is eof+IDG, not eof+IFG.
        const uint8_t data[] = { 0xFF, 0x80, 0x01, 0x02, 0x01, 0x02,
                                 0x01, 0x00, 0x00 };
        scc.ltoudp_on_tx_llap(data, sizeof(data));
        const uint64_t cap = eof + Scc::LTOUDP_IDG_14M;
        check(scc.ltoudp_tx_eof_14m == eof, "uncollected TX eof is now");
        check(scc.ltoudp_clamp_14m == cap,
              "uncollected guest TX clamps to eof+IDG");
        check(scc.ltoudp_grant_14m == cap,
              "leftover 500 ms grant is clamped at eof+IDG");
        scc.ltoudp_advance_14m(Scc::LTOUDP_IDG_14M);
        check(scc.ltoudp_test_now == cap, "guest may run to the clamp");
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, cap, cap, 0, idle_far,
                                   /*accounted=*/0, /*with_accounted=*/false),
              "unaccounted 4-word leftover grant sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_grant_14m == cap && scc.ltoudp_clamp_14m == cap,
              "4-word leftover grant does not lift the clamp");
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, cap, cap, 0, idle_far, 0),
              "5-word grant with accounted=0 sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_grant_14m == cap && scc.ltoudp_clamp_14m == cap,
              "accounted=0 leftover grant does not lift the clamp");
        check(send_ltg1_time_grant(LTOUDP_TIME_BEACON, cap, cap, 0, idle_far, eof),
              "accounted grant sent");
        scc.ltoudp_collect(200);
        check(scc.ltoudp_clamp_14m == 0,
              "grant that accounts for the TX lifts the clamp");
        check(scc.ltoudp_grant_14m == idle_far,
              "accounted grant restores the leftover lookahead");
        const uint64_t step = 20 * Scc::LTOUDP_IFG_14M;
        scc.ltoudp_advance_14m(step);
        check(scc.ltoudp_test_now == cap + step,
              "after an accounted grant the guest may run past the clamp");
        check(scc.ltoudp_horizon_faults == 0,
              "uncollected-TX case records no horizon-peer-fault");
    }


    // --- 16. Injected-drop reliability: guest-tick traces match the no-drop run ---
    // A dropped peer CTS/data frame, a dropped guest frame, and a dropped
    // time control each recover by NACK/resend with identical due/grant.
    {
        const uint8_t atp16[] = { 0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
                                  0x60, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00, 0x02 };
        const uint8_t cts16[] = { 0x80, 0x01, 0x85 };
        const uint64_t now16 = 100000ull;
        const uint64_t due16 = now16 + Scc::LTOUDP_IFG_14M;

        auto advertise = [](Scc &scc, SeqPeer &peer) {
            check(peer.send_time(LTOUDP_TIME_BEACON, 0, 0, 0, 0, 0),
                  "SEQ1 advertisement sent");
            scc.ltoudp_collect(200);
            check(ltoudp_test_remote_seq(), "official saw SEQ1 advertisement");
            (void)peer.pump(50); // drain official advertisement beacons
        };

        // 16a. Dropped peer data (ATP after guest CTS) vs no-drop.
        uint64_t due_a_nodrop = 0, due_a_drop = 0;
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_test_now = now16;
            SeqPeer peer;
            check(peer.open(), "seq peer open (16a no-drop)");
            ltoudp_test_reset_stats();
            advertise(scc, peer);
            std::thread late([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                (void)peer.send_llap(atp16, sizeof(atp16), now16, due16);
            });
            scc.ltoudp_on_tx_llap(cts16, sizeof(cts16));
            late.join();
            check(scc.ltoudp_last_collect_reason &&
                  std::strcmp(scc.ltoudp_last_collect_reason, "reply") == 0,
                  "16a no-drop collect ended on reply");
            check(scc.ltoudp_due.size() == 1, "16a no-drop ATP is on the due queue");
            if (!scc.ltoudp_due.empty()) due_a_nodrop = scc.ltoudp_due[0].due_14m;
            peer.close_sock();
        }
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_test_now = now16;
            SeqPeer peer;
            check(peer.open(), "seq peer open (16a drop)");
            ltoudp_test_reset_stats();
            advertise(scc, peer);
            peer.drop_next_tx = 1;
            std::thread late([&] {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                (void)peer.send_llap(atp16, sizeof(atp16), now16, due16);
                (void)peer.send_time(LTOUDP_TIME_BEACON, now16, 0, 0, 0, 0);
                (void)peer.pump(400);
            });
            scc.ltoudp_on_tx_llap(cts16, sizeof(cts16));
            late.join();
            check(scc.ltoudp_last_collect_reason &&
                  std::strcmp(scc.ltoudp_last_collect_reason, "reply") == 0,
                  "16a drop collect ended on reply");
            check(scc.ltoudp_due.size() == 1, "16a drop ATP is on the due queue");
            if (!scc.ltoudp_due.empty()) due_a_drop = scc.ltoudp_due[0].due_14m;
            check(ltoudp_test_nacks_tx() >= 1, "16a official NACKed the dropped peer data");
            check(peer.resends >= 1, "16a peer resent the dropped data");
            peer.close_sock();
        }
        check(due_a_nodrop == due16 && due_a_drop == due_a_nodrop,
              "16a dropped peer data has the same guest-tick due as no-drop");

        // 16b. Dropped guest frame (official CTS) vs no-drop.
        uint64_t due_b_nodrop = 0, due_b_drop = 0;
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_test_now = now16;
            SeqPeer peer;
            check(peer.open(), "seq peer open (16b no-drop)");
            ltoudp_test_reset_stats();
            advertise(scc, peer);
            std::thread late([&] {
                const auto t0 = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(800)) {
                    if (peer.pump(50) && !peer.last_llap.empty()
                        && peer.last_llap.size() >= 3 && peer.last_llap[2] == 0x85) {
                        (void)peer.send_llap(atp16, sizeof(atp16), now16, due16);
                        return;
                    }
                }
            });
            scc.ltoudp_on_tx_llap(cts16, sizeof(cts16));
            late.join();
            check(scc.ltoudp_last_collect_reason &&
                  std::strcmp(scc.ltoudp_last_collect_reason, "reply") == 0,
                  "16b no-drop collect ended on reply");
            if (!scc.ltoudp_due.empty()) due_b_nodrop = scc.ltoudp_due[0].due_14m;
            peer.close_sock();
        }
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_test_now = now16;
            SeqPeer peer;
            check(peer.open(), "seq peer open (16b drop)");
            ltoudp_test_reset_stats();
            advertise(scc, peer);
            peer.drop_next_rx = 1;
            std::thread late([&] {
                // Drop the first official datagram after advertise (the CTS
                // LLAP). The collect keepalive makes the gap visible; we
                // NACK, official resends the CTS, then we reply with ATP.
                const auto t0 = std::chrono::steady_clock::now();
                while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(800)) {
                    if (peer.pump(50) && !peer.last_llap.empty()
                        && peer.last_llap.size() >= 3 && peer.last_llap[2] == 0x85) {
                        (void)peer.send_llap(atp16, sizeof(atp16), now16, due16);
                        return;
                    }
                }
            });
            scc.ltoudp_on_tx_llap(cts16, sizeof(cts16));
            late.join();
            check(scc.ltoudp_last_collect_reason &&
                  std::strcmp(scc.ltoudp_last_collect_reason, "reply") == 0,
                  "16b drop collect ended on reply");
            if (!scc.ltoudp_due.empty()) due_b_drop = scc.ltoudp_due[0].due_14m;
            check(peer.nacks_tx >= 1 || ltoudp_test_resends() >= 1,
                  "16b dropped guest CTS was NACKed and resent");
            peer.close_sock();
        }
        check(due_b_nodrop == due16 && due_b_drop == due_b_nodrop,
              "16b dropped guest frame has the same guest-tick due as no-drop");

        // 16c. Dropped time control vs no-drop: same final grant.
        uint64_t grant_c_nodrop = 0, grant_c_drop = 0;
        const uint64_t g1 = 200000ull;
        const uint64_t g2 = 400000ull;
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_lockstep = true;
            scc.ltoudp_test_now = 0;
            SeqPeer peer;
            check(peer.open(), "seq peer open (16c no-drop)");
            ltoudp_test_reset_stats();
            advertise(scc, peer);
            check(peer.send_time(LTOUDP_TIME_BEACON, g1, g1, 0, g1, 0), "16c grant 1");
            check(peer.send_time(LTOUDP_TIME_BEACON, g2, g2, 0, g2, 0), "16c grant 2");
            scc.ltoudp_collect(200);
            grant_c_nodrop = scc.ltoudp_grant_14m;
            peer.close_sock();
        }
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_lockstep = true;
            scc.ltoudp_test_now = 0;
            SeqPeer peer;
            check(peer.open(), "seq peer open (16c drop)");
            ltoudp_test_reset_stats();
            advertise(scc, peer);
            ltoudp_test_inject_drop(1);
            std::thread late([&] {
                (void)peer.send_time(LTOUDP_TIME_BEACON, g1, g1, 0, g1, 0);
                (void)peer.send_time(LTOUDP_TIME_BEACON, g2, g2, 0, g2, 0);
                (void)peer.pump(400);
            });
            scc.ltoudp_collect(500);
            late.join();
            grant_c_drop = scc.ltoudp_grant_14m;
            check(ltoudp_test_nacks_tx() >= 1, "16c official NACKed the dropped time control");
            check(peer.resends >= 1, "16c peer resent the dropped time control");
            peer.close_sock();
        }
        check(grant_c_nodrop == g2 && grant_c_drop == grant_c_nodrop,
              "16c dropped time control has the same grant as no-drop");
    }


    // --- 17. Untagged LTG1 does not punch a seq hole; NACK is once per missing ---
    {
        const uint8_t cts17[] = { 0x80, 0x01, 0x85 };
        const uint8_t atp17[] = { 0x01, 0x80, 0x01, 0x00, 0x0d, 0x81, 0x04, 0x03,
                                  0x60, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00, 0x02 };
        const uint64_t now17 = 100000ull;
        const uint64_t due17 = now17 + Scc::LTOUDP_IFG_14M;

        // 17a. Classic (untagged) LTG1 then SEQ1 starting at 1: no NACK.
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_test_now = now17;
            SeqPeer peer;
            check(peer.open(), "seq peer open (17a)");
            ltoudp_test_reset_stats();
            check(send_ltg1_llap(cts17, sizeof(cts17), now17, due17),
                  "17a untagged LTG1 CTS sent");
            check(peer.send_time(LTOUDP_TIME_BEACON, now17, 0, 0, 0, 0),
                  "17a first tagged time is seq 1");
            scc.ltoudp_collect(200);
            check(ltoudp_test_nacks_tx() == 0,
                  "17a untagged-then-seq1 does not NACK a phantom hole");
            check(scc.ltoudp_due.size() == 1 || !scc.ltoudp_inbox.empty(),
                  "17a untagged CTS was delivered");
            peer.close_sock();
        }

        // 17b. SEQ1 2,3,4 with no seq 1: exactly one NACK (not a storm).
        {
            Scc scc(&irq, nullptr, nullptr);
            program_sdlc(scc, SCC_CHANNEL_A, 0x01, kDisRx);
            scc.ltoudp_peer_ltg1 = true;
            scc.ltoudp_test_now = now17;
            SeqPeer peer;
            check(peer.open(), "seq peer open (17b)");
            ltoudp_test_reset_stats();
            // Manually emit SEQ1 frames 2,3,4 (skip 1) in one burst.
            peer.out_seq = 1; // next send would be 2; we bypass send_llap
            for (uint64_t seq = 2; seq <= 4; seq++) {
                std::vector<uint8_t> buf(4 + 20 + 4 + 8 + sizeof(atp17));
                buf[0] = (uint8_t)(peer.sid);
                buf[1] = (uint8_t)(peer.sid >> 8);
                buf[2] = (uint8_t)(peer.sid >> 16);
                buf[3] = (uint8_t)(peer.sid >> 24);
                std::memcpy(buf.data() + 4, "LTG1", 4);
                put_u64le(buf.data() + 8, now17);
                put_u64le(buf.data() + 16, due17);
                std::memcpy(buf.data() + 24, "SEQ1", 4);
                put_u64le(buf.data() + 28, seq);
                std::memcpy(buf.data() + 36, atp17, sizeof(atp17));
                peer.sent[seq] = buf;
                auto d = peer.dest();
                sendto(peer.s, (const char *)buf.data(), (int)buf.size(), 0,
                       (sockaddr *)&d, sizeof(d));
            }
            scc.ltoudp_collect(40); // under the 50 ms re-NACK cooldown
            check(ltoudp_test_nacks_tx() == 1,
                  "17b NACK once per missing seq, not once per later datagram");
            check(scc.ltoudp_due.empty() && scc.ltoudp_inbox.empty(),
                  "17b held out-of-order frames are not delivered before seq 1");
            peer.close_sock();
        }
    }

    ltoudp_close();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
