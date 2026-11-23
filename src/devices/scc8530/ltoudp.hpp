#pragma once

// LocalTalk-over-UDP (LToUDP): raw LLAP over UDP multicast, loopback/local
// only by default. See Docs/LToUDP.md.
//
// Classic datagram: 4-byte little-endian sender id + raw LLAP (dest, src,
// type, data) with no SDLC flags or FCS. Group 239.192.76.84 port 1954.
//
// Optional LTG1 guest-time extension (plain peers ignore it, or never see
// the extended LLAP form):
//   Time control: sid + dest=$00 src=$00 type=$C0 + "LTG1" + cmd + now_u64le
//                 + idle_u64le + delivered_u64le + grant_u64le +
//                 accounted_u64le + optional seq_u64le. cmd 0 = request,
//                 1 = beacon/reply, 2 = delivered-at, 3 = no-reply, 4 =
//                 NACK (now = first missing seq). grant is "nothing from
//                 me before T." accounted is the eof tick of the last guest
//                 frame that grant promises silence for. seq is the
//                 sender's monotonic datagram sequence. delivered/grant/
//                 accounted/seq may be 0; older 2-, 3-, 4-, and 5-word
//                 controls still parse.
//   Extended LLAP (only after a peer has advertised LTG1): sid + "LTG1"
//                 + now_u64le + due_u64le + llap. A sequenced speaker
//                 that has seen a SEQ1 advertisement inserts "SEQ1" +
//                 seq_u64le between due and the llap. Plain peers are
//                 unchanged (classic 4+LLAP, no seq).
//
// Bind and membership are 127.0.0.1, IP_MULTICAST_TTL=0, so packets stay on
// this host. A2GSPU_LTOUDP=0 disables. Optional A2GSPU_LTOUDP_PORT.

#include <cstddef>
#include <cstdint>

bool ltoudp_open();
void ltoudp_close();
bool ltoudp_enabled();

// Send one raw LLAP frame (3..603 bytes). No-op if disabled or size is wrong.
// Classic 4+LLAP form; use ltoudp_send_llap_guest for a stamped datagram.
void ltoudp_send_llap(const uint8_t *llap, size_t n);

// Stamp an LLAP with guest time. If ext is true, use the LTG1 prefix
// (sid + "LTG1" + now + due + llap). Otherwise classic 4+LLAP (plain peer).
void ltoudp_send_llap_guest(const uint8_t *llap, size_t n,
                            uint64_t now_14m, uint64_t due_14m, bool ext);

// Time request (cmd=0), beacon/reply (cmd=1), delivered-at (cmd=2),
// or no-reply (cmd=3).
// dest=$00 type=$C0, not LLAP. delivered_14m is the last-byte RX tick of
// the most recent peer frame, or 0.
void ltoudp_send_time(uint8_t cmd, uint64_t now_14m, uint64_t idle_14m,
                      uint64_t delivered_14m = 0, uint64_t grant_14m = 0,
                      uint64_t accounted_14m = 0);

// Non-blocking by default (timeout_ms=0). Returns bytes stored (3..max) or 0.
// Drops own sender id and LTG1 time controls. Strips an LTG1 LLAP prefix.
size_t ltoudp_recv_llap(uint8_t *llap, size_t max, int timeout_ms);

enum {
    LTOUDP_RECV_NONE = 0,
    LTOUDP_RECV_LLAP = 1,
    LTOUDP_RECV_TIME = 2
};

struct ltoudp_pkt {
    uint8_t llap[608];
    size_t n;
    bool has_guest;
    uint64_t now_14m;
    uint64_t due_14m;
    uint64_t idle_14m;
    uint64_t delivered_14m;
    uint64_t grant_14m;
    uint64_t accounted_14m;
    uint64_t seq;
    uint32_t sid;
    uint8_t time_cmd;
    int kind;
};

// Like recv_llap, but keeps LTG1 timestamps and reports time controls.
// kind is LTOUDP_RECV_*.
int ltoudp_recv_pkt(ltoudp_pkt *out, int timeout_ms);

uint16_t ltoudp_crc16_x25(const uint8_t *p, size_t n);

constexpr uint8_t LTOUDP_LAP_TIME = 0xC0;
constexpr uint8_t LTOUDP_TIME_REQ = 0;
constexpr uint8_t LTOUDP_TIME_BEACON = 1;
constexpr uint8_t LTOUDP_TIME_DELIVERED = 2;
constexpr uint8_t LTOUDP_TIME_NOREPLY = 3;
constexpr uint8_t LTOUDP_TIME_NACK = 4;

// Test hooks for sccltoudptest injected-drop cases. No-ops if disabled.
void ltoudp_test_inject_drop(int n);
void ltoudp_test_reset_stats();
uint64_t ltoudp_test_gaps();
uint64_t ltoudp_test_nacks_tx();
uint64_t ltoudp_test_nacks_rx();
uint64_t ltoudp_test_resends();
bool ltoudp_test_remote_seq();
