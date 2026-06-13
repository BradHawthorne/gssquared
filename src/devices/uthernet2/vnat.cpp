/*
 * vnat.cpp — Virtual NAT implementation
 *
 * ══════════════════════════════════════════════════════════════════════════
 * HOW THIS MODULE FITS INTO THE LARGER SYSTEM
 * ══════════════════════════════════════════════════════════════════════════
 *
 * The Apple IIgs runs Marinetti (its TCP/IP stack). Marinetti uses the
 * Uthernet II card (W5100 chip) in MACRAW mode, which means it builds and
 * parses complete raw Ethernet frames itself. Our W5100 emulation
 * (w5100.cpp) intercepts those frames and hands them to this module.
 *
 * This module acts as a software NAT router / Ethernet bridge:
 *
 *   IIgs (Marinetti)  ←→  W5100 emulation  ←→  vnat.cpp  ←→  SDL3_net  ←→  Internet
 *
 * For every outbound frame from the IIgs, we:
 *   1. Parse the Ethernet header to determine the protocol (ARP or IP).
 *   2. For ARP: synthesize a reply that maps any IP to our gateway MAC.
 *   3. For IP/UDP: open or reuse a datagram socket; send the payload.
 *   4. For IP/TCP: establish a real TCP connection via SDL3_net; proxy data.
 *
 * For every inbound network response, we craft a complete Ethernet frame
 * (with valid Ethernet, IP, UDP/TCP headers and checksums) and place it
 * in the RX queue. The W5100 emulation drains that queue and presents the
 * frames to Marinetti as if they arrived on a real NIC.
 *
 * See vnat.hpp for the full architectural rationale and data structure docs.
 *
 * Copyright (c) 2026 Brad Hawthorne
 * Licensed under GPL-3.0 (matching GSSquared)
 */

#include "vnat.hpp"
#include <cstring>
#include <cstdio>


// ══════════════════════════════════════════════════════════════════════════
// BYTE ORDER HELPERS
// ══════════════════════════════════════════════════════════════════════════
//
// All multi-byte fields in Ethernet, IP, TCP, UDP, and ARP are in network
// byte order, which is big-endian (most significant byte first). x86 and
// ARM hosts are little-endian, so we must explicitly convert when reading
// or writing these fields.
//
// We avoid htons/htonl because:
//   a) The SDL3 environment is not guaranteed to have POSIX <arpa/inet.h>.
//   b) Explicit byte-shifting makes the byte layout visually obvious.
//   c) These trivial functions are inlined by the compiler anyway.
//
// Naming convention:
//   rd16be — read 16-bit big-endian value from byte pointer
//   rd32be — read 32-bit big-endian value from byte pointer
//   wr16be — write 16-bit big-endian value to byte pointer
//   wr32be — write 32-bit big-endian value to byte pointer

static uint16_t rd16be(const uint8_t *p) {
    // p[0] is the high byte (MSB), p[1] is the low byte (LSB)
    return ((uint16_t)p[0] << 8) | p[1];
}
static uint32_t rd32be(const uint8_t *p) {
    // p[0] is the most significant byte, p[3] is the least significant
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
static void wr16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void wr32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}


// ══════════════════════════════════════════════════════════════════════════
// INTERNET CHECKSUM (RFC 1071)
// ══════════════════════════════════════════════════════════════════════════
//
// Both IPv4 headers and TCP/UDP pseudo-headers use the RFC 1071 "Internet
// Checksum": the one's complement sum of all 16-bit words in the data, with
// any carry bits folded back in, then bitwise inverted.
//
// Algorithm:
//   1. Sum all 16-bit big-endian words. If the length is odd, treat the
//      trailing byte as the high byte of a 16-bit word (zero-padded low byte).
//   2. Fold the 32-bit accumulator: add the high 16 bits to the low 16 bits,
//      repeat until no carry remains. This is equivalent to one's complement
//      addition.
//   3. Invert all bits (~sum). The result is the checksum field value.
//
// Verification: if you include the checksum field itself in the sum, a correct
// packet produces 0xFFFF (all ones in one's complement = zero in two's
// complement). We don't verify received checksums — we trust SDL3_net and
// the IIgs stack to have validated them before we see the payloads.
//
// This single function is used for both IP header checksums and the
// TCP/UDP pseudo-header+segment checksums (the caller builds the full
// byte sequence to be summed).
//
// Parameters:
//   data — pointer to data to checksum
//   len  — number of bytes (may be odd)
//
// Returns: 16-bit one's complement checksum (ready to store in header)

static uint16_t ip_checksum(const uint8_t *data, int len) {
    uint32_t sum = 0;

    // Sum all complete 16-bit words
    for (int i = 0; i < len - 1; i += 2)
        sum += ((uint16_t)data[i] << 8) | data[i + 1];

    // If odd length, treat trailing byte as MSB of final 16-bit word
    if (len & 1)
        sum += (uint16_t)data[len - 1] << 8;

    // Fold carry bits: add high word into low word until carry is gone.
    // At most two iterations needed (a 16-bit sum plus a single carry
    // bit produces at most a 17-bit value, so the first fold reduces it
    // to 16 bits plus at most 1 carry, and the second fold finishes).
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    return (uint16_t)(~sum);  // one's complement (bitwise invert)
}


// ══════════════════════════════════════════════════════════════════════════
// RESPONSE FRAME QUEUE
// ══════════════════════════════════════════════════════════════════════════
//
// The response queue (rx_buf in vnat_state_t) is a linear byte buffer
// storing complete Ethernet frames that are ready for the IIgs to receive.
//
// FORMAT: Each frame is prefixed by a 2-byte big-endian length field:
//
//   [ len_hi ][ len_lo ][ ethernet_frame_byte_0 ]...[ ethernet_frame_byte_N ]
//
// The length prefix matches the format the W5100 uses in its own MACRAW RX
// buffer (2-byte length prefix per frame), making it trivial for the W5100
// emulation to copy frames out of our queue into the W5100's RX ring.
//
// BUFFER MANAGEMENT STRATEGY:
//
// We use a simple linear allocation strategy with reset-on-empty:
//   - rx_head tracks the next free byte for writing.
//   - rx_tail tracks the next byte to read.
//   - rx_count tracks how many complete frames are present.
//   - When rx_count drops to 0, both pointers are reset to 0. This
//     reclaims the entire buffer without fragmentation.
//
// This strategy works because:
//   1. The W5100 emulation polls for frames frequently (at CPU burst
//      boundaries), so the queue is typically drained before new frames
//      arrive. In practice, rx_count is almost always 0 or 1.
//   2. If the queue is not empty, rx_head may be non-zero. New frames are
//      appended at rx_head. We check that rx_head + (2 + len) fits within
//      the 16 KB buffer; if not, the frame is dropped with a warning.
//
// OVERFLOW BEHAVIOR:
//
// If the buffer fills up (unlikely in normal operation), new frames are
// dropped. The IIgs will retransmit (for TCP) or the application will
// retry (for UDP). Marinetti's timeout and retry mechanisms handle this.
//
// enqueue_frame: Append one Ethernet frame to the RX queue.
//
// Parameters:
//   frame — pointer to Ethernet frame bytes (starting at destination MAC)
//   len   — frame length in bytes (NOT including the 2-byte length prefix
//           that we will prepend)

static void enqueue_frame(vnat_state_t *v, const uint8_t *frame, uint16_t len) {
    uint16_t total = 2 + len;  // 2 bytes for the length prefix + frame data

    // Reset-on-empty: when the queue is fully drained, reclaim the entire
    // buffer by resetting both pointers to the start. This prevents the
    // head pointer from slowly drifting to the end of the buffer over time.
    if (v->rx_count == 0) {
        v->rx_head = 0;
        v->rx_tail = 0;
    }

    // Guard against buffer overflow — drop the frame rather than corrupt
    // adjacent data. Log the drop for diagnostics.
    if (v->rx_head + total > sizeof(v->rx_buf)) {
        fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " RX buffer full, dropping frame\n");
        return;
    }

    // Write the 2-byte big-endian length prefix
    v->rx_buf[v->rx_head] = (uint8_t)(len >> 8);
    v->rx_buf[v->rx_head + 1] = (uint8_t)(len & 0xFF);

    // Copy the Ethernet frame data immediately after the length prefix
    memcpy(&v->rx_buf[v->rx_head + 2], frame, len);

    // Advance the write cursor past this entry
    v->rx_head += total;
    v->rx_count++;

    // Debug: dump the first 80 bytes of the enqueued frame for diagnostics
    fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " RX %d bytes:", len);
    for (uint16_t d = 0; d < len && d < 80; d++)
        fprintf(stderr, " %02X", frame[d]);
    if (len > 80) fprintf(stderr, "...");
    fprintf(stderr, "\n");
}


