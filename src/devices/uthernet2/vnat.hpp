/*
 * vnat.hpp — Virtual NAT: Ethernet frame bridge for W5100 MACRAW mode
 *
 * ══════════════════════════════════════════════════════════════════════════
 * ARCHITECTURAL OVERVIEW
 * ══════════════════════════════════════════════════════════════════════════
 *
 * The W5100 chip on the Uthernet II card can operate in one of two modes:
 *
 *   1. Socket mode (TCP/UDP):  The W5100 manages the full TCP/IP stack
 *      internally. The Apple IIgs software writes payload data; the chip
 *      wraps it in TCP/UDP/IP/Ethernet headers automatically. This mode
 *      is used by simple applications that talk to the W5100 directly.
 *
 *   2. MACRAW mode:  The W5100 is used as a raw Ethernet NIC. The Apple
 *      IIgs software (Marinetti in this case) builds complete Ethernet
 *      frames from scratch — including Ethernet, IP, TCP/UDP headers —
 *      and hands them to the W5100 as opaque byte sequences. Marinetti
 *      also receives complete raw Ethernet frames back. This mode is
 *      used by Marinetti because it wants to implement its own full TCP/IP
 *      stack rather than relying on the W5100's limited hardware stack.
 *
 * WHY THIS MODULE EXISTS
 * ──────────────────────
 * In a real system, MACRAW mode sends and receives real Ethernet frames
 * on a physical network. In our emulator, the "network" is the host OS's
 * networking stack (Windows/macOS/Linux), accessed via SDL3_net.
 *
 * SDL3_net works at the BSD socket level (TCP streams, UDP datagrams),
 * not at the Ethernet frame level. We cannot use pcap (promiscuous raw
 * capture) without administrator privileges and a real NIC, and we cannot
 * use libslirp (QEMU's user-mode NAT) without adding a heavy dependency.
 *
 * The solution is this Virtual NAT layer, which acts as a software Ethernet
 * switch + NAT router between the IIgs and the real internet:
 *
 *   IIgs (Marinetti)
 *       │  raw Ethernet frames
 *       ▼
 *   W5100 emulation (w5100.cpp)
 *       │  calls vnat_send_frame() / vnat_recv_frame()
 *       ▼
 *   vnat.cpp  ◄──── This module ────►  SDL3_net  ──► Host OS ──► Internet
 *       │
 *       │  parses: ARP, IPv4/UDP, IPv4/TCP
 *       │  crafts: ARP replies, UDP datagrams, TCP segments
 *       ▼
 *   rx_buf[] (response frame queue)
 *       │
 *       ▼
 *   W5100 MACRAW RX ring ──► Marinetti reads
 *
 * The virtual NAT presents itself to the IIgs as:
 *   - A single MAC address (gw_mac) that owns ALL remote IP addresses.
 *     Every ARP request (regardless of target IP) gets a reply claiming
 *     that gw_mac owns that IP. This is called "proxy ARP" and is the
 *     correct behavior for a NAT gateway — the IIgs never needs to know
 *     which physical host owns which IP.
 *   - A transparent forwarder. UDP datagrams are sent through a
 *     NET_DatagramSocket and responses are relayed back. TCP connections
 *     are proxied through NET_StreamSocket with full sequence number
 *     tracking.
 *
 * PROTOCOL STACK IMPLEMENTED
 * ──────────────────────────
 *   Ethernet II framing (14-byte header: dst MAC, src MAC, EtherType)
 *   ARP (Address Resolution Protocol, RFC 826)
 *   IPv4 (RFC 791, fixed 20-byte header, no options)
 *   ICMP (RFC 792, recognized but not yet implemented beyond stub)
 *   UDP (RFC 768, including pseudo-header checksum)
 *   TCP (RFC 793, SYN/SYN-ACK/ACK/PSH/FIN/RST, fixed 20-byte header)
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0 (matching GSSquared)
 */

#pragma once

#include <cstdint>
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>

