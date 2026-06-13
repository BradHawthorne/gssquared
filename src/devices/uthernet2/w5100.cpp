/*
 *   W5100 Chip Emulation
 *
 *   Register-level emulation of the Wiznet W5100 TCP/IP controller.
 *   Socket operations are bridged to the host OS via SDL3_net.
 *
 *   The W5100 has a 32KB internal address space accessed indirectly
 *   through ADDR_HI/ADDR_LO/DATA port registers. The chip handles
 *   TCP/IP in hardware, so from the Apple II's perspective it just
 *   writes socket parameters and commands, then reads status/data.
 *
 *   We emulate this by mapping socket commands to SDL3_net calls:
 *   - OPEN: initialize socket state
 *   - CONNECT: NET_CreateClient() (async)
 *   - SEND: NET_WriteToStreamSocket() from TX ring buffer
 *   - RECV: advance RX read pointer (data already buffered)
 *   - CLOSE/DISCON: NET_DestroyStreamSocket()
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#include "w5100.hpp"
#include <cstring>
#include <cstdio>


// ════════════════════════════════════════════════════════════════════
// BUFFER GEOMETRY: TMSR/RMSR DECODING
// ════════════════════════════════════════════════════════════════════
//
// The W5100 divides its fixed 8KB TX pool and 8KB RX pool among four
// sockets using the TMSR and RMSR registers.  Each register encodes
// four 2-bit fields — one per socket — as:
//
//   TMSR[7:6] = socket 3 TX size code
//   TMSR[5:4] = socket 2 TX size code
//   TMSR[3:2] = socket 1 TX size code
//   TMSR[1:0] = socket 0 TX size code
//
// Size code mapping:
//   0b00 → 1 KB  (1024 bytes)
//   0b01 → 2 KB  (2048 bytes)   ← default (0x55 = 01 01 01 01)
//   0b10 → 4 KB  (4096 bytes)
//   0b11 → 8 KB  (8192 bytes)
//
// The total allocated across all sockets must not exceed 8KB.  The
// datasheet warns that exceeding 8KB produces undefined behavior; we
// do not enforce this constraint and follow what the guest programs.
//
// decode_buf_size(): converts a 2-bit code to a byte count.
// The formula (1024 << bits) exploits the power-of-2 relationship:
//   bits=0 → 1024<<0 = 1024
//   bits=1 → 1024<<1 = 2048
//   bits=2 → 1024<<2 = 4096
//   bits=3 → 1024<<3 = 8192

static uint16_t decode_buf_size(uint8_t bits) {
    return (uint16_t)1024 << bits;
}

// recalc_buffer_geometry(): recompute per-socket buffer base/size/mask.
//
// Called whenever TMSR or RMSR is written.  Iterates sockets 0–3 in
// order, accumulating the offsets within the TX (0x4000–0x5FFF) and
// RX (0x6000–0x7FFF) regions.
//
// The tx_mask and rx_mask fields are (size - 1), exploiting the
// invariant that all sizes are powers of two.  The mask is used in
// the ring-buffer physical-address formula:
//
//   phys = socket_base + (ring_pointer & socket_mask)
//
// This is equivalent to modulo-division by socket_size but costs only
// a single AND instruction.  It works because (pointer & mask) cycles
// through [0, size-1] as pointer increments, regardless of how many
// times the pointer has wrapped around 0xFFFF.
//
// Example with S0 at rx_base=0x6000, rx_size=2048, rx_mask=0x07FF:
//   pointer=0x0002  →  phys = 0x6000 + (0x0002 & 0x07FF) = 0x6002
//   pointer=0x0800  →  phys = 0x6000 + (0x0800 & 0x07FF) = 0x6000  (wrapped)
//   pointer=0x8003  →  phys = 0x6000 + (0x8003 & 0x07FF) = 0x6003  (still correct)

static void recalc_buffer_geometry(w5100_state_t *w) {
    uint8_t tmsr = w->mem[W5100_TMSR];
    uint8_t rmsr = w->mem[W5100_RMSR];

    uint16_t tx_offset = 0;  // Running byte offset within the 8KB TX pool
    uint16_t rx_offset = 0;  // Running byte offset within the 8KB RX pool

    for (int i = 0; i < W5100_NUM_SOCKETS; i++) {
        // Extract 2-bit size code for socket i from bits [2i+1:2i]
        uint16_t ts = decode_buf_size((tmsr >> (i * 2)) & 0x03);
        uint16_t rs = decode_buf_size((rmsr >> (i * 2)) & 0x03);

        // Socket i's TX ring buffer occupies mem[tx_base .. tx_base+tx_size-1]
        w->sockets[i].tx_base = W5100_TX_BASE + tx_offset;
        w->sockets[i].tx_size = ts;
        w->sockets[i].tx_mask = ts - 1;  // power-of-2, so mask = size - 1

        // Socket i's RX ring buffer occupies mem[rx_base .. rx_base+rx_size-1]
        w->sockets[i].rx_base = W5100_RX_BASE + rx_offset;
        w->sockets[i].rx_size = rs;
        w->sockets[i].rx_mask = rs - 1;

        // Advance offsets so socket i+1 starts immediately after socket i
        tx_offset += ts;
        rx_offset += rs;
    }
}

// ── Helpers for 16-bit register access ──────────────────────────────
//
// All W5100 multi-byte registers are big-endian (network byte order):
// the high byte is at the lower address.  These helpers encapsulate
// that convention so call sites don't need to remember byte order.

static uint16_t read16(const uint8_t *base, uint16_t offset) {
    return ((uint16_t)base[offset] << 8) | base[offset + 1];
}

static void write16(uint8_t *base, uint16_t offset, uint16_t val) {
    base[offset] = (uint8_t)(val >> 8);
    base[offset + 1] = (uint8_t)(val & 0xFF);
}

// ── Socket register helpers ─────────────────────────────────────────
//
// Returns a pointer to the base of socket sn's 256-byte register block.
// All Sn_* offsets defined in the header are relative to this pointer.

static uint8_t *sock_reg(w5100_state_t *w, int sn) {
    return &w->mem[W5100_SREG_BASE + sn * W5100_SREG_SIZE];
}

// ── Close a socket's network resources ──────────────────────────────
//
// Destroys all SDL3_net objects associated with a socket and clears
// the socket struct's pointers.  Safe to call on an already-closed
// socket (all pointer checks are null guards).
//
// Note: this function only tears down host-OS resources.  It does NOT
// modify any W5100 register values (Sn_SR, Sn_IR, etc.).  The callers
// are responsible for updating register state after calling this.

static void close_socket_net(w5100_socket_t *s) {
    if (s->tcp_socket) {
        NET_DestroyStreamSocket(s->tcp_socket);
        s->tcp_socket = nullptr;
    }
    if (s->udp_socket) {
        NET_DestroyDatagramSocket(s->udp_socket);
        s->udp_socket = nullptr;
    }
    if (s->resolve_addr) {
        // resolve_addr is a temporary reference kept only during the
        // CONNECT command.  Under normal operation it is released in
        // exec_socket_cmd() immediately after NET_CreateClient(); this
        // path handles the error case where close_socket_net is called
        // while a resolution is still outstanding.
        NET_UnrefAddress(s->resolve_addr);
        s->resolve_addr = nullptr;
    }
    s->connect_pending = false;
}

// ════════════════════════════════════════════════════════════════════
// SOCKET COMMAND EXECUTION
// ════════════════════════════════════════════════════════════════════
//
// exec_socket_cmd() is the core of the W5100 emulation.  It is called
// whenever the guest writes a non-zero value to any Sn_CR register.
// It dispatches on the command byte and drives the socket state machine.
//
// On the real chip the command register self-clears to 0x00 when the
// command completes.  We replicate this at the bottom of the function.
// Software should poll Sn_CR == 0 before issuing the next command, but
// fast Apple II programs may skip the poll for common cases.
//
// COMMAND FLOW SUMMARY
// ────────────────────
// OPEN     → allocate resources, set initial status
// LISTEN   → set status = LISTEN (TCP server; Phase 2)
// CONNECT  → resolve IP, NET_CreateClient (async), set SYNSENT
// DISCON   → teardown established TCP, set DISCON interrupt
// CLOSE    → unconditional teardown, set CLOSED
// SEND     → drain TX ring buffer to network
// RECV     → no-op (RX_RSR computed dynamically from pointers)

static void exec_socket_cmd(w5100_state_t *w, int sn, uint8_t cmd) {
    uint8_t *sr = sock_reg(w, sn);
    w5100_socket_t *s = &w->sockets[sn];
    uint8_t protocol = sr[W5100_Sn_MR] & W5100_Sn_MR_PROTO_MASK;

    switch (cmd) {

        // ── OPEN ─────────────────────────────────────────────────────────
        // Tears down any previous network connection, resets all TX/RX ring
        // buffer pointers to 0, then opens the requested protocol.
        //
        // Pointer reset rationale: ring pointers are raw 16-bit counters
        // that never stop incrementing on the real chip.  Resetting to 0
        // on OPEN is correct per the datasheet; the guest always re-opens
        // before using a socket.  The mask-based physical address formula
        // is pointer-value-agnostic, so any starting value would work, but
        // 0 is the documented initial state.
        case W5100_Sn_CR_OPEN: {
            close_socket_net(s);

            // Reset TX/RX pointers (raw 16-bit counters, wrap at 0xFFFF)
            write16(sr, W5100_Sn_TX_RD, 0);
            write16(sr, W5100_Sn_TX_WR, 0);
            write16(sr, W5100_Sn_RX_RD, 0);
            write16(sr, W5100_Sn_RX_WR, 0);

            switch (protocol) {
                case W5100_Sn_MR_TCP:
                    // TCP sockets start in INIT, not ESTABLISHED.
                    // The guest must follow up with CONNECT (client) or
                    // LISTEN (server) to reach a usable state.
                    sr[W5100_Sn_SR] = W5100_SOCK_INIT;
                    break;

                case W5100_Sn_MR_UDP: {
                    // UDP sockets are immediately usable after OPEN.
                    // We bind a datagram socket to the configured source
                    // port so incoming datagrams can reach us.
                    uint16_t src_port = read16(sr, W5100_Sn_PORT);
                    s->udp_socket = NET_CreateDatagramSocket(NULL, src_port);
                    if (s->udp_socket) {
                        sr[W5100_Sn_SR] = W5100_SOCK_UDP;
                        // DEBUG: fprintf(stderr, "[U2] Socket %d: UDP open on port %d\n", sn, src_port);
                    } else {
                        // DEBUG: fprintf(stderr, "[U2] Socket %d: UDP open failed\n", sn);
                        sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                    }
                    break;
                }

                case W5100_Sn_MR_IPRAW:
                    // IPRAW: raw IP packets.  We record the status but do
                    // not open an OS raw socket (requires root on most hosts).
                    // Guest can send/receive if VNAT handles the protocol.
                    sr[W5100_Sn_SR] = W5100_SOCK_IPRAW;
                    break;

                case W5100_Sn_MR_MACRAW:
                    // MACRAW is valid only on Socket 0 per W5100 datasheet.
                    // All raw Ethernet frames pass through the VNAT engine,
                    // which provides ARP, ICMP, and TCP/UDP bridging without
                    // requiring host raw-socket privileges.
                    //
                    // We snapshot the current IP/MAC/gateway/subnet registers
                    // into VNAT so it can respond to ARP requests and route
                    // outgoing packets correctly.
                    if (sn == 0) {
                        sr[W5100_Sn_SR] = W5100_SOCK_MACRAW;
                        // Update VNAT with current network config from W5100 registers
                        {
                            auto r32 = [](const uint8_t *p) -> uint32_t {
                                return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
                                       ((uint32_t)p[2]<<8)|p[3];
                            };
                            vnat_update_config(&w->vnat,
                                &w->mem[W5100_SHAR],
                                r32(&w->mem[W5100_SIPR]),
                                r32(&w->mem[W5100_GAR]),
                                r32(&w->mem[W5100_SUBR]));
                        }
                        // DEBUG: fprintf(stderr, "[U2] Socket 0: MACRAW open (VNAT active)\n");
                    }
                    break;

                default:
                    sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                    break;
            }
            break;
        }

        // ── LISTEN ───────────────────────────────────────────────────────
        // TCP server mode: transition from INIT to LISTEN.
        // On the real chip the W5100 then waits for an incoming SYN and
        // autonomously completes the three-way handshake.
        // Phase 2 TODO: accept incoming SDL3_net connections.
        case W5100_Sn_CR_LISTEN: {
            if (sr[W5100_Sn_SR] == W5100_SOCK_INIT && protocol == W5100_Sn_MR_TCP) {
                sr[W5100_Sn_SR] = W5100_SOCK_LISTEN;
                // TODO phase 2: accept incoming connections. Requires NET_CreateServer() to bind a
                // listening port, then NET_AcceptClient() polled in process_sockets() to detect
                // incoming connections. Socket transitions: SOCK_INIT → SOCK_LISTEN → SOCK_SYNRECV
                // → SOCK_ESTABLISHED. Sn_IR_CON interrupt must be set when the connection completes.
            }
            break;
        }

        // ── CONNECT ──────────────────────────────────────────────────────
        // Initiates an outbound TCP connection to Sn_DIPR:Sn_DPORT.
        // The connect is asynchronous: NET_CreateClient() returns immediately
        // with a socket handle; the three-way handshake completes in the
        // background.  We set connect_pending = true and poll for completion
        // in process_sockets() via NET_WaitUntilConnected(sock, 0).
        //
        // The resolution step (NET_ResolveHostname + NET_WaitUntilResolved)
        // is synchronous with a 5-second timeout.  For IPv4 literals this
        // completes instantly; for hostnames it may take tens of milliseconds.
        // That latency is acceptable because CONNECT is issued only once per
        // session, not in a hot loop.
        case W5100_Sn_CR_CONNECT: {
            // W5100 datasheet: CONNECT is only valid from SOCK_INIT state with TCP protocol.
            // Re-issuing CONNECT on an already-connecting (SYNSENT) or established socket is
            // silently dropped rather than returning an error, matching real hardware behavior
            // where the command register is write-and-forget.
            if (sr[W5100_Sn_SR] != W5100_SOCK_INIT || protocol != W5100_Sn_MR_TCP)
                break;

            // Read destination IP and port from socket registers
            uint8_t *dipr = &sr[W5100_Sn_DIPR];
            uint16_t dport = read16(sr, W5100_Sn_DPORT);

            // Format IP as string for SDL3_net
            char ip_str[20];
            snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                     dipr[0], dipr[1], dipr[2], dipr[3]);

            // DEBUG: fprintf(stderr, "[Uthernet2] Socket %d connecting to %s:%d\n", sn, ip_str, dport);

            // Resolve hostname (synchronous for IP addresses)
            s->resolve_addr = NET_ResolveHostname(ip_str);
            if (!s->resolve_addr) {
                // DEBUG: fprintf(stderr, "[Uthernet2] Socket %d: resolve failed\n", sn);
                sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                sr[W5100_Sn_IR] |= W5100_Sn_IR_TIMEOUT;
                break;
            }

            // 5-second DNS timeout balances two concerns: too short and Marinetti gets spurious
            // SOCK_CLOSED on slow DNS (the IIgs retries, but the user sees a delay); too long and
            // the emulator freezes visibly (this is a synchronous call on the main thread).
            // 5s matches typical Marinetti retry intervals.
            if (NET_WaitUntilResolved(s->resolve_addr, 5000) < 0) {
                // DEBUG: fprintf(stderr, "[Uthernet2] Socket %d: resolve timeout\n", sn);
                NET_UnrefAddress(s->resolve_addr);
                s->resolve_addr = nullptr;
                sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                sr[W5100_Sn_IR] |= W5100_Sn_IR_TIMEOUT;
                break;
            }

            // Create TCP client connection (async).
            // NET_CreateClient returns immediately; the socket transitions
            // through SYNSENT → ESTABLISHED as the handshake completes.
            // We release resolve_addr immediately — NET_CreateClient holds
            // its own reference to the address if it needs it.
            s->tcp_socket = NET_CreateClient(s->resolve_addr, dport);
            NET_UnrefAddress(s->resolve_addr);
            s->resolve_addr = nullptr;

            if (!s->tcp_socket) {
                // DEBUG: fprintf(stderr, "[Uthernet2] Socket %d: connect failed\n", sn);
                sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                sr[W5100_Sn_IR] |= W5100_Sn_IR_TIMEOUT;
                break;
            }

            // SYNSENT: SYN has been sent, waiting for SYN-ACK.
            // process_sockets() will poll NET_WaitUntilConnected(sock, 0)
            // each time the guest reads Sn_SR or Sn_RX_RSR, advancing to
            // ESTABLISHED (or CLOSED on failure) asynchronously.
            sr[W5100_Sn_SR] = W5100_SOCK_SYNSENT;
            s->connect_pending = true;
            break;
        }

        // ── DISCON ───────────────────────────────────────────────────────
        // Graceful TCP close: on a real chip this initiates the FIN/ACK
        // four-way handshake and transitions through FIN_WAIT→TIME_WAIT.
        // In emulation we close immediately and signal DISCON to the guest.
        // This is acceptable because SDL3_net handles graceful teardown
        // on NET_DestroyStreamSocket.
        case W5100_Sn_CR_DISCON: {
            if (sr[W5100_Sn_SR] == W5100_SOCK_ESTABLISHED) {
                close_socket_net(s);
                sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                sr[W5100_Sn_IR] |= W5100_Sn_IR_DISCON;
            }
            break;
        }

        // ── CLOSE ────────────────────────────────────────────────────────
        // Unconditional close: tear down all resources, set CLOSED.
        // No interrupt is generated (unlike DISCON).  This is the normal
        // cleanup path when the guest is done with a socket and wants to
        // reuse it or leave it idle.
        case W5100_Sn_CR_CLOSE: {
            close_socket_net(s);
            sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
            break;
        }

        // ── SEND ─────────────────────────────────────────────────────────
        // Transmit all data in the TX ring buffer between TX_RD and TX_WR.
        //
        // RING BUFFER DRAIN ALGORITHM
        // ───────────────────────────
        // The host CPU has written (TX_WR - TX_RD) bytes into the TX ring
        // buffer and then advanced TX_WR.  SEND tells the chip to drain
        // everything from TX_RD up to TX_WR.
        //
        // We linearize the ring into a flat tmp[] buffer using the mask
        // formula:  phys = tx_base + ((tx_rd + offset) & tx_mask)
        //
        // After successful transmission, TX_RD is advanced to TX_WR,
        // marking those bytes as consumed (free space returns to tx_size).
        // The SENDOK interrupt bit is set to notify the guest.
        //
        // MACRAW SEND
        // ───────────
        // The TX data is a complete Ethernet frame (including 14-byte
        // Ethernet header: dst_mac[6] + src_mac[6] + ethertype[2]).
        // We pass it to vnat_send_frame() which dispatches based on
        // ethertype (ARP, IPv4) and protocol (ICMP, TCP, UDP).
        // We also refresh VNAT's config snapshot in case SIPR/GAR/SUBR
        // changed since OPEN (e.g., Marinetti obtains IP via DHCP after
        // opening the MACRAW socket).
        //
        // TCP SEND
        // ────────
        // NET_WriteToStreamSocket() writes to the kernel's TCP send buffer.
        // It may return before all bytes are sent over the wire, but the
        // data is safely buffered in the kernel.  We advance TX_RD on any
        // non-error return, matching the W5100 behavior of advancing the
        // read pointer after handing data to the TCP stack.
        //
        // UDP SEND
        // ────────
        // Each SEND command produces one UDP datagram.  The destination
        // is read from Sn_DIPR/Sn_DPORT at send time, allowing the guest
        // to change the destination between sends on the same socket
        // (standard UDP practice).  We resolve synchronously with a 1-
        // second timeout (shorter than CONNECT because UDP is typically
        // used with IP literals).
        case W5100_Sn_CR_SEND: {
            uint16_t tx_rd = read16(sr, W5100_Sn_TX_RD);
            uint16_t tx_wr = read16(sr, W5100_Sn_TX_WR);
            uint16_t to_send = tx_wr - tx_rd;  // unsigned 16-bit: handles pointer wrap

            if (to_send == 0) {
                // Vacuous success: TX_RD == TX_WR means zero bytes to send. The real W5100
                // behavior is ambiguous (datasheet says 'transmits from TX_RD to TX_WR' but
                // doesn't specify SENDOK on empty range). We set SENDOK to avoid leaving
                // Marinetti waiting for an interrupt that would never arrive.
                sr[W5100_Sn_IR] |= W5100_Sn_IR_SENDOK;
                break;
            }

            // Linearize TX ring buffer into flat array.
            // We walk from tx_rd to tx_rd+to_send-1, computing the physical
            // address of each byte via the mask formula.
            // Stack-safe: to_send is bounded by tx_size which is at most 8192 bytes
            // (maximum W5100 TX buffer per socket), so this allocation can never overflow.
            uint8_t tmp[8192];
            uint16_t copied = 0;
            while (copied < to_send) {
                uint16_t phys = s->tx_base + ((tx_rd + copied) & s->tx_mask);
                tmp[copied] = w->mem[phys];
                copied++;
            }

            bool ok = false;

            if (sr[W5100_Sn_SR] == W5100_SOCK_MACRAW) {
                // Refresh VNAT config: the guest may have acquired a new IP
                // via DHCP after opening the MACRAW socket.
                {
                    auto r32 = [](const uint8_t *p) -> uint32_t {
                        return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|
                               ((uint32_t)p[2]<<8)|p[3];
                    };
                    vnat_update_config(&w->vnat,
                        &w->mem[W5100_SHAR],
                        r32(&w->mem[W5100_SIPR]),
                        r32(&w->mem[W5100_GAR]),
                        r32(&w->mem[W5100_SUBR]));
                }
                vnat_send_frame(&w->vnat, tmp, to_send);
                ok = true;

            } else if (sr[W5100_Sn_SR] == W5100_SOCK_ESTABLISHED && s->tcp_socket) {
                ok = NET_WriteToStreamSocket(s->tcp_socket, tmp, (int)to_send);

            } else if (sr[W5100_Sn_SR] == W5100_SOCK_UDP && s->udp_socket) {
                // Resolve destination on every SEND rather than caching: the guest can change
                // Sn_DIPR/Sn_DPORT between sends (standard UDP practice — each datagram can
                // target a different host). Caching the resolved address would silently send to
                // the wrong destination after a Sn_DIPR change. For IP address literals,
                // NET_ResolveHostname returns immediately without DNS lookup.
                uint8_t *dipr = &sr[W5100_Sn_DIPR];
                uint16_t dport = read16(sr, W5100_Sn_DPORT);
                char ip_str[20];
                snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d",
                         dipr[0], dipr[1], dipr[2], dipr[3]);

                NET_Address *dest = NET_ResolveHostname(ip_str);
                if (dest && NET_WaitUntilResolved(dest, 1000) == 1) {
                    ok = NET_SendDatagram(s->udp_socket, dest, dport, tmp, (int)to_send);
                }
                if (dest) NET_UnrefAddress(dest);
            }

            if (ok) {
                // Advance TX_RD to TX_WR, marking all transmitted bytes as free.
                // TX_FSR is computed on read as (tx_size - (TX_WR - TX_RD)), so
                // this implicitly restores full free space without a separate write.
                write16(sr, W5100_Sn_TX_RD, tx_wr);
                sr[W5100_Sn_IR] |= W5100_Sn_IR_SENDOK;
            } else {
                // DEBUG: fprintf(stderr, "[U2] Socket %d: send failed\n", sn);
                sr[W5100_Sn_IR] |= W5100_Sn_IR_TIMEOUT;
            }
            break;
        }

        // ── RECV ─────────────────────────────────────────────────────────
        // On the real W5100, RECV acknowledges that the host CPU has
        // finished reading from the RX buffer and has updated RX_RD.
        // The chip uses this to decide it can overwrite consumed space.
        //
        // In our emulation, RX_RSR is computed dynamically as (RX_WR - RX_RD)
        // on every read.  We never store a stale RX_RSR value, so there is
        // nothing to update here.  The host's write to RX_RD is the only
        // acknowledgement needed; RECV is truly a no-op.
        case W5100_Sn_CR_RECV: {
            // RECV acknowledges the host CPU has updated RX_RD.
            // On real W5100, RX_RSR is recomputed from RX_WR - RX_RD.
            // We compute RSR dynamically on read, so RECV is a no-op.
            break;
        }

        default:
            break;
    }

    // Command register self-clears after execution.
    // The guest polls Sn_CR == 0 to confirm the command was accepted.
    sr[W5100_Sn_CR] = 0x00;
}

// ════════════════════════════════════════════════════════════════════
// REGISTER READ
// ════════════════════════════════════════════════════════════════════
//
// w5100_read() is called by the Uthernet II device for every DATA-port
// read after the guest has set ADDR_HI/ADDR_LO.  With auto-increment
// (MR_AI) the device layer increments addr after each call.
//
// Most addresses return mem[addr] directly.  Special cases:
//
// TX_FSR:  Free TX space, computed as (tx_size - (TX_WR - TX_RD)).
//          This is the amount the guest can write before the TX buffer
//          is full.  Computed on read to reflect any TX_RD advances
//          made by SEND without requiring explicit invalidation.
//
// RX_RSR:  Received byte count, computed as (RX_WR - RX_RD).
//          RX_WR is advanced by process_sockets() as data arrives.
//          RX_RD is advanced by the guest after consuming data.
//          Unsigned 16-bit arithmetic handles pointer wrap correctly.
//
// POLL TRIGGER:  Reading Sn_SR, Sn_RX_RSR, or Sn_RX_RSR+1 triggers
// process_sockets() to service network I/O.  This is the natural
// coupling point: the guest's receive-polling loop reads these
// registers in a tight loop, so we process the network on each
// iteration of that loop.  Rate-limiting prevents thrashing.

uint8_t w5100_read(w5100_state_t *w, uint16_t addr) {
    // O4: out-of-range address is a bug — annotate unlikely so normal reads aren't penalized.
    if (__builtin_expect(addr >= W5100_MEM_SIZE, 0)) return 0;

    // Check if this is a socket register read that needs dynamic values.
    // O13: precompute the relative offset once; avoids three subtractions and two
    // divisions/modulo ops inside the block (sn and offset are both derived from it).
    if (addr >= W5100_SREG_BASE && addr < W5100_SREG_BASE + W5100_NUM_SOCKETS * W5100_SREG_SIZE) {
        uint16_t rel    = addr - W5100_SREG_BASE;           // O13: single subtraction
        int      sn     = rel / W5100_SREG_SIZE;            // socket number
        uint8_t  offset = (uint8_t)(rel % W5100_SREG_SIZE); // register offset within socket
        uint8_t *sr = sock_reg(w, sn);
        w5100_socket_t *s = &w->sockets[sn];

        // Poll network I/O when software reads status or RX size.
        // Rate-limited: SDL3_net calls are expensive in tight poll loops.
        // 16ms = ~60Hz, matching the IIgs frame rate.
        //
        // Using SDL_GetTicks() (1ms resolution) as the rate limiter:
        // "now != last_poll" fires at most once per millisecond, which is
        // still 1000 polls/second — ample for 60fps networking — without
        // the overhead of calling SDL3_net on every 6502 instruction.
        if (offset == W5100_Sn_SR || offset == W5100_Sn_RX_RSR || offset == W5100_Sn_RX_RSR + 1) {
            // Shared across all W5100 instances (static local). If two Uthernet II cards exist
            // in different slots, they share the same rate-limiting timestamp. This is
            // intentional — both cards benefit from the same poll cadence, and the overhead
            // of tracking per-instance timestamps is not justified.
            static uint64_t last_poll = 0;
            uint64_t now = SDL_GetTicks();
            if (now != last_poll) {  // at most once per millisecond
                last_poll = now;
                w5100_process_sockets(w);
            }
        }

        // TX_FSR: free space = buffer size - (TX_WR - TX_RD)
        //
        // TX_WR is advanced by the guest (filling the buffer).
        // TX_RD is advanced by SEND (consuming transmitted bytes).
        // The unsigned difference (TX_WR - TX_RD) is the current used
        // space; subtracting from tx_size gives remaining free space.
        // Both bytes of the big-endian 16-bit value are handled here.
        if (offset == W5100_Sn_TX_FSR || offset == W5100_Sn_TX_FSR + 1) {
            uint16_t tx_rd = read16(sr, W5100_Sn_TX_RD);
            uint16_t tx_wr = read16(sr, W5100_Sn_TX_WR);
            uint16_t used = tx_wr - tx_rd;
            uint16_t free = s->tx_size - used;
            if (offset == W5100_Sn_TX_FSR)
                return (uint8_t)(free >> 8);
            else
                return (uint8_t)(free & 0xFF);
        }

        // RX_RSR: computed dynamically as RX_WR - RX_RD (unsigned 16-bit).
        //
        // WHY UNSIGNED SUBTRACTION?  The ring pointers are 16-bit counters
        // that increment freely and wrap through 0x0000 without resetting.
        // If RX_WR = 0x0002 and RX_RD = 0xFFFE (RD hasn't caught up yet),
        // the subtraction 0x0002 - 0xFFFE = 0x0004 gives the correct
        // byte count (4) because uint16_t arithmetic wraps modulo 65536.
        // This works for any combination as long as the ring buffer is
        // not more than 32KB full (which is impossible since each socket's
        // RX buffer is at most 8KB).
        if (offset == W5100_Sn_RX_RSR || offset == W5100_Sn_RX_RSR + 1) {
            uint16_t rx_wr = read16(sr, W5100_Sn_RX_WR);
            uint16_t rx_rd = read16(sr, W5100_Sn_RX_RD);
            uint16_t rsr = rx_wr - rx_rd;

            if (offset == W5100_Sn_RX_RSR)
                return (uint8_t)(rsr >> 8);
            else
                return (uint8_t)(rsr & 0xFF);
        }
    }

    return w->mem[addr];
}

// ════════════════════════════════════════════════════════════════════
// REGISTER WRITE
// ════════════════════════════════════════════════════════════════════
//
// w5100_write() handles all writes to the W5100 internal address space.
// Most writes go directly to mem[addr].  Special cases require
// interception before (or instead of) the plain store:
//
// Sn_CR:   Non-zero writes trigger exec_socket_cmd(); the command byte
//          is stored first so exec_socket_cmd can read it if needed.
//
// Sn_SR:   Read-only; writes are silently discarded.  The guest must
//          never write to status registers.
//
// Sn_IR:   Write-one-to-clear (W1C).  Implementation: mem &= ~val.
//          CRITICAL: do NOT write val to mem first.  The register
//          already holds the current interrupt state; writing val
//          would clear bits that are already 1 AND set bits that are
//          already 0.  The correct operation clears only the bits that
//          the guest explicitly acknowledged (those set to 1 in val).
//
// TX_FSR:  Read-only (computed on read); writes discarded.
// RX_RSR:  Read-only (computed on read); writes discarded.
//
// RX_RD:   Written normally so read16(Sn_RX_RD) returns the correct
//          value when the next RSR is computed.
//
// TMSR/RMSR: Buffer geometry depends on these; trigger recalculation
//            immediately so the next ring buffer access uses correct
//            base/size/mask values.

void w5100_write(w5100_state_t *w, uint16_t addr, uint8_t val) {
    // O4: out-of-range address is a programming error — annotate unlikely.
    if (__builtin_expect(addr >= W5100_MEM_SIZE, 0)) return;

    // O13: compute the socket-range membership once and reuse it for both the
    // command/status checks and the Sn_IR check below, avoiding a second
    // addr >= W5100_SREG_BASE comparison.
    static constexpr uint16_t SREG_END = W5100_SREG_BASE + W5100_NUM_SOCKETS * W5100_SREG_SIZE;
    bool in_sreg = (addr >= W5100_SREG_BASE && addr < SREG_END);

    // Socket command register — intercept and execute.
    // Write val to mem first so exec_socket_cmd can read it back
    // if needed, then dispatch.
    if (in_sreg) {
        uint16_t rel    = addr - W5100_SREG_BASE;            // O13: single subtraction
        int      sn     = rel / W5100_SREG_SIZE;
        uint8_t  offset = (uint8_t)(rel % W5100_SREG_SIZE);

        if (offset == W5100_Sn_CR && val != 0) {
            w->mem[addr] = val;
            exec_socket_cmd(w, sn, val);
            return;
        }

        // Status register is read-only; writes are silently ignored.
        // The guest must use OPEN/CONNECT/CLOSE commands to change status.
        if (offset == W5100_Sn_SR) return;

        // TX_FSR and RX_RSR are read-only computed values; discard writes.
        if (offset == W5100_Sn_TX_FSR || offset == W5100_Sn_TX_FSR + 1) return;
        if (offset == W5100_Sn_RX_RSR || offset == W5100_Sn_RX_RSR + 1) return;

        // Socket Interrupt Register: write-one-to-clear (same semantics as global IR).
        // Checked here (inside the already-confirmed in_sreg block) rather than in
        // a second outer range check below — O13: eliminates the redundant range test.
        if (offset == W5100_Sn_IR) {
            w->mem[addr] &= ~val;
            return;
        }
    }

    // Global Interrupt Register: write-one-to-clear (W1C).
    //
    // The guest reads IR to learn which sockets have pending interrupts,
    // then writes back the same value to clear all observed bits.  If
    // a new interrupt arrives between the read and the write, it must
    // not be lost.  Implementation:  mem &= ~val  clears only the bits
    // the guest acknowledged; any newly-arrived bits (not in val) are
    // preserved.  The plain-store alternative (mem = val) would
    // erroneously clear bits not present in the original read result.
    if (addr == W5100_IR) {
        w->mem[W5100_IR] &= ~val;
        return;
    }

    // Note: the original second socket-range check for Sn_IR has been merged
    // into the in_sreg block above (O13: eliminates duplicate range test).

    // General register write: store value directly.
    w->mem[addr] = val;

    // TMSR/RMSR writes recalculate buffer geometry immediately.
    // This must happen after the store so recalc_buffer_geometry reads
    // the new value from mem[W5100_TMSR/RMSR].
    if (addr == W5100_TMSR || addr == W5100_RMSR) {
        recalc_buffer_geometry(w);
    }
}

// ════════════════════════════════════════════════════════════════════
// SOCKET POLLING: process_sockets()
// ════════════════════════════════════════════════════════════════════
//
// process_sockets() drives the asynchronous side of the W5100 emulation.
// On a real chip, the TCP/IP engine runs independently and raises the
// /INT pin when something noteworthy happens.  In emulation we have no
// such mechanism; instead we poll from the guest's read path (see the
// rate-limited trigger in w5100_read).
//
// Three operations are performed per socket per call:
//
// 1. CONNECT COMPLETION CHECK
//    When connect_pending is true, we call NET_WaitUntilConnected(sock, 0)
//    with timeout=0 (non-blocking probe).  Return values:
//      +1 → connected: set Sn_SR=ESTABLISHED, set Sn_IR_CON, clear pending
//       0 → still connecting: do nothing, try again next call
//      -1 → failed: close socket, set Sn_SR=CLOSED, set Sn_IR_TIMEOUT
//    We use 'continue' after the connect check so that we don't also try
//    to read data from a socket that hasn't connected yet.
//
// 2. TCP DATA BUFFERING
//    For ESTABLISHED sockets, we call NET_ReadFromStreamSocket() with
//    the available ring buffer free space as the max read size.  The
//    received bytes are written into the ring buffer using the mask
//    formula.  RX_WR is advanced by the number of bytes actually read.
//    The RECV interrupt bit is set so the guest knows data is available.
//    If the read returns < 0, the remote peer closed the connection:
//    we set CLOSE_WAIT and the DISCON interrupt bit.
//
// 3. UDP DATAGRAM BUFFERING
//    Each datagram is received with NET_ReceiveDatagram() and prefixed
//    with an 8-byte W5100 UDP header before writing to the RX ring.
//    The header format is: [src_ip:4][src_port:2][payload_length:2].
//    This matches the WIZnet W5100 hardware behavior; software reading
//    the RX buffer (e.g., Marinetti) strips this header to extract the
//    sender address and payload length.
//
// 4. MACRAW DATA BUFFERING
//    vnat_poll() is called first to let the virtual NAT engine process
//    any pending timers or retransmissions.  Then we drain the VNAT
//    receive queue with vnat_recv_frame() and write each frame into the
//    RX ring buffer with the 2-byte PACKET_INFO prefix.
//
//    PACKET_INFO = frame_len + 2 (total bytes consumed including header).
//    See the header file's MACRAW RX FORMAT section for the full protocol.
//    The key invariant: RX_WR is advanced by (frame_len + 2) to match
//    what the reader will advance RX_RD by after reading PACKET_INFO.

void w5100_process_sockets(w5100_state_t *w) {
    for (int sn = 0; sn < W5100_NUM_SOCKETS; sn++) {
        uint8_t *sr = sock_reg(w, sn);
        w5100_socket_t *s = &w->sockets[sn];

        // ── Step 1: Check for pending TCP connect completion ─────────────
        //
        // NET_WaitUntilConnected(sock, 0) is a non-blocking probe:
        //   +1 → handshake complete
        //    0 → still in progress (SYN sent, waiting for SYN-ACK)
        //   -1 → connection refused or network error
        // We use 'continue' to skip the data-read steps for this socket
        // during the same poll cycle; the connection isn't usable yet.
        if (s->connect_pending && s->tcp_socket) {
            int status = NET_WaitUntilConnected(s->tcp_socket, 0);
            if (status == 1) {
                // Connected
                sr[W5100_Sn_SR] = W5100_SOCK_ESTABLISHED;
                sr[W5100_Sn_IR] |= W5100_Sn_IR_CON;
                s->connect_pending = false;
                // DEBUG: fprintf(stderr, "[Uthernet2] Socket %d: connected\n", sn);
            } else if (status < 0) {
                // Failed
                close_socket_net(s);
                sr[W5100_Sn_SR] = W5100_SOCK_CLOSED;
                sr[W5100_Sn_IR] |= W5100_Sn_IR_TIMEOUT;
                // DEBUG: fprintf(stderr, "[Uthernet2] Socket %d: connect failed\n", sn);
            }
            // status == 0: still pending, check next frame
            continue;
        }

        // ── Step 2: Buffer incoming TCP data ─────────────────────────────
        //
        // Read up to (rx_size - used) bytes from the kernel TCP receive
        // buffer into our W5100 RX ring.  We limit to 'free' bytes to
        // prevent overwriting data the guest hasn't consumed yet.
        //
        // Physical address for byte i: rx_base + ((rx_wr + i) & rx_mask)
        // After writing, RX_WR is advanced by the actual byte count.
        // The RECV interrupt signals the guest that RX_RSR > 0.
        //
        // On read error (got < 0): the TCP connection was closed by the
        // remote peer.  We transition to CLOSE_WAIT (RFC 793 state for
        // "received FIN, local close pending") and set DISCON interrupt.
        if (sr[W5100_Sn_SR] == W5100_SOCK_ESTABLISHED && s->tcp_socket) {
            uint16_t rx_wr = read16(sr, W5100_Sn_RX_WR);
            uint16_t rx_rd = read16(sr, W5100_Sn_RX_RD);
            uint16_t used = rx_wr - rx_rd;
            if (used < s->rx_size) {
                uint16_t free = s->rx_size - used;
                uint8_t tmp[8192];
                int max_read = (free < sizeof(tmp)) ? free : sizeof(tmp);
                int got = NET_ReadFromStreamSocket(s->tcp_socket, tmp, max_read);

                if (got > 0) {
                    // O13: fast path for the common case where incoming data fits
                    // contiguously in the ring buffer without wrapping.
                    // phys_start = rx_base + (rx_wr & rx_mask).  If
                    // (rx_wr & rx_mask) + got <= rx_size, the run is contiguous
                    // and a single memcpy replaces the byte-by-byte loop.
                    uint16_t ring_off = rx_wr & s->rx_mask;
                    if (__builtin_expect((uint32_t)ring_off + (uint32_t)got <= (uint32_t)s->rx_size, 1)) {
                        // O13: no wrap — copy in one shot
                        memcpy(&w->mem[s->rx_base + ring_off], tmp, got);
                    } else {
                        // Wrapping case: copy in two pieces (head to end, remainder from start)
                        uint16_t first_chunk = s->rx_size - ring_off;
                        memcpy(&w->mem[s->rx_base + ring_off], tmp, first_chunk);
                        memcpy(&w->mem[s->rx_base], tmp + first_chunk, got - first_chunk);
                    }
                    write16(sr, W5100_Sn_RX_WR, rx_wr + (uint16_t)got);
                    sr[W5100_Sn_IR] |= W5100_Sn_IR_RECV;
                } else if (got < 0) {
                    // Remote closed connection (EOF or error)
                    sr[W5100_Sn_SR] = W5100_SOCK_CLOSE_WAIT;
                    sr[W5100_Sn_IR] |= W5100_Sn_IR_DISCON;
                }
            }
        }

        // ── Step 3: Buffer incoming UDP datagrams ─────────────────────────
        //
        // W5100 UDP RX format: [src_ip:4][src_port:2][length:2][payload...]
        // Each datagram is prefixed with this 8-byte header so the reader
        // knows who sent it and how long the payload is.
        //
        // One datagram per poll cycle: unlike MACRAW (which drains all pending frames), UDP
        // processes a single datagram per call. This rate-limits the guest's RX processing
        // to one datagram per frame (~60fps), preventing a burst of incoming datagrams from
        // monopolizing the poll cycle. For typical BBS/terminal traffic this is sufficient;
        // high-throughput UDP applications (streaming, gaming) may need the limit raised.
        //
        // The 8-byte header is constructed from the NET_Datagram fields:
        //   header[0..3] = source IPv4 address (parsed from string)
        //   header[4..5] = source port (big-endian)
        //   header[6..7] = payload length (big-endian)
        // Then the payload bytes follow immediately in the ring buffer.
        // RX_WR advances by (8 + payload_length).
        if (sr[W5100_Sn_SR] == W5100_SOCK_UDP && s->udp_socket) {
            uint16_t rx_wr = read16(sr, W5100_Sn_RX_WR);
            uint16_t rx_rd = read16(sr, W5100_Sn_RX_RD);
            uint16_t used = rx_wr - rx_rd;

            NET_Datagram *dgram = nullptr;
            if (used < s->rx_size && NET_ReceiveDatagram(s->udp_socket, &dgram) && dgram) {
                uint16_t free = s->rx_size - used;
                uint16_t total = 8 + (uint16_t)dgram->buflen;

                if (total <= free) {
                    uint8_t header[8] = {};
                    const char *str = dgram->addr ? NET_GetAddressString(dgram->addr) : nullptr;
                    if (str) {
                        int a, b, c, d;
                        if (sscanf(str, "%d.%d.%d.%d", &a, &b, &c, &d) == 4) {
                            header[0] = (uint8_t)a; header[1] = (uint8_t)b;
                            header[2] = (uint8_t)c; header[3] = (uint8_t)d;
                        }
                    }
                    header[4] = (uint8_t)(dgram->port >> 8);
                    header[5] = (uint8_t)(dgram->port & 0xFF);
                    header[6] = (uint8_t)(dgram->buflen >> 8);
                    header[7] = (uint8_t)(dgram->buflen & 0xFF);

                    // O13: fast path when the 8-byte header + payload fit contiguously.
                    // For typical small UDP datagrams (DNS, NTP, <8KB) and ring buffers
                    // that are rarely near-full, this path is taken nearly every time.
                    uint16_t hdr_ring_off = rx_wr & s->rx_mask;
                    if (__builtin_expect((uint32_t)hdr_ring_off + (uint32_t)total <= (uint32_t)s->rx_size, 1)) {
                        // No wrap: copy header + payload in two memcpys
                        memcpy(&w->mem[s->rx_base + hdr_ring_off], header, 8);
                        memcpy(&w->mem[s->rx_base + hdr_ring_off + 8], dgram->buf, dgram->buflen);
                    } else {
                        // Wrapping case: fall back to byte-by-byte for simplicity
                        for (uint16_t i = 0; i < 8; i++) {
                            uint16_t phys = s->rx_base + ((rx_wr + i) & s->rx_mask);
                            w->mem[phys] = header[i];
                        }
                        for (int i = 0; i < dgram->buflen; i++) {
                            uint16_t phys = s->rx_base + ((rx_wr + 8 + i) & s->rx_mask);
                            w->mem[phys] = dgram->buf[i];
                        }
                    }
                    write16(sr, W5100_Sn_RX_WR, rx_wr + total);
                    sr[W5100_Sn_IR] |= W5100_Sn_IR_RECV;
                }
                NET_DestroyDatagram(dgram);
            }
        }

        // ── Step 4: Buffer incoming MACRAW frames ─────────────────────────
        //
        // For MACRAW sockets, the flow is:
        //   a) vnat_poll(): let VNAT process internal timers, send queued
        //      responses (e.g., delayed ARP replies, TCP retransmissions).
        //   b) vnat_recv_frame(): pull one response frame from VNAT's output
        //      queue.  Returns 0 when the queue is empty.
        //   c) Write frame into RX ring with 2-byte PACKET_INFO header.
        //
        // PACKET_INFO FORMAT:
        //   Bytes 0–1: uint16 big-endian = frame_len + 2
        //   Bytes 2+:  raw Ethernet frame (frame_len bytes)
        //
        // The "+2" in PACKET_INFO means the value encodes the total number
        // of ring-buffer bytes consumed (header + data).  The consumer
        // (Marinetti, ioLibrary) reads PACKET_INFO, subtracts 2 to get
        // the raw frame length, reads that many frame bytes, then advances
        // RX_RD by the original (un-subtracted) PACKET_INFO value.
        //
        // OVERFLOW GUARD: if (used + total > rx_size) we stop draining.
        // This prevents overwriting data the guest hasn't consumed yet.
        // The frame stays in VNAT's queue and will be re-attempted next poll.
        if (sr[W5100_Sn_SR] == W5100_SOCK_MACRAW) {
            vnat_poll(&w->vnat);

            uint8_t frame[1600];
            uint16_t frame_len;
            while ((frame_len = vnat_recv_frame(&w->vnat, frame, sizeof(frame))) > 0) {
                uint16_t rx_wr = read16(sr, W5100_Sn_RX_WR);
                uint16_t rx_rd = read16(sr, W5100_Sn_RX_RD);
                uint16_t used = rx_wr - rx_rd;
                uint16_t total = 2 + frame_len;  // 2-byte header + frame data

                if (used + total > s->rx_size) break;  // need room for header + data physically

                // W5100 MACRAW RX (datasheet + WIZnet readFrame):
                // PACKET_INFO (2 bytes) = total size INCLUDING itself = frame_len + 2
                // Then frame_len bytes of raw Ethernet frame data.
                // Reader reads PACKET_INFO, subtracts 2 to get frame length,
                // reads that many bytes, advances RX_RD by PACKET_INFO value.
                // RX_WR advances by frame_len + 2 (total bytes written).
                uint16_t pkt_info = frame_len + 2;  // includes the 2-byte header itself
                uint16_t phys;

                // Write 2-byte PACKET_INFO (big-endian) at current rx_wr position.
                // Always write byte-by-byte for the 2-byte header (wrapping is possible).
                phys = s->rx_base + (rx_wr & s->rx_mask);
                w->mem[phys] = (uint8_t)(pkt_info >> 8);
                phys = s->rx_base + ((rx_wr + 1) & s->rx_mask);
                w->mem[phys] = (uint8_t)(pkt_info & 0xFF);

                // Write frame data starting 2 bytes later (after PACKET_INFO).
                // O13: fast path when frame doesn't wrap in the ring buffer.
                {
                    uint16_t data_ring_off = (uint16_t)((rx_wr + 2) & s->rx_mask);
                    if (__builtin_expect((uint32_t)data_ring_off + (uint32_t)frame_len <= (uint32_t)s->rx_size, 1)) {
                        // O13: no wrap — single memcpy replaces the per-byte loop
                        memcpy(&w->mem[s->rx_base + data_ring_off], frame, frame_len);
                    } else {
                        // Wrapping case: two-piece copy
                        uint16_t first_chunk = s->rx_size - data_ring_off;
                        memcpy(&w->mem[s->rx_base + data_ring_off], frame, first_chunk);
                        memcpy(&w->mem[s->rx_base], frame + first_chunk, frame_len - first_chunk);
                    }
                }

                // Advance RX_WR by total bytes written (header + frame).
                // RX_RSR = RX_WR - RX_RD will reflect this new data on the
                // next guest read of Sn_RX_RSR.
                write16(sr, W5100_Sn_RX_WR, rx_wr + total);
                sr[W5100_Sn_IR] |= W5100_Sn_IR_RECV;
            }
        }
    }
}

// ════════════════════════════════════════════════════════════════════
// INITIALIZATION AND RESET
// ════════════════════════════════════════════════════════════════════

// w5100_init(): one-time initialization.
// Calls NET_Init() if not already done (guarded by net_initialized flag).
// net_initialized is a per-instance field on w5100_state_t, so the guard prevents
// double-init on the same instance only, not across cards. NET_Init() itself is
// idempotent (SDL3_net convention), so multiple cards calling it is safe regardless.
// Initializes VNAT, then resets chip state.
void w5100_init(w5100_state_t *w) {
    if (!w->net_initialized) {
        if (NET_Init()) {
            w->net_initialized = true;
        } else {
            // DEBUG: fprintf(stderr, "[Uthernet2] NET_Init failed\n");
        }
    }
    vnat_init(&w->vnat);
    w5100_reset(w);
}

// w5100_reset(): restore power-on state.
//
// Called on emulator reset (Ctrl-Reset, power cycle) and on software
// reset (guest writes MR_RST=1 to the MR port).
//
// Sequence:
// 1. Close all socket network resources (SDL3_net objects).
// 2. Reset VNAT engine.
// 3. Zero the entire 32KB memory array (clears all registers and buffers).
// 4. Set addr=0, mode_reg=MR_IND (Uthernet II always uses indirect mode).
// 5. Write W5100 datasheet default values for RTR, RCR, TMSR, RMSR, MR.
// 6. Set a locally-administered MAC address for the emulated chip.
// 7. Recalculate buffer geometry from the default TMSR/RMSR values.
//
// DEFAULT TMSR/RMSR = 0x55 = 0b01010101
// Field layout: [S3=01][S2=01][S1=01][S0=01] → 2KB per socket.
// This divides the 8KB TX and 8KB RX pools evenly (4 × 2KB = 8KB).
//
// DEFAULT MAC = 02:00:A2:55:47:53
// Bit 1 of byte 0 set → "locally administered" (not a factory OUI).
// Bytes 2–5 spell "A2GS" in ASCII — Apple II / GSSquared branding.
// The locally-administered bit prevents conflicts with real hardware MACs.
void w5100_reset(w5100_state_t *w) {
    // Close all sockets and reset VNAT
    for (int i = 0; i < W5100_NUM_SOCKETS; i++) {
        close_socket_net(&w->sockets[i]);
    }
    vnat_reset(&w->vnat);

    // Clear register/memory file
    memset(w->mem, 0, W5100_MEM_SIZE);

    // Reset address pointer and mode
    w->addr = 0;
    w->mode_reg = W5100_MR_IND;  // Indirect mode always enabled on Uthernet II

    // Default register values per W5100 datasheet
    w->mem[W5100_MR] = W5100_MR_IND;
    w->mem[W5100_RTR] = 0x07;      // RTR default = 0x07D0 (2000 = 200ms)
    w->mem[W5100_RTR + 1] = 0xD0;
    w->mem[W5100_RCR] = 0x08;      // 8 retries
    w->mem[W5100_TMSR] = 0x55;     // 2KB per socket (default): 0b01_01_01_01
    w->mem[W5100_RMSR] = 0x55;     // 2KB per socket (default): 0b01_01_01_01

    // Set a default MAC address (locally administered).
    // Byte 0 bit 1 = 1 → locally administered (not a globally assigned OUI).
    // This ensures no conflict with real Uthernet II hardware on the same LAN.
    w->mem[W5100_SHAR + 0] = 0x02;  // locally administered bit
    w->mem[W5100_SHAR + 1] = 0x00;
    w->mem[W5100_SHAR + 2] = 0xA2;  // "A2" for Apple II
    w->mem[W5100_SHAR + 3] = 0x55;
    w->mem[W5100_SHAR + 4] = 0x47;  // "G" for GSSquared
    w->mem[W5100_SHAR + 5] = 0x53;  // "S"

    // Recalculate buffer geometry from the freshly-written TMSR/RMSR defaults
    recalc_buffer_geometry(w);
}