// ══════════════════════════════════════════════════════════════════════════
// ETHERNET + IP HEADER BUILDER
// ══════════════════════════════════════════════════════════════════════════
//
// Most response frames we generate share the same Ethernet + IPv4 header
// structure. This helper fills the first 34 bytes (ETH_HEADER_SIZE +
// IP_HEADER_SIZE) of the provided packet buffer and returns the byte
// offset at which the transport-layer payload should begin.
//
// Ethernet header (bytes 0–13):
//   dst MAC = our_mac (the IIgs's MAC — all responses go to the IIgs)
//   src MAC = gw_mac  (our synthetic gateway MAC — all responses come from "gateway")
//   EtherType = 0x0800 (IPv4)
//
// IPv4 header (bytes 14–33):
//   Version/IHL = 0x45  (IPv4, 5 × 4 = 20-byte header, no options)
//   DSCP/ECN    = 0x00  (default traffic class, no ECN)
//   Total Length = IP_HEADER_SIZE + payload_len (set by caller)
//   Identification = ip_id_counter++  (monotonically incrementing per-packet ID;
//                    used by receiving stacks for fragment reassembly, even
//                    though we never actually fragment — some stacks reject
//                    0x0000 as invalid)
//   Flags/Offset = 0x0000  (no DF flag, no fragmentation; we deliberately do NOT
//                  set DF "Don't Fragment" because real DNS responses from
//                  authoritative servers typically don't set DF, and some
//                  Marinetti implementations or intermediate code may use
//                  the DF flag to detect whether a packet is locally generated)
//   TTL  = 64  (standard "sensible default" TTL; Linux uses 64, Windows uses 128)
//   Protocol = caller-supplied (IP_PROTO_TCP or IP_PROTO_UDP)
//   Header Checksum = computed by ip_checksum() over the 20-byte IP header
//   Source IP = src_ip (caller-supplied, the remote's IP as seen by the IIgs)
//   Dest IP   = dst_ip (caller-supplied, always the IIgs's IP = our_ip)
//
// NOTE on ip_id_counter: This is a file-scope (module-level) counter that
// increments for every IP packet we generate. It is NOT per-connection.
// This is standard practice — IP IDs just need to be unique within the
// maximum fragment reassembly time (~30 seconds for a 16-bit counter at
// typical packet rates, which is far below any realistic saturation point).
//
// Parameters:
//   pkt         — output buffer, must be at least ETH_HEADER_SIZE + IP_HEADER_SIZE bytes
//   v           — VNAT state (for our_mac, gw_mac)
//   src_ip      — Source IP for the generated packet (remote host's IP, as seen by IIgs)
//   dst_ip      — Destination IP for the generated packet (IIgs's IP)
//   protocol    — IP protocol number (IP_PROTO_TCP or IP_PROTO_UDP)
//   payload_len — Size of the transport-layer content (TCP or UDP total bytes)
//
// Returns: byte offset (always ETH_HEADER_SIZE + IP_HEADER_SIZE = 34) where
//          the caller should write the transport header

static uint16_t ip_id_counter = 1;  // Global monotonic IP identification counter

static int build_eth_ip(uint8_t *pkt, const vnat_state_t *v,
                        uint32_t src_ip, uint32_t dst_ip,
                        uint8_t protocol, uint16_t payload_len) {
    // ── Ethernet header ─────────────────────────────────────────────────
    memcpy(pkt, v->our_mac, 6);      // dst MAC = IIgs's MAC (recipient of this frame)
    memcpy(pkt + 6, v->gw_mac, 6);   // src MAC = virtual gateway MAC (us)
    wr16be(pkt + 12, ETH_TYPE_IP);   // EtherType = 0x0800 (IPv4)

    // ── IPv4 header ──────────────────────────────────────────────────────
    uint8_t *ip = pkt + ETH_HEADER_SIZE;
    uint16_t total_len = IP_HEADER_SIZE + payload_len;  // IP total = header + everything after it

    ip[0] = 0x45;                     // Version=4 (bits 7-4), IHL=5 (bits 3-0) → 20-byte header
    ip[1] = 0x00;                     // DSCP=0 (best-effort), ECN=0 (no congestion notification)
    wr16be(ip + 2, total_len);        // Total length: IP header + transport header + data
    wr16be(ip + 4, ip_id_counter++);  // Packet ID: unique per datagram, wraps after 65535
    wr16be(ip + 6, 0x0000);           // Flags=0 (no DF, no MF), Fragment Offset=0 (not fragmented)
    ip[8] = 64;                       // TTL=64: standard default; packet lives for 64 router hops
    ip[9] = protocol;                 // Transport protocol: 6=TCP, 17=UDP
    wr16be(ip + 10, 0);               // Checksum placeholder: zero before computing
    wr32be(ip + 12, src_ip);          // Source IP: the remote host (or ourselves masquerading as it)
    wr32be(ip + 16, dst_ip);          // Destination IP: the IIgs

    // Compute and fill in the IP header checksum.
    // The checksum field itself is zero when computing — the RFC 791
    // algorithm treats the checksum field as 0x0000 during calculation.
    wr16be(ip + 10, ip_checksum(ip, IP_HEADER_SIZE));

    return ETH_HEADER_SIZE + IP_HEADER_SIZE;  // Return offset where transport header starts
}


// ══════════════════════════════════════════════════════════════════════════
// ARP HANDLER
// ══════════════════════════════════════════════════════════════════════════
//
// ARP (Address Resolution Protocol, RFC 826) allows an Ethernet host to
// discover the MAC address associated with an IPv4 address. The exchange is:
//
//   Host A sends ARP Request (broadcast):
//     "Who has IP 192.168.1.1? Tell 192.168.1.2 (my MAC is AA:BB:CC:DD:EE:FF)"
//
//   Host B (owner of 192.168.1.1) replies (unicast to A):
//     "192.168.1.1 is at 11:22:33:44:55:66"
//
// In our virtual network, we respond to EVERY ARP Request with our gateway
// MAC (gw_mac), regardless of which IP is being queried. This is Proxy ARP:
// the virtual gateway claims ownership of all IP addresses. This is correct
// because in our virtual network topology, ALL remote hosts are "behind"
// the gateway (us), so the IIgs should send all traffic to the gateway MAC.
//
// After seeing the ARP reply, the IIgs's ARP cache stores:
//   <any IP> → 02:00:47:57:00:01 (gw_mac)
//
// This means every subsequent IP packet the IIgs sends (to any destination)
// will be addressed to gw_mac at the Ethernet layer, which is exactly what
// we want — all traffic comes to us.
//
// WHAT WE DO NOT HANDLE:
//   - ARP Probe (RFC 5227): Gratuitous ARP to verify IP uniqueness. These
//     have TPA=IIgs's own IP and SHA=zeros. We could respond but Marinetti
//     doesn't seem to require it.
//   - ARP Announcements: Gratuitous ARP replies. We ignore these.
//   - Reverse ARP (RARP, op=3/4): Used by diskless hosts; not used by Marinetti.
//
// Parameters:
//   frame — complete inbound Ethernet frame from the IIgs
//   len   — total frame length in bytes

static void handle_arp(vnat_state_t *v, const uint8_t *frame, uint16_t len) {
    // Minimum length check: Ethernet header (14) + ARP payload (28) = 42 bytes
    if (len < ETH_HEADER_SIZE + ARP_SIZE) return;

    const uint8_t *arp = frame + ETH_HEADER_SIZE;  // ARP payload starts after Ethernet header
    uint16_t op = rd16be(arp + 6);                 // ARP opcode: 1=Request, 2=Reply

    if (op != 1) return;  // Only process ARP Requests (op=1); ignore Replies (op=2)

    // Extract the target IP being queried (arp[24..27])
    // The sender wants to know: "Who owns this IP?"
    uint32_t target_ip = rd32be(arp + 24);

    fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " ARP who-has %d.%d.%d.%d?\n",
            (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
            (target_ip >> 8) & 0xFF, target_ip & 0xFF);

    // Build an ARP Reply claiming gw_mac owns the queried IP.
    // We respond to ALL ARP requests with the gateway MAC — this is
    // Proxy ARP and is correct for a virtual NAT where all traffic
    // routes through the single gateway.
    uint8_t reply[ETH_HEADER_SIZE + ARP_SIZE];

    // ── Ethernet header for the reply ────────────────────────────────────
    // dst MAC = IIgs's MAC (unicast reply back to requester)
    // src MAC = gw_mac (we are the gateway answering the query)
    memcpy(reply, v->our_mac, 6);        // dst = requester (IIgs)
    memcpy(reply + 6, v->gw_mac, 6);     // src = us (virtual gateway)
    wr16be(reply + 12, ETH_TYPE_ARP);    // EtherType = 0x0806 (ARP)

    // ── ARP Reply payload ─────────────────────────────────────────────────
    uint8_t *rarp = reply + ETH_HEADER_SIZE;

    wr16be(rarp, 0x0001);                 // Hardware Type = 1 (Ethernet)
    wr16be(rarp + 2, 0x0800);             // Protocol Type = 0x0800 (IPv4)
    rarp[4] = 6;                          // Hardware Address Length = 6 (bytes per MAC)
    rarp[5] = 4;                          // Protocol Address Length = 4 (bytes per IPv4)
    wr16be(rarp + 6, 0x0002);             // Opcode = 2 (ARP Reply)

    // Sender fields (us, the gateway):
    memcpy(rarp + 8, v->gw_mac, 6);      // Sender MAC = gateway MAC (the answer to the query)
    wr32be(rarp + 14, target_ip);         // Sender IP = the IP that was queried (we claim to own it)

    // Target fields (copied from request's sender fields):
    memcpy(rarp + 18, arp + 8, 6);       // Target MAC = requester's MAC (from request's Sender MAC)
    memcpy(rarp + 24, arp + 14, 4);      // Target IP  = requester's IP  (from request's Sender IP)

    enqueue_frame(v, reply, sizeof(reply));

    fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " ARP reply: %d.%d.%d.%d is-at gw_mac\n",
            (target_ip >> 24) & 0xFF, (target_ip >> 16) & 0xFF,
            (target_ip >> 8) & 0xFF, target_ip & 0xFF);
}