// ══════════════════════════════════════════════════════════════════════════
// PROTOCOL CONSTANTS
// ══════════════════════════════════════════════════════════════════════════

// ── Ethernet frame layout ───────────────────────────────────────────────
//
// An Ethernet II frame begins with a 14-byte header:
//
//   Offset  Size  Field
//   ──────  ────  ─────────────────────────────────────────────
//    0      6     Destination MAC address
//    6      6     Source MAC address
//   12      2     EtherType (identifies the encapsulated protocol)
//   14      …     Payload (up to 1500 bytes for standard MTU)
//
// EtherType values below 0x0600 are "length" fields (802.3 framing).
// Values >= 0x0600 are EtherType identifiers (Ethernet II framing).
// The two types we care about:
//   0x0800 = IPv4 payload
//   0x0806 = ARP payload

#define ETH_HEADER_SIZE   14        // 6 (dst MAC) + 6 (src MAC) + 2 (EtherType)
#define ETH_TYPE_ARP      0x0806    // Address Resolution Protocol
#define ETH_TYPE_IP       0x0800    // Internet Protocol version 4

// ── IPv4 header layout ──────────────────────────────────────────────────
//
// The IPv4 header (RFC 791) with no options is exactly 20 bytes:
//
//   Offset  Size  Field
//   ──────  ────  ─────────────────────────────────────────────
//    0      1     Version (4) | IHL (header length in 32-bit words, min=5)
//    1      1     DSCP (6 bits) | ECN (2 bits)  — formerly TOS
//    2      2     Total Length (header + payload, in bytes)
//    4      2     Identification (fragment reassembly ID)
//    6      2     Flags (3 bits) | Fragment Offset (13 bits)
//    8      1     TTL (hop limit, we use 64)
//    9      1     Protocol (identifies transport layer: 1=ICMP, 6=TCP, 17=UDP)
//   10      2     Header Checksum (one's complement sum of header words)
//   12      4     Source IP address
//   16      4     Destination IP address
//
// IHL=5 means "5 × 4 = 20 bytes" — no options present.
// We always generate IHL=5 (0x45 = version 4, IHL 5).

#define IP_HEADER_SIZE    20        // Minimum IPv4 header, no options

// IPv4 Protocol field values (ip[9]):
#define IP_PROTO_ICMP     1         // Internet Control Message Protocol
#define IP_PROTO_TCP      6         // Transmission Control Protocol
#define IP_PROTO_UDP      17        // User Datagram Protocol

// ── UDP header layout ───────────────────────────────────────────────────
//
// UDP (RFC 768) has a minimal 8-byte header:
//
//   Offset  Size  Field
//   ──────  ────  ─────────────────────────────────────────────
//    0      2     Source Port
//    2      2     Destination Port
//    4      2     Length (UDP header + payload, in bytes; minimum 8)
//    6      2     Checksum (pseudo-header + UDP header + payload)
//
// The UDP checksum covers a "pseudo-header" prepended to the UDP segment
// for checksum purposes only (it is never actually transmitted):
//
//   Pseudo-header (12 bytes):
//     4 bytes  Source IP
//     4 bytes  Destination IP
//     1 byte   Zero (padding)
//     1 byte   Protocol (17 = UDP)
//     2 bytes  UDP Length
//
// This binds the UDP checksum to the IP addresses, preventing packets from
// being misdelivered and accepted by the wrong socket. For IPv4, a UDP
// checksum of 0x0000 means "no checksum" (checksum was omitted by sender).
// If the computed checksum happens to be 0x0000, the value 0xFFFF is sent
// instead (both represent the same all-ones bit pattern in one's complement).

#define UDP_HEADER_SIZE   8         // Source port, dest port, length, checksum