// ══════════════════════════════════════════════════════════════════════════
// UDP CONNECTION TABLE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════

// find_udp_conn: Look up an existing UDP flow in the connection table.
//
// A UDP flow is identified by the 3-tuple (src_port, dst_ip, dst_port).
// We don't need src_ip in the lookup because the IIgs has only one IP.
//
// Why we match on dst_ip and dst_port:
//   Multiple flows may be active simultaneously (e.g., DNS on port 53 to
//   different servers, NTP on port 123). Each must use its own socket because
//   SDL3_net binds each datagram socket to a specific source port, and the OS
//   uses the (source_port, dest_port) pair to route replies.
//
// Returns: pointer to the matching vnat_udp_t, or nullptr if not found.

static vnat_udp_t *find_udp_conn(vnat_state_t *v, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port) {
    for (int i = 0; i < VNAT_MAX_UDP; i++) {
        vnat_udp_t *u = &v->udp_conns[i];
        // socket != nullptr means the slot is in use (a nullptr socket = free slot)
        if (u->socket && u->src_port == src_port &&
            u->dst_ip == dst_ip && u->dst_port == dst_port)
            return u;
    }
    return nullptr;
}

// alloc_udp_conn: Allocate a UDP flow entry, evicting the LRU entry if needed.
//
// ALLOCATION STRATEGY:
//   1. Scan for a free slot (socket == nullptr).
//   2. If no free slot exists, evict the entry with the oldest last_used
//      timestamp (Least Recently Used policy). The socket is destroyed
//      before reuse to free the OS resource.
//
// WHY LRU EVICTION:
//   DNS flows are short-lived (one request/response, then idle), so stale
//   entries accumulate quickly. LRU naturally reclaims idle DNS sockets
//   when new ones are needed, without requiring explicit timeout cleanup.
//
// Returns: pointer to an available (zeroed) vnat_udp_t slot.

static vnat_udp_t *alloc_udp_conn(vnat_state_t *v) {
    // First pass: find an unused slot (socket == nullptr → free)
    for (int i = 0; i < VNAT_MAX_UDP; i++) {
        if (!v->udp_conns[i].socket) return &v->udp_conns[i];
    }

    // All slots occupied — evict the least recently used entry
    uint64_t oldest = UINT64_MAX;
    int oldest_idx = 0;
    for (int i = 0; i < VNAT_MAX_UDP; i++) {
        if (v->udp_conns[i].last_used < oldest) {
            oldest = v->udp_conns[i].last_used;
            oldest_idx = i;
        }
    }

    vnat_udp_t *u = &v->udp_conns[oldest_idx];
    if (u->socket) NET_DestroyDatagramSocket(u->socket);  // Free the OS socket resource
    memset(u, 0, sizeof(*u));                              // Zero the slot for reuse
    return u;
}

// handle_udp: Process one outbound UDP datagram from the IIgs.
//
// FRAME LAYOUT EXPECTED:
//   [Ethernet header 14 bytes][IP header N bytes][UDP header 8 bytes][payload]
//
// The IP header length is variable (IHL field encodes it in 32-bit words),
// but we only generate IHL=5 (20 bytes). Received frames from the IIgs may
// also use IHL=5 in practice, but we read the actual IHL from the header
// to be correct.
//
// SPECIAL HANDLING:
//
//   DHCP (dst_port=67, src_port=68, dst_ip=broadcast):
//     Marinetti sends DHCP Discover during boot if configured for DHCP.
//     We silently drop it. Rationale: we cannot act as a DHCP server
//     (we don't know what IP to offer without risk of conflict), and
//     responding with DHCP NAK causes Marinetti to abort its initialization
//     entirely ("LINKCONNECT failed"). Silent drop is the correct behavior:
//     Marinetti's DHCP client will eventually time out and either try again
//     or fall back to the static IP that the user has presumably already
//     configured in Marinetti's TCP/IP settings. The NAK code below
//     (wrapped in `if (false)`) is preserved for documentation and possible
//     future investigation.
//
//   Broadcast/Multicast (dst_ip=0xFFFFFFFF or high nibble=0xE):
//     These cannot be relayed through a NAT. Broadcast addresses
//     (255.255.255.255) are link-local and have no meaning on a different
//     subnet. Multicast addresses (224.0.0.0/4) require IGMP group
//     membership. We drop both silently.
//
//   DNS redirect (dst_port=53, dst_ip=192.x.x.x):
//     If Marinetti is configured to use the LAN router as DNS server
//     (e.g., 192.168.1.1), that query will be sent to the virtual IP but
//     the real router may reject it or be unreachable from the host. We
//     redirect DNS queries addressed to any 192.x.x.x host to 8.8.8.8
//     (Google Public DNS), which is always reachable. The response is then
//     spoofed to appear to come from the original destination IP (so
//     Marinetti's DNS cache stores the correct server association).

static void handle_udp(vnat_state_t *v, const uint8_t *frame, uint16_t len) {
    const uint8_t *ip = frame + ETH_HEADER_SIZE;
    uint8_t ihl = (ip[0] & 0x0F) * 4;  // IP Header Length: low nibble of ip[0] × 4 bytes
    const uint8_t *udp = ip + ihl;      // UDP header starts immediately after IP header

    // Minimum length check: Ethernet (14) + IP (ihl) + UDP header (8) must all fit
    if (len < ETH_HEADER_SIZE + ihl + UDP_HEADER_SIZE) return;

    // Extract IP and UDP header fields
    uint32_t src_ip = rd32be(ip + 12);      // IIgs source IP (typically 192.168.1.x)
    uint32_t dst_ip = rd32be(ip + 16);      // Destination IP (remote server or broadcast)
    uint16_t src_port = rd16be(udp);        // IIgs source port (ephemeral, e.g. 1024–65535)
    uint16_t dst_port = rd16be(udp + 2);    // Remote destination port (53=DNS, 123=NTP, etc.)
    uint16_t udp_len = rd16be(udp + 4);     // UDP Length field: UDP header (8) + payload

    // Guard against malformed "length too small" — prevents underflow in payload_len
    if (udp_len < UDP_HEADER_SIZE) return;

    uint16_t payload_len = udp_len - UDP_HEADER_SIZE;  // Actual application payload size
    const uint8_t *payload = udp + UDP_HEADER_SIZE;    // Pointer to application data

    // Guard against truncated frame (UDP Length says more bytes than the frame contains)
    if (len < ETH_HEADER_SIZE + ihl + udp_len) return;

    // ── DHCP: silently drop ───────────────────────────────────────────────
    // DHCP Discover/Request: src=68, dst=67, dst_ip=broadcast
    // We cannot be a DHCP server without risking conflict with the real LAN.
    // NAK response causes Marinetti to abort; silence causes it to fall back
    // to the static IP already configured in Marinetti. Silent drop is correct.
    if (dst_ip == 0xFFFFFFFF && dst_port == 67 && src_port == 68) {
        return;
    }

    // ── DHCP NAK code (DISABLED) ──────────────────────────────────────────
    // This block is intentionally unreachable (if (false)). It contains the
    // code that would send a DHCP NAK response. It was disabled because:
    //   - A DHCP NAK tells the client "your IP request is refused", which
    //     causes Marinetti's DHCP client to immediately abort and propagate
    //     a LINKCONNECT failure up to the application.
    //   - Silent drop is safer: DHCP client times out and Marinetti falls
    //     through to use its statically-configured IP.
    // Preserved here for reference and future investigation.
    if (false && dst_ip == 0xFFFFFFFF && dst_port == 67 && src_port == 68 && payload_len >= 240) {
        static bool dhcp_nak_sent = false;
        if (dhcp_nak_sent) return;
        uint8_t msg_type = payload[0];
        if (msg_type == 1) {  // DHCP Discover (message type 1)
            dhcp_nak_sent = true;

            // Build BOOTP/DHCP reply packet
            // BOOTP (Bootstrap Protocol) is the wire format for DHCP messages.
            // A DHCP NAK is opcode=2 (reply), DHCP Message Type option = 6.
            uint8_t pkt[600];
            memset(pkt, 0, sizeof(pkt));

            uint8_t bootp[300];
            memset(bootp, 0, sizeof(bootp));
            bootp[0] = 2;                          // op: BOOTREPLY (server→client)
            bootp[1] = 1;                          // htype: Ethernet (hardware type 1)
            bootp[2] = 6;                          // hlen: 6 bytes per MAC address
            memcpy(bootp + 4, payload + 4, 4);     // xid: transaction ID (echo back from Discover)
            memcpy(bootp + 28, payload + 28, 16);  // chaddr: client hardware address (MAC)

            // DHCP magic cookie (RFC 2131): always 99.130.83.99 = 0x63825363
            // This marks the options area as DHCP (not plain BOOTP)
            bootp[236] = 99; bootp[237] = 130; bootp[238] = 83; bootp[239] = 99;

            // DHCP Option 53: Message Type = NAK (value 6)
            // Format: [option_code=53][length=1][value=6]
            bootp[240] = 53; bootp[241] = 1; bootp[242] = 6;
            // DHCP Option 255: End of options
            bootp[243] = 0xFF;

            uint16_t bootp_len = 300;
            uint16_t udp_total = UDP_HEADER_SIZE + bootp_len;

            // Build Ethernet+IP header manually (cannot use build_eth_ip because
            // our_mac may not be set yet during early boot before ARP exchange)
            // Ethernet header: broadcast dst (DHCP replies go to broadcast or client MAC)
            memset(pkt, 0xFF, 6);                   // dst MAC: broadcast FF:FF:FF:FF:FF:FF
            memcpy(pkt + 6, v->gw_mac, 6);          // src MAC: virtual gateway
            wr16be(pkt + 12, ETH_TYPE_IP);

            // IP header
            uint8_t *ip = pkt + ETH_HEADER_SIZE;
            uint16_t ip_total = IP_HEADER_SIZE + udp_total;
            ip[0] = 0x45;                           // Version=4, IHL=5
            wr16be(ip + 2, ip_total);               // Total length
            wr16be(ip + 6, 0x4000);                 // Flags: DF=1 (don't fragment; DHCP servers set this)
            ip[8] = 64;                             // TTL
            ip[9] = IP_PROTO_UDP;
            // Source: gateway IP if known, else fallback to 192.168.1.1
            wr32be(ip + 12, v->gateway_ip ? v->gateway_ip : 0xC0A80101);
            wr32be(ip + 16, 0xFFFFFFFF);            // Destination: broadcast
            wr16be(ip + 10, ip_checksum(ip, IP_HEADER_SIZE));

            // UDP header
            uint8_t *rudp = pkt + ETH_HEADER_SIZE + IP_HEADER_SIZE;
            wr16be(rudp, 67);                       // src port: DHCP server (67)
            wr16be(rudp + 2, 68);                   // dst port: DHCP client (68)
            wr16be(rudp + 4, udp_total);            // UDP length
            wr16be(rudp + 6, 0);                    // Checksum: 0 = omitted (valid for IPv4 UDP)
            memcpy(rudp + UDP_HEADER_SIZE, bootp, bootp_len);

            enqueue_frame(v, pkt, ETH_HEADER_SIZE + IP_HEADER_SIZE + udp_total);
            fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " DHCP NAK sent\n");
        }
        return;
    }

    // ── Drop broadcast and multicast ──────────────────────────────────────
    // 0xFFFFFFFF = limited broadcast (this network)
    // 0xExxxxxxx = multicast (224.0.0.0 – 239.255.255.255, high nibble = E)
    // These cannot be relayed through a NAT to the real network.
    if (dst_ip == 0xFFFFFFFF || (dst_ip & 0xF0000000) == 0xE0000000) {
        return;
    }

    fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " UDP %d.%d.%d.%d:%d -> %d.%d.%d.%d:%d (%d bytes)\n",
            (src_ip >> 24) & 0xFF, (src_ip >> 16) & 0xFF,
            (src_ip >> 8) & 0xFF, src_ip & 0xFF, src_port,
            (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
            (dst_ip >> 8) & 0xFF, dst_ip & 0xFF, dst_port,
            payload_len);

    // ── Find or create UDP flow entry ─────────────────────────────────────
    vnat_udp_t *conn = find_udp_conn(v, src_port, dst_ip, dst_port);
    if (!conn) {
        // No existing flow — allocate a new entry (or evict LRU)
        conn = alloc_udp_conn(v);
        conn->src_ip = src_ip;
        conn->src_port = src_port;
        conn->dst_ip = dst_ip;       // Original destination IP (may be redirected for DNS below)
        conn->dst_port = dst_port;

        // NET_CreateDatagramSocket(NULL, 0): bind to any available local port.
        // SDL3_net allocates an ephemeral source port; the OS routes replies
        // back to this socket based on (local_port, remote_ip, remote_port).
        conn->socket = NET_CreateDatagramSocket(NULL, 0);
        if (!conn->socket) {
            fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " Failed to create UDP socket\n");
            return;
        }
    }
    conn->last_used = SDL_GetTicks();  // Update LRU timestamp

    // ── DNS redirect ──────────────────────────────────────────────────────
    // If the IIgs is configured to use a LAN router as DNS (192.x.x.x) but
    // we can't reach that router directly from the host, redirect to Google
    // Public DNS (8.8.8.8). We keep conn->dst_ip as the ORIGINAL destination
    // so that the response frame appears to come from the expected IP (see
    // build_udp_response for how reply_src_ip is set from conn->dst_ip).
    uint32_t relay_ip = dst_ip;  // Actual IP we'll send the datagram to
    if (dst_port == 53 && (dst_ip & 0xFF000000) == 0xC0000000) {  // port 53 + 192.x.x.x
        relay_ip = 0x08080808;  // 8.8.8.8 = Google Public DNS
        fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " DNS redirected to 8.8.8.8\n");
    }

    // Convert the relay IP to a dotted-decimal string for SDL3_net resolution.
    // NET_ResolveHostname() accepts both IP strings and hostnames; using the
    // IP string directly avoids a DNS lookup for the DNS server's address.
    char ip_str[20];
    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
             (relay_ip >> 24) & 0xFF, (relay_ip >> 16) & 0xFF,
             (relay_ip >> 8) & 0xFF, relay_ip & 0xFF);

    // Resolve the address synchronously (100ms timeout).
    // For IP-address strings this typically completes immediately.
    // For hostnames it may block up to 100ms — acceptable for a once-per-flow cost.
    NET_Address *dest = NET_ResolveHostname(ip_str);
    if (dest && NET_WaitUntilResolved(dest, 100) == 1) {
        NET_SendDatagram(conn->socket, dest, dst_port, payload, payload_len);
    }
    if (dest) NET_UnrefAddress(dest);  // Always release the address object
}

// build_udp_response: Craft an Ethernet/IP/UDP frame carrying a UDP reply
// and enqueue it in the RX buffer for delivery to the IIgs.
//
// IMPORTANT — REPLY SOURCE IP SPOOFING:
//   We use conn->dst_ip (the original destination the IIgs was sending to)
//   as the source IP in the reply frame, NOT the actual responder's IP
//   (from_ip). This is necessary because:
//   - When DNS is redirected (192.168.1.1 → 8.8.8.8), the actual reply
//     comes from 8.8.8.8, but Marinetti's DNS client expects the reply to
//     come from 192.168.1.1 (the server it queried). If the source IP
//     doesn't match, Marinetti drops the reply as unsolicited.
//   - For non-redirected flows, conn->dst_ip == the actual responder's IP,
//     so spoofing it makes no difference.
//
// UDP CHECKSUM:
//   The UDP checksum is mandatory in this implementation (even though IPv4
//   allows omitting it with checksum=0x0000) because Marinetti validates
//   incoming UDP checksums. Computing the checksum requires a 12-byte
//   pseudo-header (see vnat.hpp for layout) prepended to the UDP segment.
//   We build the pseudo-header in a temporary stack buffer and feed the
//   combined pseudo-header + UDP header + payload through the same one's
//   complement summation used by ip_checksum().
//
//   Special case: if the computed checksum is 0x0000, we transmit 0xFFFF
//   instead. In one's complement arithmetic, both 0x0000 and 0xFFFF
//   represent "zero", but 0x0000 in the UDP checksum field means "checksum
//   omitted" — so a computed result of 0x0000 must be transmitted as 0xFFFF
//   to convey "checksum present and valid, value happens to be all-ones."
//
// Parameters:
//   conn        — UDP flow entry for this reply
//   payload     — application-layer payload bytes (DNS response, etc.)
//   payload_len — length of payload in bytes
//   from_ip     — actual IP that sent the response (used for logging)
//   from_port   — port the response came from (used as UDP source port)