// ── TCP header layout ───────────────────────────────────────────────────
//
// TCP (RFC 793) has a minimum 20-byte header (no options):
//
//   Offset  Size  Field
//   ──────  ────  ─────────────────────────────────────────────
//    0      2     Source Port
//    2      2     Destination Port
//    4      4     Sequence Number (SEQ) — byte offset in our stream
//    8      4     Acknowledgment Number (ACK) — next expected from peer
//   12      1     Data Offset (4 bits, header length in 32-bit words) | Reserved (4 bits)
//   13      1     Control Flags (see TCP_FLAG_* below)
//   14      2     Window Size (receive buffer space offered to sender)
//   16      2     Checksum (same pseudo-header structure as UDP)
//   18      2     Urgent Pointer (only valid when URG flag set)
//   20+     …     Options (if Data Offset > 5) — we never generate options
//
// TCP is a reliable, ordered, connection-oriented protocol. The sequence
// numbers allow detection of reordering and loss; the ACK number tells the
// sender which bytes have been received. We implement the minimum needed:
//   - Three-way handshake: SYN → SYN+ACK → ACK
//   - Data transfer: PSH+ACK (push flag tells receiver to deliver immediately)
//   - Teardown: FIN+ACK → ACK or RST (abortive close)
// We do NOT implement: retransmission, sliding window, congestion control,
// TCP options (MSS, timestamps, SACK). Since SDL3_net handles the real TCP
// connection to the remote server, these are not needed — we only need to
// satisfy Marinetti's TCP state machine.

#define TCP_HEADER_SIZE   20        // Minimum TCP header, no options

// ── ARP message layout ──────────────────────────────────────────────────
//
// ARP (RFC 826) resolves an IPv4 address to an Ethernet MAC address.
// An ARP message for Ethernet/IPv4 is always exactly 28 bytes:
//
//   Offset  Size  Field
//   ──────  ────  ─────────────────────────────────────────────
//    0      2     Hardware Type (1 = Ethernet)
//    2      2     Protocol Type (0x0800 = IPv4)
//    4      1     Hardware Address Length (6 for Ethernet)
//    5      1     Protocol Address Length (4 for IPv4)
//    6      2     Operation (1 = Request, 2 = Reply)
//    8      6     Sender Hardware Address (MAC)
//   14      4     Sender Protocol Address (IP)
//   18      6     Target Hardware Address (MAC; zero-filled for Requests)
//   24      4     Target Protocol Address (IP being queried)
//
// An ARP Request says: "Who has IP X? Tell IP Y (my MAC is Z)."
// An ARP Reply says:   "IP X is at MAC W."
//
// In our virtual NAT, we respond to ALL ARP Requests with our gateway MAC,
// regardless of the target IP. This is correct proxy-ARP behavior for a
// virtual network — all external IP addresses are "reachable via gateway."

#define ARP_SIZE          28        // Fixed size for Ethernet/IPv4 ARP

// ── TCP control flags (tcp[13]) ─────────────────────────────────────────
//
// These are bit positions in the TCP flags byte. Multiple flags can be set
// simultaneously (e.g., SYN+ACK = 0x12, FIN+ACK = 0x11, PSH+ACK = 0x18).

#define TCP_FLAG_FIN      0x01      // FIN: sender has finished sending data
#define TCP_FLAG_SYN      0x02      // SYN: synchronize sequence numbers (start connection)
#define TCP_FLAG_RST      0x04      // RST: reset / abort connection immediately
#define TCP_FLAG_PSH      0x08      // PSH: push data to application without buffering
#define TCP_FLAG_ACK      0x10      // ACK: the ACK number field is valid

// ══════════════════════════════════════════════════════════════════════════
// CONNECTION TRACKING TABLES
// ══════════════════════════════════════════════════════════════════════════
//
// The virtual NAT maintains two connection tracking tables: one for UDP and
// one for TCP. These map the IIgs-side (src_ip, src_port, dst_ip, dst_port)
// 4-tuple to an open SDL3_net socket.
//
// WHY SEPARATE TABLES:
//   UDP is connectionless — each {src_port, dst_ip, dst_port} tuple gets its
//   own datagram socket, which can be reused for multiple datagrams to the
//   same destination. TCP is connection-oriented — each SYN initiates a new
//   stream socket and full 3-way handshake.
//
// LIFECYCLE:
//   UDP: created on first datagram to a {src_port, dst_ip, dst_port} tuple;
//        reused for subsequent datagrams to the same destination; evicted by
//        LRU (least recently used) when the table is full.
//   TCP: created on SYN; transitions connect_pending → established; destroyed
//        on FIN or RST (either direction) or SDL3_net error.