static void build_udp_response(vnat_state_t *v, vnat_udp_t *conn,
                               const uint8_t *payload, int payload_len,
                               uint32_t from_ip, uint16_t from_port) {
    uint8_t pkt[1600];  // 1600 bytes covers max Ethernet MTU (1500) + headers (34)
    uint16_t udp_total = UDP_HEADER_SIZE + payload_len;  // Total UDP segment size

    // Use the original destination IP (conn->dst_ip) as the reply's source IP,
    // not the actual responder's IP. This ensures DNS-redirected replies appear
    // to come from the server Marinetti queried (see REPLY SOURCE IP SPOOFING above).
    uint32_t reply_src_ip = conn->dst_ip;

    // Build Ethernet + IP headers. dst_ip for the response is the IIgs's IP
    // (conn->src_ip), and src_ip is the remote server's IP (reply_src_ip).
    int offset = build_eth_ip(pkt, v, reply_src_ip, conn->src_ip,
                              IP_PROTO_UDP, udp_total);

    // ── UDP header ────────────────────────────────────────────────────────
    uint8_t *udp = pkt + offset;
    wr16be(udp, from_port);           // UDP Source Port: remote server's port
    wr16be(udp + 2, conn->src_port);  // UDP Destination Port: IIgs's ephemeral port
    wr16be(udp + 4, udp_total);       // UDP Length: header (8) + payload
    wr16be(udp + 6, 0);               // Checksum: placeholder (0 before computation)

    // Copy application payload after UDP header
    memcpy(udp + UDP_HEADER_SIZE, payload, payload_len);

    // ── UDP checksum computation ──────────────────────────────────────────
    // The checksum covers: [pseudo-header 12 bytes][UDP header 8 bytes][payload N bytes]
    //
    // Pseudo-header layout (12 bytes, never transmitted — checksum-only construct):
    //   [0..3]   Source IP (same as IP header source)
    //   [4..7]   Destination IP (same as IP header destination)
    //   [8]      Zero padding byte
    //   [9]      Protocol number (17 = UDP)
    //   [10..11] UDP Length (same as UDP header length field)
    //
    // Purpose: binding the UDP checksum to the IP addresses prevents a datagram
    // from being accepted by the wrong socket if misrouted at the IP layer.
    uint8_t pseudo[12];
    wr32be(pseudo, reply_src_ip);       // Pseudo-header Source IP
    wr32be(pseudo + 4, conn->src_ip);   // Pseudo-header Destination IP
    pseudo[8] = 0;                      // Zero padding
    pseudo[9] = IP_PROTO_UDP;           // Protocol = 17
    wr16be(pseudo + 10, udp_total);     // UDP Length (same value as UDP header field)

    // Sum pseudo-header and UDP segment (header + payload) as 16-bit words
    uint32_t sum = 0;
    for (int i = 0; i < 12; i += 2)
        sum += ((uint16_t)pseudo[i] << 8) | pseudo[i + 1];
    for (int i = 0; i < udp_total - 1; i += 2)
        sum += ((uint16_t)udp[i] << 8) | udp[i + 1];
    // Odd-byte padding: treat trailing byte as MSB (same as ip_checksum)
    if (udp_total & 1)
        sum += (uint16_t)udp[udp_total - 1] << 8;
    // Fold carry bits into 16-bit result
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    uint16_t cksum = (uint16_t)(~sum);
    // Special case: 0x0000 means "checksum omitted" in UDP; use 0xFFFF instead
    if (cksum == 0) cksum = 0xFFFF;
    wr16be(udp + 6, cksum);  // Fill in the computed checksum

    uint16_t total = offset + udp_total;
    enqueue_frame(v, pkt, total);

    fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " UDP response %d bytes from %d.%d.%d.%d:%d (cksum=$%04X)\n",
            payload_len,
            (reply_src_ip >> 24) & 0xFF, (reply_src_ip >> 16) & 0xFF,
            (reply_src_ip >> 8) & 0xFF, reply_src_ip & 0xFF, from_port, cksum);
}


// ══════════════════════════════════════════════════════════════════════════
// TCP CONNECTION TABLE MANAGEMENT
// ══════════════════════════════════════════════════════════════════════════

// find_tcp_conn: Look up an existing TCP connection by the IIgs-side 3-tuple
// (src_port, dst_ip, dst_port). The socket field being non-null indicates an
// active entry.
//
// Returns: pointer to matching vnat_tcp_t, or nullptr if not found.

static vnat_tcp_t *find_tcp_conn(vnat_state_t *v, uint16_t src_port, uint32_t dst_ip, uint16_t dst_port) {
    for (int i = 0; i < VNAT_MAX_CONNS; i++) {
        vnat_tcp_t *t = &v->tcp_conns[i];
        // A slot is active if it has a socket OR a pending connect
        if (t->socket && t->src_port == src_port &&
            t->dst_ip == dst_ip && t->dst_port == dst_port)
            return t;
    }
    return nullptr;
}

// alloc_tcp_conn: Allocate a free TCP connection slot.
//
// A slot is free if both socket == nullptr AND connect_pending == false.
// (During the async connect phase, socket may still be nullptr while
// connect_pending is true — that slot is NOT free.)
//
// Unlike UDP, there is no LRU eviction for TCP. A full connection table
// (VNAT_MAX_CONNS = 16) means new SYN requests are silently dropped.
// Rationale: 16 simultaneous TCP connections far exceeds what a 1 MHz 65816
// running Marinetti can maintain at reasonable throughput. If needed, a
// future enhancement could evict half-open or TIME_WAIT connections.
//
// Returns: pointer to a free vnat_tcp_t, or nullptr if table is full.

static vnat_tcp_t *alloc_tcp_conn(vnat_state_t *v) {
    for (int i = 0; i < VNAT_MAX_CONNS; i++) {
        if (!v->tcp_conns[i].socket && !v->tcp_conns[i].connect_pending)
            return &v->tcp_conns[i];
    }
    return nullptr;  // Table full; caller drops the SYN
}

// build_tcp_packet: Craft an Ethernet/IP/TCP segment and enqueue it for the IIgs.
//
// This is the core TCP frame generation function. It is called to send:
//   - SYN+ACK: complete the three-way handshake after connection is established
//   - ACK: acknowledge data received from the IIgs
//   - PSH+ACK: deliver data from the remote server to the IIgs
//   - FIN+ACK: signal end of data stream (graceful close)
//   - RST: abort the connection (connect failed or protocol error)
//
// SEQUENCE AND ACKNOWLEDGMENT NUMBERS:
//   local_seq:  Written to the sequence number field. This is the byte offset
//               in the stream from gateway to IIgs. It must be incremented by
//               the caller AFTER calling this function for SYN, FIN, and data
//               segments (each of these "consumes" sequence space).
//
//   peer_seq:   Written to the acknowledgment number field. This tells the IIgs
//               we have received all bytes from the IIgs through peer_seq - 1,
//               and we're expecting peer_seq next.
//
// WINDOW SIZE:
//   We advertise a fixed 8192-byte receive window. This is the receive buffer
//   space we're willing to accept from the IIgs. In practice, Marinetti sends
//   modest amounts of data (HTTP requests, Telnet keystrokes) and SDL3_net
//   has its own buffering, so a fixed window is sufficient. A real TCP stack
//   would track actual buffer fill and adjust this dynamically.
//
// TCP CHECKSUM:
//   Computed over the same pseudo-header structure as UDP (src IP, dst IP,
//   zero byte, protocol=6, TCP segment length). For TCP, zero checksum is
//   NOT valid — unlike UDP, TCP has no "checksum omitted" encoding. We do
//   not apply the 0→0xFFFF substitution here.
//
// Parameters:
//   conn        — TCP connection entry (provides 4-tuple and sequence numbers)
//   flags       — TCP control flags byte (TCP_FLAG_SYN, TCP_FLAG_ACK, etc.)
//   payload     — data to include in the segment (nullptr if flags-only)
//   payload_len — length of payload in bytes (0 for control segments)