#define VNAT_MAX_CONNS    16        // Maximum simultaneous TCP connections
#define VNAT_MAX_UDP      8         // Maximum simultaneous UDP flows

// ── UDP connection entry ─────────────────────────────────────────────────
//
// One entry per active UDP flow. A "flow" is uniquely identified by the
// (src_port, dst_ip, dst_port) 3-tuple — we do not need src_ip because the
// IIgs only has one IP address.
//
// We create a single NET_DatagramSocket per flow rather than one shared
// socket for all UDP traffic. This is necessary because:
//   - SDL3_net binds a datagram socket to a specific source port.
//   - Multiple flows may be active simultaneously (e.g., DNS lookup from
//     one port, NTP from another).
//   - The OS uses the socket's bound port to route incoming replies.
//
// The last_used timestamp (SDL_GetTicks() milliseconds) enables LRU eviction
// when VNAT_MAX_UDP slots are exhausted.

struct vnat_udp_t {
    NET_DatagramSocket *socket = nullptr;   // SDL3_net datagram (UDP) socket, or nullptr if slot free
    uint32_t src_ip = 0;                    // IIgs source IP (host byte order, big-endian)
    uint16_t src_port = 0;                  // IIgs source port (e.g., ephemeral 1024–65535)
    uint32_t dst_ip = 0;                    // Remote destination IP (may be redirected; see DNS redirect)
    uint16_t dst_port = 0;                  // Remote destination port (e.g., 53=DNS, 123=NTP)
    uint64_t last_used = 0;                 // SDL_GetTicks() at last activity — used for LRU eviction
};

// ── TCP connection entry ─────────────────────────────────────────────────
//
// One entry per active TCP connection. A TCP connection is uniquely
// identified by the (src_port, dst_ip, dst_port) 3-tuple. We do not
// need src_ip because the IIgs only has one IP address.
//
// SEQUENCE NUMBER TRACKING:
//
//   local_seq:   The sequence number we (the virtual server) use when sending
//                data TO the IIgs. Initialized to a pseudo-random value at
//                SYN time (derived from SDL_GetTicks() to avoid collisions
//                with previous connections on the same 4-tuple). Incremented
//                by 1 for each SYN and FIN, and by N for N bytes of data sent.
//
//   peer_seq:    The sequence number we expect NEXT from the IIgs. Initialized
//                to (IIgs's SYN sequence + 1) because the SYN itself consumes
//                one sequence number. Incremented by N for each N bytes of data
//                received from the IIgs, and by 1 for each FIN.
//
//   remote_ack:  The last ACK number the IIgs sent to us. This tells us which
//                of our previously sent bytes the IIgs has confirmed receiving.
//                In a full implementation this would gate retransmission; here
//                it is tracked but not actively used (SDL3_net handles the
//                real ACK with the actual remote server).
//
// STATE MACHINE:
//
//   On SYN from IIgs:
//     - Allocate entry, save 4-tuple and peer_seq = SYN.seq + 1
//     - Start NET_CreateClient() to real remote
//     - Set connect_pending = true
//
//   On NET_WaitUntilConnected() → success (polled in vnat_poll):
//     - Set established = true, connect_pending = false
//     - Send SYN+ACK to IIgs with local_seq; increment local_seq
//
//   On ACK from IIgs (completes 3-way handshake):
//     - Update remote_ack; connection is now fully open
//
//   On data from IIgs (PSH+ACK or ACK with payload):
//     - Forward to remote via NET_WriteToStreamSocket
//     - Advance peer_seq by payload_len
//     - Send ACK back to IIgs
//
//   On data from remote (polled in vnat_poll):
//     - Build PSH+ACK segment to IIgs with up to 1460 bytes
//     - Advance local_seq by bytes sent
//
//   On FIN from IIgs:
//     - Send ACK+FIN back; destroy socket; mark !established
//
//   On remote close (NET_ReadFromStreamSocket returns < 0):
//     - Send FIN+ACK to IIgs; set fin_sent; advance local_seq
//
//   On connect failure:
//     - Send RST to IIgs; destroy socket; zero entry