static void build_tcp_packet(vnat_state_t *v, vnat_tcp_t *conn,
                             uint8_t flags, const uint8_t *payload, int payload_len) {
    uint8_t pkt[1600];
    uint16_t tcp_total = TCP_HEADER_SIZE + payload_len;  // Total TCP segment size

    // Build Ethernet + IP headers.
    // src_ip for the response = conn->dst_ip (the remote server's IP, as seen by IIgs)
    // dst_ip for the response = conn->src_ip (the IIgs's IP)
    int offset = build_eth_ip(pkt, v, conn->dst_ip, conn->src_ip,
                              IP_PROTO_TCP, tcp_total);

    // ── TCP header ────────────────────────────────────────────────────────
    uint8_t *tcp = pkt + offset;
    memset(tcp, 0, TCP_HEADER_SIZE);  // Zero all fields before selectively filling

    wr16be(tcp, conn->dst_port);          // Source Port: remote server's port (e.g., 80, 23)
    wr16be(tcp + 2, conn->src_port);      // Destination Port: IIgs's ephemeral port
    wr32be(tcp + 4, conn->local_seq);     // Sequence Number: our current position in gateway→IIgs stream
    wr32be(tcp + 8, conn->peer_seq);      // Acknowledgment Number: next byte expected from IIgs
    tcp[12] = 0x50;                       // Data Offset: 0x5 << 4 = 5 words = 20 bytes (no options)
    tcp[13] = flags;                      // Control flags byte (SYN, ACK, FIN, PSH, RST, etc.)
    wr16be(tcp + 14, 8192);               // Window Size: 8 KB receive window (fixed; sufficient for IIgs speeds)
    wr16be(tcp + 16, 0);                  // Checksum: placeholder (computed below)
    wr16be(tcp + 18, 0);                  // Urgent Pointer: 0 (URG flag not set)

    // Copy payload data after fixed-size TCP header
    if (payload_len > 0)
        memcpy(tcp + TCP_HEADER_SIZE, payload, payload_len);

    // ── TCP checksum computation ──────────────────────────────────────────
    // Identical structure to UDP pseudo-header, but protocol=6 and length
    // is the TCP segment length (not the UDP length field).
    // Unlike UDP, there is no "checksum omitted" special case for TCP —
    // checksum is always required.
    uint8_t pseudo[12];
    wr32be(pseudo, conn->dst_ip);           // Pseudo-header Source IP (gateway/remote side)
    wr32be(pseudo + 4, conn->src_ip);       // Pseudo-header Destination IP (IIgs)
    pseudo[8] = 0;                          // Zero padding
    pseudo[9] = IP_PROTO_TCP;              // Protocol = 6
    wr16be(pseudo + 10, tcp_total);         // TCP segment length (header + data)

    uint32_t sum = 0;
    for (int i = 0; i < 12; i += 2)
        sum += ((uint16_t)pseudo[i] << 8) | pseudo[i + 1];
    for (int i = 0; i < tcp_total - 1; i += 2)
        sum += ((uint16_t)tcp[i] << 8) | tcp[i + 1];
    // Odd-byte padding if tcp_total is odd
    if (tcp_total & 1)
        sum += (uint16_t)tcp[tcp_total - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    wr16be(tcp + 16, (uint16_t)(~sum));  // Store the computed checksum

    uint16_t total = offset + tcp_total;
    enqueue_frame(v, pkt, total);
}

// handle_tcp: Process one inbound TCP segment from the IIgs.
//
// TCP SEGMENT PARSING:
//   The IP header length (IHL) is variable; we extract it from ip[0]'s
//   low nibble (multiply by 4). The TCP header's own data offset field
//   (tcp[12] high nibble × 4) gives the TCP header size including any
//   options — we respect this when extracting payload data, so TCP
//   options (MSS, timestamps) are transparently skipped even though we
//   don't process them.
//
//   Payload length = IP Total Length - IP Header Length - TCP Header Length
//   We compute this from the IP Total Length field rather than the frame
//   length to handle cases where the Ethernet frame has padding bytes.
//
// THREE-WAY HANDSHAKE (handled here and in vnat_poll):
//
//   Step 1 (IIgs sends SYN, handled here):
//     - Parse the target IP and port from the segment.
//     - Allocate a connection table entry.
//     - Seed local_seq with a pseudo-random value derived from the current
//       time × prime multiplier to avoid sequence number collisions across
//       connections and emulator restarts.
//     - Start the real TCP connection with NET_CreateClient() (async).
//     - Set connect_pending = true; return without sending a SYN+ACK yet.
//
//   Step 2 (vnat_poll detects connection established):
//     - NET_WaitUntilConnected() returns 1.
//     - Send SYN+ACK with our local_seq; increment local_seq (SYN consumes 1 seq#).
//     - Set established = true.
//
//   Step 3 (IIgs sends ACK, handled here):
//     - This is a pure ACK with no payload. We update remote_ack.
//     - The connection is now fully established and data can flow.
//
// DATA TRANSFER:
//   IIgs → remote: PSH+ACK or ACK with payload.
//     - Forward payload to real server via NET_WriteToStreamSocket.
//     - Advance peer_seq by payload_len.
//     - Send ACK back to IIgs (pure ACK, no data).
//
//   Remote → IIgs: polled in vnat_poll, not processed here.
//
// CONNECTION TEARDOWN:
//   IIgs sends FIN (graceful close from IIgs side):
//     - Increment peer_seq (FIN consumes 1 seq#).
//     - Send ACK+FIN back (combined: acknowledge FIN, send our own FIN).
//     - Increment local_seq (our FIN consumes 1 seq#).
//     - Destroy the SDL3_net socket.
//     - Mark established = false.
//   Note: We do not implement the full TCP TIME_WAIT state. The slot is
//   immediately freed for reuse. This is acceptable for an emulator where
//   Marinetti controls the upper layers.
//
// Parameters:
//   frame — complete inbound Ethernet frame from the IIgs
//   len   — total frame length in bytes

static void handle_tcp(vnat_state_t *v, const uint8_t *frame, uint16_t len) {
    const uint8_t *ip = frame + ETH_HEADER_SIZE;
    uint8_t ihl = (ip[0] & 0x0F) * 4;  // IP Header Length: low nibble × 4 bytes
    const uint8_t *tcp = ip + ihl;      // TCP segment starts after IP header

    // Minimum length check: frame must contain at least a full TCP header
    if (len < ETH_HEADER_SIZE + ihl + TCP_HEADER_SIZE) return;

    // Extract IP header fields
    uint32_t src_ip = rd32be(ip + 12);     // IIgs source IP
    uint32_t dst_ip = rd32be(ip + 16);     // Remote destination IP
    uint16_t src_port = rd16be(tcp);       // IIgs source (ephemeral) port
    uint16_t dst_port = rd16be(tcp + 2);   // Remote destination port
    uint32_t seq = rd32be(tcp + 4);        // IIgs's sequence number (bytes sent by IIgs)
    uint32_t ack = rd32be(tcp + 8);        // IIgs's acknowledgment number (what IIgs has received from us)
    uint8_t data_off = (tcp[12] >> 4) * 4; // TCP header size including options (high nibble × 4)
    uint8_t flags = tcp[13];              // Control flags byte

    // Compute payload length from IP Total Length to handle Ethernet padding correctly.
    // Frame padding bytes (Ethernet min frame = 64 bytes) should not be misinterpreted as payload.
    uint16_t ip_total = rd16be(ip + 2);    // IP Total Length from IP header
    int payload_len = ip_total - ihl - data_off;  // Bytes of application data in this segment
    const uint8_t *payload = tcp + data_off;       // Application data starts after TCP header + options

    // ── SYN: initiate new connection ──────────────────────────────────────
    if (flags & TCP_FLAG_SYN) {
        // Look up existing entry first — a retransmitted SYN should reuse
        // (or reinitialize) the existing entry rather than leak sockets.
        vnat_tcp_t *conn = find_tcp_conn(v, src_port, dst_ip, dst_port);
        if (!conn) {
            conn = alloc_tcp_conn(v);
            if (!conn) {
                // Table full — silently drop. Marinetti will retransmit.
                fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " TCP: no free connection slots\n");
                return;
            }
            memset(conn, 0, sizeof(*conn));  // Zero-initialize the new entry
        }

        // Populate the connection entry
        conn->src_ip = src_ip;
        conn->src_port = src_port;
        conn->dst_ip = dst_ip;
        conn->dst_port = dst_port;

        // peer_seq = SYN's sequence number + 1
        // In TCP, the SYN segment "consumes" one sequence number even though
        // it carries no data. So the first data byte from the IIgs will have
        // sequence number (seq + 1).
        conn->peer_seq = seq + 1;

        // local_seq: pseudo-random initial sequence number.
        // Using SDL_GetTicks() × prime + offset gives a value that changes
        // across connections and across emulator restarts, reducing the risk
        // of sequence number collision with a previous connection on the same
        // 4-tuple (important for some Marinetti implementations that cache
        // connection state).
        conn->local_seq = (uint32_t)SDL_GetTicks() * 12345 + 1000;
        conn->last_used = SDL_GetTicks();

        // Resolve the destination IP to an SDL3_net address and start
        // the async TCP connection. NET_CreateClient() is non-blocking;
        // we poll NET_WaitUntilConnected() in vnat_poll() each tick.
        char ip_str[20];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                 (dst_ip >> 24) & 0xFF, (dst_ip >> 16) & 0xFF,
                 (dst_ip >> 8) & 0xFF, dst_ip & 0xFF);

        fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " TCP SYN %s:%d\n", ip_str, dst_port);

        // Resolve hostname (blocks up to 5 seconds — IP strings resolve instantly).
        // 5 second timeout is generous but a TCP connect to an unresolvable host
        // would fail anyway; the async connect path handles rejection cleanly.
        NET_Address *addr = NET_ResolveHostname(ip_str);
        if (addr && NET_WaitUntilResolved(addr, 5000) == 1) {
            conn->socket = NET_CreateClient(addr, dst_port);
            conn->connect_pending = true;  // SYN+ACK will be sent in vnat_poll() when connected
        }
        if (addr) NET_UnrefAddress(addr);

        return;  // SYN handler done; do NOT send SYN+ACK yet (wait for connect)
    }

    // ── Subsequent segments: find the existing connection ─────────────────
    vnat_tcp_t *conn = find_tcp_conn(v, src_port, dst_ip, dst_port);
    if (!conn) return;  // No matching connection; silently drop (may be stale segment)

    conn->last_used = SDL_GetTicks();

    // ── ACK: update our view of what the IIgs has received ───────────────
    if (flags & TCP_FLAG_ACK) {
        // remote_ack tracks how far along the IIgs is in acknowledging our data.
        // We store it but don't use it for retransmission (SDL3_net handles that).
        conn->remote_ack = ack;
    }

    // ── Data segment: forward IIgs → remote ──────────────────────────────
    if (payload_len > 0 && conn->established && conn->socket) {
        // Forward the application payload to the real remote server.
        // SDL3_net handles all TCP framing, ACKing, and retransmission on
        // the real network side — we only need to handle the virtual side.
        NET_WriteToStreamSocket(conn->socket, payload, payload_len);

        // Advance peer_seq by the number of data bytes consumed.
        // This ensures our next ACK to the IIgs announces the correct
        // expected sequence number.
        conn->peer_seq += payload_len;

        // Send a pure ACK back to the IIgs (no PSH, no data).
        // This tells Marinetti we've "received" the data and it can
        // slide its send window forward.
        build_tcp_packet(v, conn, TCP_FLAG_ACK, nullptr, 0);
    }

    // ── FIN: graceful connection close from IIgs side ─────────────────────
    if (flags & TCP_FLAG_FIN) {
        // FIN consumes one sequence number (like SYN), so we acknowledge it
        // by incrementing peer_seq before sending our ACK.
        conn->peer_seq++;

        // Send combined ACK+FIN: acknowledge the IIgs's FIN and simultaneously
        // announce that we are also done sending (half-close on both sides).
        // A real implementation would support half-close (IIgs done, remote
        // still sending), but for the applications Marinetti runs, combined
        // FIN+ACK on receiving the IIgs's FIN is appropriate.
        build_tcp_packet(v, conn, TCP_FLAG_ACK | TCP_FLAG_FIN, nullptr, 0);

        // Our FIN also consumes one local sequence number
        conn->local_seq++;

        // Tear down the SDL3_net socket to the real server
        if (conn->socket) {
            NET_DestroyStreamSocket(conn->socket);
            conn->socket = nullptr;
        }
        conn->established = false;
        // Note: we do NOT memset the entry to zero here. It will be reused
        // by alloc_tcp_conn() on the next SYN to the same or a different host,
        // since both socket==nullptr and connect_pending==false after teardown.
    }
}


// ══════════════════════════════════════════════════════════════════════════
// IP PACKET DISPATCHER
// ══════════════════════════════════════════════════════════════════════════
//
// handle_ip: Route an IPv4 packet to the appropriate protocol handler.
//
// This function sits between the Ethernet demultiplexer (vnat_send_frame)
// and the transport protocol handlers. It extracts the IP Protocol field
// (ip[9]) and dispatches to handle_udp(), handle_tcp(), or (TODO) handle_icmp().
//
// We do NOT validate the IP header checksum on inbound frames from the IIgs.
// The IIgs (Marinetti) is a trusted source — it runs on our emulated hardware
// and cannot inject malformed packets from outside. Validating the checksum
// would be redundant overhead.
//
// We also do NOT handle IP fragmentation. Marinetti always sends packets
// within the 1500-byte Ethernet MTU, so fragmentation never occurs in
// practice. Supporting fragmentation would require a fragment reassembly
// buffer — considerable complexity for no practical benefit.
//
// Parameters:
//   frame — complete inbound Ethernet frame (starting at dst MAC byte 0)
//   len   — frame length in bytes

static void handle_ip(vnat_state_t *v, const uint8_t *frame, uint16_t len) {
    // Minimum length: Ethernet header + minimum IP header
    if (len < ETH_HEADER_SIZE + IP_HEADER_SIZE) return;

    const uint8_t *ip = frame + ETH_HEADER_SIZE;
    uint8_t protocol = ip[9];  // Transport protocol: ICMP=1, TCP=6, UDP=17

    switch (protocol) {
        case IP_PROTO_UDP:
            handle_udp(v, frame, len);
            break;
        case IP_PROTO_TCP:
            handle_tcp(v, frame, len);
            break;
        case IP_PROTO_ICMP:
            // ICMP: not yet implemented. A useful future enhancement would
            // be to respond to Echo Request (ping) with Echo Reply, allowing
            // Marinetti's "ping" diagnostic tool to work. For now, ICMP
            // packets are silently discarded — Marinetti does not require
            // ICMP for TCP/UDP connectivity.
            break;
    }
}


// ══════════════════════════════════════════════════════════════════════════
// PUBLIC API IMPLEMENTATION
// ══════════════════════════════════════════════════════════════════════════

// vnat_init: One-time initialization of the VNAT state.
//
// Sets all fields to zero via memset, then configures:
//   - last_send_time: current tick (so boot guard timer starts from now)
//   - boot_guard_active: false (disabled; see vnat_state_t notes in header)
//   - gw_mac: 02:00:47:57:00:01 (locally-administered "GW\x00\x01")
//
// The locally-administered bit (bit 1 of the first octet, 0x02) indicates
// this MAC was not assigned by the hardware manufacturer — it was chosen
// by software. Setting this bit prevents collisions with real manufacturer-
// assigned MACs (which all have bit 1 = 0 in the first octet).
// "47" = ASCII 'G', "57" = ASCII 'W' — a mnemonic for "GateWay".

void vnat_init(vnat_state_t *v) {
    memset(v, 0, sizeof(*v));
    v->last_send_time = SDL_GetTicks();
    v->boot_guard_active = false;  // Disabled — was blocking DNS responses during boot

    // Gateway MAC: 02:00:47:57:00:01 (locally-administered, "GW" mnemonic)
    // The IIgs's ARP cache will map all remote IPs to this MAC address.
    v->gw_mac[0] = 0x02;  // Locally-administered bit set (not a manufacturer MAC)
    v->gw_mac[1] = 0x00;
    v->gw_mac[2] = 0x47;  // 'G' (ASCII 71 = 0x47)
    v->gw_mac[3] = 0x57;  // 'W' (ASCII 87 = 0x57)
    v->gw_mac[4] = 0x00;
    v->gw_mac[5] = 0x01;
}

// vnat_reset: Tear down all active connections and flush the RX queue.
//
// Called when the W5100 receives a software reset (MR register bit 7 set),
// or when the emulator resets. Properly destroys all open SDL3_net sockets
// to prevent resource leaks, then zeros all connection entries.
//
// After this call, the VNAT is in the same state as after vnat_init(),
// except that our_mac, gw_mac, and the network config (our_ip, gateway_ip,
// subnet_mask) retain their previously configured values — the W5100
// emulation will re-program these if needed.

void vnat_reset(vnat_state_t *v) {
    // Destroy all active UDP sockets
    for (int i = 0; i < VNAT_MAX_UDP; i++) {
        if (v->udp_conns[i].socket) {
            NET_DestroyDatagramSocket(v->udp_conns[i].socket);
        }
        memset(&v->udp_conns[i], 0, sizeof(v->udp_conns[i]));
    }

    // Destroy all active TCP sockets
    for (int i = 0; i < VNAT_MAX_CONNS; i++) {
        if (v->tcp_conns[i].socket) {
            NET_DestroyStreamSocket(v->tcp_conns[i].socket);
        }
        memset(&v->tcp_conns[i], 0, sizeof(v->tcp_conns[i]));
    }

    // Flush the RX frame queue
    v->rx_head = 0;
    v->rx_tail = 0;
    v->rx_count = 0;
}

// vnat_update_config: Synchronize VNAT's network configuration from W5100 registers.
//
// Called by the W5100 emulation (w5100.cpp) whenever the IIgs software
// writes to any of these W5100 configuration registers:
//   SHAR (0x0009, 6 bytes): Source Hardware Address Register — the IIgs's MAC
//   SIPR (0x000F, 4 bytes): Source IP Register — the IIgs's IP address
//   GAR  (0x0001, 4 bytes): Gateway Address Register — the router's IP
//   SUBR (0x0005, 4 bytes): Subnet Mask Register
//
// These values are mirrored here so we can use them when building response
// frames (e.g., our_mac for ARP reply destination, our_ip for IP dst address).
//
// Parameters:
//   mac     — 6-byte IIgs MAC from W5100 SHAR register
//   ip      — IIgs IP as 32-bit big-endian from W5100 SIPR
//   gateway — Gateway IP as 32-bit big-endian from W5100 GAR
//   subnet  — Subnet mask as 32-bit big-endian from W5100 SUBR

void vnat_update_config(vnat_state_t *v,
                        const uint8_t *mac, uint32_t ip,
                        uint32_t gateway, uint32_t subnet) {
    memcpy(v->our_mac, mac, 6);
    v->our_ip = ip;
    v->gateway_ip = gateway;
    v->subnet_mask = subnet;
}

// vnat_send_frame: Process one outbound MACRAW frame from the IIgs.
//
// This is the entry point for all traffic flowing FROM the IIgs TO the network.
// The W5100 emulation calls this function when the IIgs writes a complete
// Ethernet frame into the W5100's MACRAW TX buffer and issues a SEND command.
//
// FRAME VALIDATION:
//   We require at least ETH_HEADER_SIZE (14) bytes — the minimum to read the
//   EtherType field. Frames shorter than 14 bytes are silently discarded.
//
// BOOT GUARD:
//   All sends update last_send_time. When boot_guard_active is true, IP packets
//   are suppressed (ARP is always processed). Currently boot_guard_active is
//   forced false at init, so the guard has no effect.
//
// DISPATCH:
//   EtherType 0x0806 → handle_arp(): generate proxy ARP reply
//   EtherType 0x0800 → handle_ip() → handle_udp() / handle_tcp()
//   Other             → logged and discarded (e.g., 0x86DD = IPv6, which we don't support)
//
// Parameters:
//   frame — pointer to raw Ethernet frame bytes (dst MAC at byte 0)
//   len   — total frame length in bytes

void vnat_send_frame(vnat_state_t *v, const uint8_t *frame, uint16_t len) {
    if (len < ETH_HEADER_SIZE) return;  // Too short to contain even an Ethernet header

    uint16_t ethertype = rd16be(frame + 12);  // EtherType field at bytes 12–13

    // Update boot guard timer on every send (currently unused since guard is disabled)
    v->last_send_time = SDL_GetTicks();

    switch (ethertype) {
        case ETH_TYPE_ARP:
            // ARP is always processed, even if boot guard were active.
            // The IIgs needs ARP responses to discover the gateway MAC
            // before any IP communication can begin.
            handle_arp(v, frame, len);
            break;
        case ETH_TYPE_IP:
            // IP traffic is gated by boot_guard_active (currently always false).
            if (!v->boot_guard_active)
                handle_ip(v, frame, len);
            break;
        default:
            // Unknown EtherType — log and discard.
            // Common unexpected types: 0x86DD (IPv6), 0x8100 (VLAN), 0x0814 (RARP).
            // Marinetti is IPv4-only, so IPv6 would be unexpected.
            fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " Unknown ethertype $%04X\n", ethertype);
            break;
    }
}