struct vnat_tcp_t {
    NET_StreamSocket *socket = nullptr;     // SDL3_net stream (TCP) socket, or nullptr if slot free
    NET_Address *resolve_addr = nullptr;    // Retained address from resolution (not currently used; reserved for future cleanup)
    uint32_t src_ip = 0;                    // IIgs source IP (our side)
    uint16_t src_port = 0;                  // IIgs source port (ephemeral)
    uint32_t dst_ip = 0;                    // Remote destination IP
    uint16_t dst_port = 0;                  // Remote destination port (e.g., 80=HTTP, 23=Telnet)
    uint32_t local_seq = 0;                 // Our (gateway→IIgs) next sequence number
    uint32_t remote_ack = 0;                // Last ACK number received from IIgs (tracks IIgs RX progress)
    uint32_t peer_seq = 0;                  // Next expected sequence number from IIgs (IIgs→gateway)
    bool connect_pending = false;           // true: NET_CreateClient() called, awaiting NET_WaitUntilConnected()
    bool established = false;               // true: 3-way handshake complete, data may flow
    bool fin_sent = false;                  // true: we have sent FIN to IIgs (remote closed)
    uint64_t last_used = 0;                 // SDL_GetTicks() at last activity (for future idle-timeout eviction)
};

// ══════════════════════════════════════════════════════════════════════════
// VIRTUAL NAT STATE
// ══════════════════════════════════════════════════════════════════════════
//
// All mutable state for the virtual NAT lives in this single structure.
// The W5100 emulation (w5100.cpp) holds one instance of this struct.
//
// VIRTUAL MAC ADDRESSES:
//
//   our_mac:   The IIgs's MAC address, read from the W5100's SHAR (Source
//              Hardware Address Register). This is what Marinetti programs
//              into the chip and uses as the Ethernet source address for all
//              outbound frames. We need it to address ARP replies and IP
//              responses back to the IIgs.
//
//   gw_mac:    A synthetic MAC address for the virtual gateway/router.
//              We use 02:00:47:57:00:01 ("GW\x00\x01" with locally-
//              administered bit set, bit 1 of first octet = 1). This address
//              is used as the SOURCE of all frames we send to the IIgs (ARP
//              replies, IP packets). The IIgs's ARP cache maps remote IPs
//              to this MAC, which is exactly what we want — all traffic goes
//              "to the gateway" (us).
//
// NETWORK CONFIGURATION:
//
//   our_ip:      IIgs's IPv4 address (from W5100 SIPR register)
//   gateway_ip:  Virtual gateway IP (from W5100 GAR register, e.g. 192.168.1.1)
//   subnet_mask: Subnet mask (from W5100 SUBR register, e.g. 255.255.255.0)
//
//   These are stored in host byte order as 32-bit big-endian values
//   (i.e., 192.168.1.1 = 0xC0A80101). They are updated by vnat_update_config()
//   whenever the W5100 registers change.
//
// BOOT GUARD:
//
//   When Marinetti first starts, it runs a boot-time network initialization
//   sequence (LINKCONNECT). During this sequence it sends DHCP Discover
//   broadcasts, ARP probes, and possibly DNS queries to validate the static
//   IP configuration. If we respond immediately to these probes (e.g., with
//   a DNS answer or ARP reply that wasn't expected), Marinetti can get confused
//   and stall or fail its initialization.
//
//   The boot guard tracks when the last MACRAW SEND occurred. If 5 seconds
//   pass with no SEND activity, we assume Marinetti has finished its boot
//   sequence and we enable full IP response forwarding.
//
//   NOTE: The boot guard is currently DISABLED (boot_guard_active = false).
//   Testing revealed that the guard was incorrectly suppressing DNS responses
//   during the boot sequence itself, causing Marinetti to timeout. ARP replies
//   are always allowed (needed for Marinetti to discover the gateway MAC),
//   and DHCP is silently dropped at the UDP level (see handle_udp), which
//   is sufficient to prevent the problematic boot behaviors without blocking
//   legitimate responses. The boot_guard fields are retained for documentation
//   purposes and potential future re-enablement.
//
// RESPONSE FRAME QUEUE (rx_buf):
//
//   rx_buf is a linear byte buffer that stores complete Ethernet frames
//   waiting to be delivered to the IIgs's W5100 MACRAW RX ring.
//
//   Format: each frame is stored as [length_hi][length_lo][frame_bytes...]
//   This is a 2-byte big-endian length prefix followed by the frame data.
//
//   The buffer uses a simple linear append strategy with reset-on-empty:
//   when rx_count drops to 0, both rx_head and rx_tail are reset to 0,
//   allowing the full buffer to be reused without fragmentation. This works
//   because the W5100 polling in w5100.cpp drains the queue faster than
//   it fills — at normal network speeds, there are rarely more than 1–2
//   frames queued simultaneously.
//
//   If the buffer fills completely (16 KB), new frames are dropped with a
//   warning. At typical 1500-byte MTU, 16 KB holds approximately 10 frames.
//
//   rx_head:  Byte offset of the next write position (after last enqueued frame)
//   rx_tail:  Byte offset of the next read position (start of oldest frame)
//   rx_count: Number of complete frames currently in the buffer

struct vnat_state_t {
    // Virtual MAC addresses (see notes above)
    uint8_t our_mac[6];                     // IIgs's MAC (from W5100 SHAR); destination for all RX frames
    uint8_t gw_mac[6];                      // Synthetic gateway MAC (source of all RX frames we generate)

    // Network configuration mirrored from W5100 registers
    uint32_t our_ip = 0;                    // IIgs IP (W5100 SIPR), big-endian host order (0xC0A80102 = 192.168.1.2)
    uint32_t gateway_ip = 0;               // Gateway IP (W5100 GAR), e.g. 0xC0A80101 = 192.168.1.1
    uint32_t subnet_mask = 0;              // Subnet mask (W5100 SUBR), e.g. 0xFFFFFF00 = /24

    // Boot guard state (disabled — see struct notes above)
    uint64_t last_send_time = 0;            // SDL_GetTicks() of last vnat_send_frame() call
    bool boot_guard_active = true;          // When true, IP responses are suppressed (currently forced false at init)

    // Connection tables
    vnat_udp_t udp_conns[VNAT_MAX_UDP] = {};       // UDP flow table (8 entries, LRU eviction)
    vnat_tcp_t tcp_conns[VNAT_MAX_CONNS] = {};     // TCP connection table (16 entries, no eviction yet)

    // Response frame queue (frames waiting to be delivered to IIgs MACRAW RX)
    uint8_t rx_buf[16384] = {};             // 16 KB linear buffer; each frame: [len:2][data:len]
    uint16_t rx_head = 0;                   // Write cursor: byte offset after last enqueued frame
    uint16_t rx_tail = 0;                   // Read cursor: byte offset of oldest enqueued frame's length prefix
    uint16_t rx_count = 0;                  // Number of complete frames waiting in rx_buf
};

// ══════════════════════════════════════════════════════════════════════════
// PUBLIC API
// ══════════════════════════════════════════════════════════════════════════