// vnat_recv_frame: Dequeue one complete Ethernet frame for the IIgs.
//
// Called by the W5100 emulation when it needs to deliver a pending frame
// into the W5100's MACRAW RX ring buffer. The W5100 MACRAW RX protocol
// prepends each frame with a 2-byte length, which is exactly what our
// rx_buf stores — so the W5100 emulation can copy directly.
//
// OVERSIZED FRAME HANDLING:
//   If the next queued frame is larger than max_len (the caller's buffer),
//   the frame is silently dropped (rx_tail and rx_count are advanced past it).
//   This prevents a large crafted frame from overflowing the W5100's RX ring.
//   In practice, our generated frames are all well within the 1500-byte MTU,
//   so this guard should never trigger.
//
// BUFFER RESET:
//   rx_tail is advanced by (2 + frame_len) after each read. When rx_count
//   drops to 0, the next call to enqueue_frame() will reset both pointers
//   to 0, reclaiming the full buffer.
//
// Parameters:
//   out_frame — caller-provided buffer to receive the Ethernet frame bytes
//   max_len   — size of out_frame in bytes (usually ~2 KB for W5100 RX ring)
//
// Returns: number of bytes written to out_frame (0 if queue empty)

uint16_t vnat_recv_frame(vnat_state_t *v, uint8_t *out_frame, uint16_t max_len) {
    if (v->rx_count == 0) return 0;  // Queue empty; nothing to deliver

    // Read the 2-byte big-endian length prefix at rx_tail
    uint16_t frame_len = rd16be(&v->rx_buf[v->rx_tail]);

    if (frame_len > max_len) {
        // Frame too large for caller's buffer — skip it entirely.
        // Advance tail past this entry (2-byte header + frame data).
        v->rx_tail += 2 + frame_len;
        v->rx_count--;
        return 0;
    }

    // Copy the frame data (immediately after the 2-byte length prefix)
    // into the caller's buffer
    memcpy(out_frame, &v->rx_buf[v->rx_tail + 2], frame_len);

    // Advance the read cursor past this entry
    v->rx_tail += 2 + frame_len;
    v->rx_count--;

    return frame_len;
}

// vnat_poll: Drive all active connections forward.
//
// Must be called regularly (every emulated CPU burst, ~1000–10000 cycles)
// to make progress on asynchronous operations:
//   1. Boot guard expiry check (no-op currently; boot_guard_active = false)
//   2. UDP: drain incoming datagrams from all active datagram sockets
//   3. TCP: check pending connects; drain incoming data from established connections
//
// BOOT GUARD TIMER:
//   If boot_guard_active is true (re-enabled in a future version), this code
//   checks whether 5 seconds have elapsed since the last vnat_send_frame()
//   call. If so, it disables the guard and logs. The 5-second threshold was
//   chosen empirically to outlast Marinetti's boot-time DHCP retry cycle
//   (typically 3 retries × ~1 second = ~3 seconds).
//
// UDP POLLING:
//   NET_ReceiveDatagram() is non-blocking. If no datagram is available on
//   the socket, it returns false immediately. We loop until it returns false,
//   which drains all pending datagrams in one poll cycle. Each received
//   datagram is converted to an Ethernet/IP/UDP frame via build_udp_response()
//   and enqueued for the IIgs.
//
//   The sender's IP address is extracted from the NET_Datagram's addr field
//   via NET_GetAddressString() and parsed as a dotted-decimal IPv4 string.
//   This is slightly clunky but SDL3_net doesn't expose a numeric IP accessor.
//
// TCP POLLING:
//   For connect_pending sockets: NET_WaitUntilConnected(timeout=0) is a
//   non-blocking poll (returns immediately). On success (return=1), we
//   complete the 3-way handshake by sending SYN+ACK to the IIgs.
//   On failure (return=-1, e.g., connection refused), we send RST and free
//   the connection entry.
//
//   For established sockets: NET_ReadFromStreamSocket() reads up to 1460
//   bytes (TCP MSS for Ethernet). We cap at 1460 to stay within one
//   maximum-size Ethernet frame (1500 bytes MTU - 20 IP - 20 TCP = 1460 data).
//   Each read result becomes a PSH+ACK segment to the IIgs. We advance
//   local_seq by the number of bytes sent.
//   Return value < 0 from NET_ReadFromStreamSocket() means remote close.

void vnat_poll(vnat_state_t *v) {
    // ── Boot guard timer ──────────────────────────────────────────────────
    // Check if enough quiet time has passed to lift the boot guard.
    // Currently no-op because boot_guard_active is always false.
    if (v->boot_guard_active) {
        if (SDL_GetTicks() - v->last_send_time > 5000) {
            v->boot_guard_active = false;
            fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " Boot guard lifted, full networking enabled\n");
        }
    }

    // ── Poll UDP datagram sockets for incoming replies ─────────────────────
    for (int i = 0; i < VNAT_MAX_UDP; i++) {
        vnat_udp_t *u = &v->udp_conns[i];
        if (!u->socket) continue;  // Skip free slots

        // Drain all pending datagrams from this socket (non-blocking loop)
        NET_Datagram *dgram = nullptr;
        while (NET_ReceiveDatagram(u->socket, &dgram) && dgram) {
            if (v->boot_guard_active) {
                // Boot guard active: consume and discard responses to prevent
                // premature delivery to Marinetti during boot sequence.
                NET_DestroyDatagram(dgram);
                dgram = nullptr;
                continue;
            }

            // Extract the sender's IP address from the datagram source address.
            // SDL3_net provides the address as a string (e.g., "8.8.8.8").
            // We parse it into a 32-bit integer for use in the frame header.
            uint32_t from_ip = 0;
            const char *str = NET_GetAddressString(dgram->addr);
            if (str) {
                int a, b, c, d;
                if (sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                    from_ip = ((uint32_t)a << 24) | ((uint32_t)b << 16) |
                              ((uint32_t)c << 8) | d;
                }
            }

            // Build and enqueue the UDP response frame for the IIgs
            build_udp_response(v, u, dgram->buf, dgram->buflen,
                               from_ip, dgram->port);
            NET_DestroyDatagram(dgram);
            dgram = nullptr;
        }
    }

    // ── Poll TCP stream sockets ───────────────────────────────────────────
    for (int i = 0; i < VNAT_MAX_CONNS; i++) {
        vnat_tcp_t *t = &v->tcp_conns[i];
        if (!t->socket) continue;  // Skip free and not-yet-connected slots

        // ── Pending connect: check if the async TCP connect has completed ──
        if (t->connect_pending) {
            // NET_WaitUntilConnected(timeout=0): non-blocking poll.
            //   Returns  1 if connected successfully.
            //   Returns  0 if still in progress.
            //   Returns -1 if connection failed (refused, timeout, etc.).
            int status = NET_WaitUntilConnected(t->socket, 0);
            if (status == 1) {
                // Connection to remote server established.
                // Complete the TCP 3-way handshake: send SYN+ACK to IIgs.
                t->connect_pending = false;
                t->established = true;
                build_tcp_packet(v, t, TCP_FLAG_SYN | TCP_FLAG_ACK, nullptr, 0);
                // SYN consumes one sequence number; advance local_seq.
                t->local_seq++;
                fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " TCP connected, sent SYN-ACK\n");
            } else if (status < 0) {
                // Connection failed — inform the IIgs by sending RST.
                // RST abortively closes the half-open connection on the IIgs side.
                build_tcp_packet(v, t, TCP_FLAG_RST, nullptr, 0);
                NET_DestroyStreamSocket(t->socket);
                memset(t, 0, sizeof(*t));  // Free the slot for reuse
                fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " TCP connect failed\n");
            }
            // status == 0: still connecting; check again next poll
            continue;
        }

        // ── Established: drain incoming data from remote server ────────────
        if (t->established) {
            // Read up to 1460 bytes per poll iteration.
            // 1460 = 1500 (Ethernet MTU) - 20 (IP header) - 20 (TCP header).
            // Staying within one MTU prevents the IIgs from needing to handle
            // IP fragmentation (which we don't generate anyway).
            uint8_t buf[1460];
            int got = NET_ReadFromStreamSocket(t->socket, buf, sizeof(buf));
            if (got > 0) {
                // Data received from remote — send PSH+ACK to IIgs.
                // PSH tells the IIgs's TCP stack to push the data up to the
                // application (Marinetti) immediately rather than buffering.
                build_tcp_packet(v, t, TCP_FLAG_ACK | TCP_FLAG_PSH, buf, got);
                t->local_seq += got;  // Advance our sequence number by bytes sent
            } else if (got < 0) {
                // Remote has closed the connection (orderly close or error).
                // Initiate graceful teardown: send FIN+ACK to IIgs.
                // FIN+ACK = "I've received all your data AND I'm done sending."
                build_tcp_packet(v, t, TCP_FLAG_FIN | TCP_FLAG_ACK, nullptr, 0);
                t->local_seq++;  // FIN consumes one sequence number
                t->fin_sent = true;
                // Note: we don't destroy the socket here. We wait for the IIgs
                // to send its FIN in response (handled in handle_tcp), which
                // will perform the final teardown. This implements half of the
                // TCP four-way FIN exchange from our side.
                fprintf(stderr, "[VNAT %llu]", (unsigned long long)SDL_GetTicks()); fprintf(stderr, " TCP remote closed\n");
            }
            // got == 0: no data available right now; check again next poll
        }
    }
}