// vnat_init: Initialize all state to defaults. Sets gw_mac to the locally-
// administered address 02:00:47:57:00:01 and clears all connection tables and
// the RX buffer. boot_guard_active is forced to false (see boot guard notes).
// Must be called once before any other vnat_* function.
void vnat_init(vnat_state_t *v);

// vnat_reset: Tear down all active connections and clear the RX buffer.
// Called when the W5100 is reset (e.g., emulator reset). All SDL3_net
// sockets are destroyed to avoid resource leaks. Connection tables and
// the RX buffer are zeroed.
void vnat_reset(vnat_state_t *v);

// vnat_update_config: Synchronize the VNAT's view of the W5100 network
// configuration. Called by the W5100 emulation whenever the IIgs writes
// to SHAR (Source Hardware Address Register), SIPR (Source IP Register),
// GAR (Gateway Address Register), or SUBR (Subnet Mask Register).
//
// Parameters:
//   mac      — 6-byte Ethernet MAC (from W5100 SHAR, IIgs's chosen MAC)
//   ip       — IIgs IPv4 address, big-endian 32-bit (from W5100 SIPR)
//   gateway  — Gateway IPv4 address, big-endian 32-bit (from W5100 GAR)
//   subnet   — Subnet mask, big-endian 32-bit (from W5100 SUBR)
void vnat_update_config(vnat_state_t *v,
                        const uint8_t *mac, uint32_t ip,
                        uint32_t gateway, uint32_t subnet);

// vnat_send_frame: Process one outbound MACRAW frame from the IIgs.
// Called by the W5100 emulation when the IIgs writes to the MACRAW TX
// buffer and triggers a SEND command on socket 0.
//
// Parses the Ethernet header's EtherType:
//   0x0806 (ARP)  → handle_arp(): generate proxy ARP reply
//   0x0800 (IP)   → handle_ip() → handle_udp() or handle_tcp()
//   other         → logged and discarded
//
// May enqueue one or more response frames into rx_buf.
//
// Parameters:
//   frame  — pointer to raw Ethernet frame bytes (starting at dst MAC)
//   len    — total frame length in bytes (minimum ETH_HEADER_SIZE = 14)
void vnat_send_frame(vnat_state_t *v, const uint8_t *frame, uint16_t len);

// vnat_recv_frame: Dequeue one inbound Ethernet frame for the IIgs.
// Called by the W5100 emulation when Marinetti reads from the MACRAW RX
// ring (i.e., when the W5100 has pending RX data indicated in its interrupt
// or socket status registers).
//
// Returns the length of the frame written to out_frame, or 0 if no frames
// are queued. If the next queued frame is larger than max_len, it is
// silently skipped (dropped) and 0 is returned — this prevents a large
// crafted frame from overflowing the caller's buffer.
//
// Parameters:
//   out_frame  — caller-provided buffer to receive the Ethernet frame
//   max_len    — size of out_frame in bytes
//
// Returns: number of bytes written (0–max_len), or 0 if queue empty
uint16_t vnat_recv_frame(vnat_state_t *v, uint8_t *out_frame, uint16_t max_len);

// vnat_poll: Drive all active connections. Must be called periodically
// (every emulated frame or CPU cycle burst) to:
//
//   1. Check the boot guard timer and disable the guard after 5 seconds
//      of inactivity (currently a no-op since boot_guard_active = false).
//
//   2. Poll all UDP datagram sockets for incoming replies via
//      NET_ReceiveDatagram(). For each received datagram, craft a UDP
//      response Ethernet frame and enqueue it in rx_buf.
//
//   3. Poll all TCP stream sockets:
//      a. If connect_pending, call NET_WaitUntilConnected(timeout=0).
//         On success, send SYN+ACK to IIgs. On failure, send RST and free.
//      b. If established, call NET_ReadFromStreamSocket() to drain any
//         data the remote server sent. Craft PSH+ACK segments (up to
//         1460 bytes each) and enqueue in rx_buf.
//         If the socket reports remote close (return < 0), send FIN+ACK.
void vnat_poll(vnat_state_t *v);
