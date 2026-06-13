/*
 *   W5100 Chip Emulation — Wiznet W5100 as used in Uthernet II
 *
 *   Register-level emulation of the W5100 TCP/IP offload chip.
 *   Indirect register access via ADDR_HI/ADDR_LO/DATA ports.
 *   Socket operations bridged to host OS via SDL3_net.
 *
 *   Reference: WIZnet W5100 Datasheet v1.2.4
 *              WIZnet ioLibrary_Driver (GitHub Wiznet/ioLibrary_Driver)
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

/*
 * ══════════════════════════════════════════════════════════════════════
 * W5100 CHIP ARCHITECTURE OVERVIEW
 * ══════════════════════════════════════════════════════════════════════
 *
 * The WIZnet W5100 is a "hardwired TCP/IP" controller: it implements
 * the complete TCP/IP stack in silicon, exposing a simple register
 * interface to the host CPU.  From the Apple II's perspective, there
 * is no TCP/IP implementation in software — it just writes socket
 * parameters and commands, then reads status and data.
 *
 * HOST CPU INTERFACE
 * ──────────────────
 * The Uthernet II card connects the W5100 in "indirect bus interface"
 * mode (MR_IND=1).  Instead of mapping the full 32KB into Apple II
 * address space (which would require a large window), all 32KB are
 * accessed through four Apple II I/O registers at slot base + 0–3:
 *
 *   Port offset 0: MR      (Mode Register, direct)
 *   Port offset 1: ADDR_HI (high byte of 16-bit internal address)
 *   Port offset 2: ADDR_LO (low byte of 16-bit internal address)
 *   Port offset 3: DATA    (read/write byte at the latched address)
 *
 * A typical read sequence:
 *   Write ADDR_HI = 0x04           ; target = socket 0 status (0x0403)
 *   Write ADDR_LO = 0x03
 *   Read  DATA    → Sn_SR value
 *
 * With MR_AI (auto-increment) set, each DATA access advances the
 * address pointer by one, enabling sequential burst reads/writes
 * without re-issuing ADDR_HI/ADDR_LO for every byte.
 *
 * INTERNAL 32KB ADDRESS SPACE
 * ────────────────────────────
 * The W5100's 32KB is divided into four regions:
 *
 *   0x0000–0x002F  Common registers   (48 bytes — chip-wide config)
 *   0x0400–0x07FF  Socket registers   (4 × 256 bytes = 1024 bytes)
 *   0x4000–0x5FFF  TX buffer memory   (8192 bytes total across 4 sockets)
 *   0x6000–0x7FFF  RX buffer memory   (8192 bytes total across 4 sockets)
 *
 * Addresses 0x0030–0x03FF and 0x0800–0x3FFF are reserved/undefined.
 * The TX and RX regions together occupy the upper 16KB of the space.
 *
 * FOUR SOCKET MODEL
 * ─────────────────
 * The W5100 supports four independent sockets (S0–S3).  Each socket
 * has its own 256-byte register block and a private slice of the TX
 * and RX buffer memories.  Sockets are independent: S0 can carry a
 * TCP connection while S1 carries UDP while S2 is MACRAW.
 *
 * SUPPORTED SOCKET MODES
 * ──────────────────────
 *   TCP    — reliable stream, client or server
 *   UDP    — unreliable datagram
 *   IPRAW  — raw IP packets (non-TCP/UDP)
 *   MACRAW — raw Ethernet frames, Socket 0 only
 *   PPPoE  — PPP over Ethernet, Socket 0 only (not emulated here)
 *
 * ══════════════════════════════════════════════════════════════════════
 * REGISTER FILE LAYOUT
 * ══════════════════════════════════════════════════════════════════════
 *
 * COMMON REGISTERS (0x0000–0x002F)
 * ──────────────────────────────────
 * These are chip-wide configuration registers.  The most important
 * for operation are:
 *
 *   MR   (0x0000)  Mode: enables indirect bus, auto-increment, PPPoE,
 *                  ping block; bit 7 is a self-clearing software reset.
 *
 *   SHAR (0x0009)  Source MAC address (6 bytes).  Used as the Ethernet
 *                  source address for all transmitted frames.
 *
 *   SIPR (0x000F)  Source IP address (4 bytes).  The chip's IP identity.
 *
 *   GAR  (0x0001)  Gateway IP (4 bytes).  Used for routing packets whose
 *                  destination is outside the local subnet.
 *
 *   SUBR (0x0005)  Subnet mask (4 bytes).  Used to decide whether a
 *                  destination IP is local or needs the gateway.
 *
 *   IR   (0x0015)  Interrupt Register.  Bits [3:0] mirror the per-socket
 *                  interrupt status.  Write-one-to-clear semantics.
 *
 *   RMSR (0x001A)  RX Memory Size Register.  Two bits per socket encode
 *                  how the 8KB RX region is divided (see below).
 *
 *   TMSR (0x001B)  TX Memory Size Register.  Same encoding as RMSR.
 *
 * SOCKET REGISTER BLOCK (0x0400–0x07FF)
 * ──────────────────────────────────────
 * Socket n's registers start at 0x0400 + n×0x0100.  Key registers:
 *
 *   Sn_MR    (+0x00)  Mode: protocol selection (TCP/UDP/IPRAW/MACRAW),
 *                     no-delayed-ACK flag, multicast flag.
 *
 *   Sn_CR    (+0x01)  Command: write a command byte to trigger an action
 *                     (OPEN, CONNECT, SEND, RECV, DISCON, CLOSE…).
 *                     Self-clears to 0x00 after execution.
 *
 *   Sn_IR    (+0x02)  Socket Interrupt: CON, DISCON, RECV, TIMEOUT,
 *                     SENDOK.  Write-one-to-clear.  Must NOT write val
 *                     first; use mem &= ~val to avoid re-setting bits.
 *
 *   Sn_SR    (+0x03)  Socket Status: read-only state machine value
 *                     (CLOSED=0x00, INIT=0x13, ESTABLISHED=0x17, …).
 *                     Driven by command execution, not by software write.
 *
 *   Sn_PORT  (+0x04)  Source port (2 bytes, big-endian).
 *   Sn_DHAR  (+0x06)  Destination MAC (6 bytes, for MACRAW).
 *   Sn_DIPR  (+0x0C)  Destination IP (4 bytes).
 *   Sn_DPORT (+0x10)  Destination port (2 bytes).
 *
 *   Sn_TX_FSR (+0x20)  TX Free Size (2 bytes).  Computed: tx_size - (TX_WR - TX_RD).
 *   Sn_TX_RD  (+0x22)  TX Read Pointer.  Chip advances this after transmitting.
 *   Sn_TX_WR  (+0x24)  TX Write Pointer.  Host advances this after filling TX buf.
 *   Sn_RX_RSR (+0x26)  RX Received Size (2 bytes).  Computed: RX_WR - RX_RD.
 *   Sn_RX_RD  (+0x28)  RX Read Pointer.  Host advances this after consuming data.
 *   Sn_RX_WR  (+0x2A)  RX Write Pointer.  Chip advances this after buffering data.
 *
 * ══════════════════════════════════════════════════════════════════════
 * BUFFER GEOMETRY: TMSR/RMSR ENCODING
 * ══════════════════════════════════════════════════════════════════════
 *
 * The W5100 has a fixed 8KB TX pool and a fixed 8KB RX pool.  TMSR and
 * RMSR control how each pool is split among the four sockets.
 *
 * Each register is 8 bits: [S3:S2][S2:S1] in 2-bit fields where field i
 * is bits [2i+1:2i].  The encoding is:
 *
 *   00 → 1 KB   (1024 bytes)
 *   01 → 2 KB   (2048 bytes)   ← default (0x55 = 01 01 01 01)
 *   10 → 4 KB   (4096 bytes)
 *   11 → 8 KB   (8192 bytes)
 *
 * The total across all four sockets must not exceed 8KB.  The default
 * value 0x55 = 0b01010101 allocates 2KB to each of the 4 sockets.
 *
 * PHYSICAL ADDRESS COMPUTATION (ring buffer)
 * ───────────────────────────────────────────
 * Each socket's buffer is a power-of-two ring buffer within the W5100
 * memory array.  The ring pointers (RX_WR, RX_RD, TX_WR, TX_RD) are
 * raw 16-bit counters that wrap at 0xFFFF — they do NOT reset to the
 * buffer base.  To find the physical memory address for a given pointer:
 *
 *   phys = socket_base + (pointer & socket_mask)
 *
 * where socket_mask = socket_size - 1.  Because socket_size is always a
 * power of 2, the mask operation is simply the modulo within the ring.
 * Example: if S0's RX base is 0x6000 and size is 2KB (mask = 0x07FF):
 *
 *   pointer = 0x8003  →  phys = 0x6000 + (0x8003 & 0x07FF) = 0x6003
 *   pointer = 0x8801  →  phys = 0x6000 + (0x8801 & 0x07FF) = 0x6001
 *
 * The pointer arithmetic itself (WR - RD) is unsigned 16-bit, which
 * naturally handles wrap-around at 0xFFFF:
 *
 *   WR=0x0001, RD=0xFFFE → WR-RD = 0x0003 (3 bytes available)
 *
 * BUFFER BASE LAYOUT
 * ──────────────────
 * Sockets are packed in order within the TX and RX regions.  With the
 * default 2KB allocation:
 *
 *   S0 TX: 0x4000–0x47FF    S0 RX: 0x6000–0x67FF
 *   S1 TX: 0x4800–0x4FFF    S1 RX: 0x6800–0x6FFF
 *   S2 TX: 0x5000–0x57FF    S2 RX: 0x7000–0x77FF
 *   S3 TX: 0x5800–0x5FFF    S3 RX: 0x7800–0x7FFF
 *
 * If S0 is given 8KB, it consumes the entire TX or RX pool.
 *
 * ══════════════════════════════════════════════════════════════════════
 * SOCKET LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════
 *
 * TCP CLIENT LIFECYCLE
 * ────────────────────
 *   1. Write Sn_MR = 0x01 (TCP)
 *      Write Sn_PORT = local source port (optional for client)
 *      Write Sn_DIPR = destination IP (4 bytes)
 *      Write Sn_DPORT = destination port
 *   2. Write Sn_CR = OPEN  →  Sn_SR becomes SOCK_INIT (0x13)
 *      TX/RX pointers reset to 0.
 *   3. Write Sn_CR = CONNECT  →  Sn_SR becomes SOCK_SYNSENT (0x15)
 *      TCP SYN is transmitted.  connect_pending set.
 *   4. Poll Sn_SR until SOCK_ESTABLISHED (0x17).
 *      Sn_IR bit CON (0x01) is set; Sn_IR bit in IR register set too.
 *   5. To send: fill TX buffer via DATA port, advance TX_WR,
 *      write Sn_CR = SEND.  Chip advances TX_RD and sets SENDOK.
 *   6. To receive: poll RX_RSR > 0, read data from RX buffer via
 *      DATA port, advance RX_RD, write Sn_CR = RECV.
 *   7. Write Sn_CR = DISCON  →  TCP FIN/ACK exchange,
 *      Sn_SR → SOCK_CLOSED.  Sn_IR bit DISCON (0x02) set.
 *   8. Write Sn_CR = CLOSE  →  Immediate close, Sn_SR → SOCK_CLOSED.
 *
 * TCP SERVER LIFECYCLE (Phase 2 — not yet implemented)
 * ─────────────────────────────────────────────────────
 *   1. OPEN → SOCK_INIT
 *   2. LISTEN → SOCK_LISTEN; chip waits for SYN
 *   3. SYN arrives → SOCK_SYNRECV (internally) → SOCK_ESTABLISHED
 *      Sn_IR bit CON set.
 *   4. Same send/recv flow as TCP client.
 *
 * UDP LIFECYCLE
 * ─────────────
 *   1. Write Sn_MR = 0x02 (UDP)
 *      Write Sn_PORT = local source port
 *   2. Write Sn_CR = OPEN  →  Sn_SR becomes SOCK_UDP (0x22)
 *      A datagram socket is bound to the source port.
 *   3. To send: fill TX buffer, set Sn_DIPR/Sn_DPORT (per-send dest),
 *      write Sn_CR = SEND.
 *   4. Incoming datagrams are prefixed with a W5100 UDP RX header:
 *        [src_ip:4][src_port:2][length:2][payload:length]
 *      Read using the same RX_RSR / RX_RD protocol as TCP.
 *   5. Write Sn_CR = CLOSE → SOCK_CLOSED, socket destroyed.
 *
 * MACRAW LIFECYCLE (Socket 0 only)
 * ─────────────────────────────────
 *   1. Write Sn_MR = 0x04 (MACRAW) on Socket 0 only.
 *   2. Write Sn_CR = OPEN  →  Sn_SR becomes SOCK_MACRAW (0x42).
 *      VNAT is configured with current IP/MAC/GW/MASK registers.
 *   3. To send: write a complete Ethernet frame into TX buffer,
 *      write Sn_CR = SEND.  Frame goes to vnat_send_frame().
 *   4. Received frames are written into RX buffer with a 2-byte
 *      PACKET_INFO prefix (see MACRAW RX FORMAT below).
 *   5. Write Sn_CR = CLOSE → SOCK_CLOSED.
 *
 * MACRAW RX FORMAT — PACKET_INFO CONVENTION
 * ──────────────────────────────────────────
 * The W5100 datasheet and WIZnet ioLibrary readFrame() function define
 * the MACRAW RX format as:
 *
 *   Byte 0–1: PACKET_INFO (big-endian uint16)
 *             = frame_length + 2    ← includes the 2-byte header itself
 *   Byte 2–N: raw Ethernet frame data (frame_length bytes)
 *
 * WHY +2?  Because readFrame reads the 2-byte header to learn the packet
 * length, then reads that many bytes from the buffer.  If PACKET_INFO
 * held just frame_length, the pointer arithmetic would be:
 *   advance RX_RD by (2 + frame_length)
 * But WIZnet ioLibrary's convention is:
 *   read PACKET_INFO → pkt_len
 *   read (pkt_len - 2) bytes of frame data
 *   advance RX_RD by pkt_len (= frame_len + 2)
 *
 * This means PACKET_INFO encodes the total bytes consumed from the ring
 * buffer (header + data), simplifying the consumer's pointer update.
 * Marinetti follows the same convention: it reads the 2-byte length,
 * subtracts 2 to get the frame size, reads that many frame bytes, and
 * advances RX_RD by the original (non-subtracted) length value.
 *
 * ══════════════════════════════════════════════════════════════════════
 * RX_RSR: DYNAMIC COMPUTATION
 * ══════════════════════════════════════════════════════════════════════
 *
 * The Received Size Register (RX_RSR) is not stored in the register
 * array.  It is computed on every read as:
 *
 *   RSR = (uint16_t)(RX_WR - RX_RD)
 *
 * WHY NOT STORE IT?  Because both RX_WR (advanced by the chip when new
 * data arrives) and RX_RD (advanced by the host CPU after consuming
 * data) can change independently between any two reads of RSR.  Storing
 * a cached value would require careful invalidation on every pointer
 * change.  Computing it fresh on each read is simpler and correct.
 *
 * The unsigned 16-bit subtraction naturally handles pointer wrap-around:
 * if RX_WR wraps past 0xFFFF before RX_RD does, the subtraction still
 * yields the correct positive byte count.
 *
 * TX_FSR (TX Free Size) is computed by the same principle:
 *   FSR = tx_size - (uint16_t)(TX_WR - TX_RD)
 *
 * ══════════════════════════════════════════════════════════════════════
 * INTERRUPT REGISTER SEMANTICS: WRITE-ONE-TO-CLEAR
 * ══════════════════════════════════════════════════════════════════════
 *
 * Both the global IR (0x0015) and per-socket Sn_IR (+0x02) use
 * "write-one-to-clear" (W1C) semantics — the same convention used by
 * most hardware interrupt status registers:
 *
 *   To CLEAR bit n: write a byte with bit n = 1.
 *   To LEAVE bit n: write a byte with bit n = 0.
 *   Bits set to 0 in the write value are unchanged.
 *
 * IMPLEMENTATION: The clear must be implemented as:
 *   mem[addr] &= ~val;
 *
 * NOT as:
 *   mem[addr] = val;   ← WRONG: would overwrite other pending bits
 *   mem[addr] &= ~val; ← then clear: this sequence is also wrong because
 *                         the first write already clobbers pending bits
 *
 * WHY THIS MATTERS: The host CPU typically reads IR, then writes the
 * same value back to acknowledge all observed interrupts.  If new
 * interrupts arrive between the read and the write, a plain store would
 * silently drop them.  The &= ~val approach only clears bits the writer
 * explicitly acknowledged, preserving any new bits that arrived.
 *
 * ══════════════════════════════════════════════════════════════════════
 * PROCESS_SOCKETS POLLING ARCHITECTURE
 * ══════════════════════════════════════════════════════════════════════
 *
 * The real W5100 handles network I/O in hardware and uses hardware
 * interrupts to notify the host.  In this emulation, there is no
 * hardware interrupt line.  Instead, we poll SDL3_net when the guest
 * reads Sn_SR or Sn_RX_RSR (the two registers Marinetti polls in its
 * receive loop).  This creates a natural coupling: the guest gets fresh
 * data precisely when it asks for it.
 *
 * The poll is rate-limited to at most once per millisecond (SDL_GetTicks
 * resolution) to prevent thrashing SDL3_net in tight polling loops.  At
 * 60Hz the guest polls roughly every 16ms, well within this limit.
 *
 * ASYNC CONNECT COMPLETION
 * ────────────────────────
 * NET_CreateClient() returns immediately with a socket handle; the TCP
 * three-way handshake completes asynchronously.  We track this with
 * connect_pending = true and call NET_WaitUntilConnected(sock, 0) each
 * poll.  Return values:
 *   +1 → connected; update Sn_SR = ESTABLISHED, set Sn_IR_CON
 *    0 → still in progress; try again next poll
 *   -1 → failed; close socket, set Sn_IR_TIMEOUT, Sn_SR = CLOSED
 */

#pragma once

#include <cstdint>
#include <SDL3/SDL.h>
#include <SDL3_net/SDL_net.h>
#include "vnat.hpp"

// ── W5100 Common Registers (0x0000–0x002F) ─────────────────────────
//
// These registers configure the chip globally and are shared across all
// four sockets.  They live at the very bottom of the 32KB address space.

#define W5100_MR          0x0000   // Mode Register: IND, AI, PPPOE, PB, RST
#define W5100_GAR         0x0001   // Gateway IP (4 bytes) — used for routing
#define W5100_SUBR        0x0005   // Subnet Mask (4 bytes)
#define W5100_SHAR        0x0009   // Source MAC Address (6 bytes)
#define W5100_SIPR        0x000F   // Source (chip) IP Address (4 bytes)
#define W5100_IR          0x0015   // Interrupt Register (W1C — write-one-to-clear)
#define W5100_IMR         0x0016   // Interrupt Mask (1=enable, 0=suppress)
#define W5100_RTR         0x0017   // Retry Time (2 bytes, units of 100µs; default 2000=200ms)
#define W5100_RCR         0x0019   // Retry Count (default 8)
#define W5100_RMSR        0x001A   // RX Memory Size: 2-bit field per socket, bits[2i+1:2i]=socket_i
#define W5100_TMSR        0x001B   // TX Memory Size: same encoding as RMSR
#define W5100_PATR        0x001C   // PPPoE Auth Type (2 bytes; not emulated)
#define W5100_PTIMER      0x0028   // PPP LCP Timer (not emulated)
#define W5100_PMAGIC      0x0029   // PPP Magic Number (not emulated)
#define W5100_UIPR        0x002A   // Unreachable IP (4 bytes; set on ICMP port-unreach)
#define W5100_UPORT       0x002E   // Unreachable Port (2 bytes; set on ICMP port-unreach)

// ── Mode Register Bits ──────────────────────────────────────────────
//
// MR controls the chip's bus interface and global behaviors.
// On Uthernet II, MR_IND is always set (hardware wired).

#define W5100_MR_RST      0x80     // Software reset: clear all state; self-clearing
#define W5100_MR_PB       0x10     // Ping block: silently drop incoming ICMP echo requests
#define W5100_MR_PPPOE    0x08     // PPPoE mode enable (not emulated)
#define W5100_MR_AI       0x02     // Auto-increment: DATA port advances addr pointer each access
#define W5100_MR_IND      0x01     // Indirect bus interface: access chip via ADDR_HI/LO/DATA ports

// ── Interrupt Register Bits ─────────────────────────────────────────
//
// IR is a global interrupt summary register.  Bits [3:0] mirror the
// corresponding Sn_IR "any interrupt pending" state for sockets 0–3.
// Bits [7:4] signal chip-level events (IP conflict, unreachable, PPPoE).
// All bits are W1C: write a 1 to clear, write a 0 to leave unchanged.

#define W5100_IR_CONFLICT 0x80     // IP address conflict detected on network
#define W5100_IR_UNREACH  0x40     // Destination unreachable (ICMP type 3 received)
#define W5100_IR_PPPOE    0x20     // PPPoE session closed
#define W5100_IR_S3       0x08     // Socket 3 has a pending interrupt (mirrors Sn_IR != 0)
#define W5100_IR_S2       0x04     // Socket 2 has a pending interrupt
#define W5100_IR_S1       0x02     // Socket 1 has a pending interrupt
#define W5100_IR_S0       0x01     // Socket 0 has a pending interrupt

// ── Socket Register Block ───────────────────────────────────────────
//
// The four socket register blocks occupy 0x0400–0x07FF.  Each block
// is 256 bytes, regardless of which registers within the 256 are
// defined.  The remaining bytes in each 256-byte slot are reserved.
//
// Socket n base address = W5100_SREG_BASE + n * W5100_SREG_SIZE

#define W5100_SREG_BASE   0x0400   // Socket register area starts here
#define W5100_SREG_SIZE   0x0100   // 256 bytes reserved per socket

// Socket register offsets (relative to socket n's base address)
// Access as: mem[W5100_SREG_BASE + sn*W5100_SREG_SIZE + offset]

#define W5100_Sn_MR       0x00     // Mode: protocol (bits [3:0]), flags (bits [7:4])
#define W5100_Sn_CR       0x01     // Command: write to initiate an action; self-clears to 0x00
#define W5100_Sn_IR       0x02     // Socket Interrupt: CON/DISCON/RECV/TIMEOUT/SENDOK (W1C)
#define W5100_Sn_SR       0x03     // Socket Status: read-only state machine value
#define W5100_Sn_PORT     0x04     // Source port number (2 bytes, big-endian)
#define W5100_Sn_DHAR     0x06     // Destination MAC address (6 bytes; used in MACRAW/UDP multicast)
#define W5100_Sn_DIPR     0x0C     // Destination IP address (4 bytes)
#define W5100_Sn_DPORT    0x10     // Destination port number (2 bytes, big-endian)
#define W5100_Sn_MSSR     0x12     // Maximum Segment Size for TCP (2 bytes; 0=use default)
#define W5100_Sn_PROTO    0x14     // IP protocol number for IPRAW mode
#define W5100_Sn_TOS      0x15     // IP Type of Service byte
#define W5100_Sn_TTL      0x16     // IP Time to Live (default 0x80 = 128 hops)
#define W5100_Sn_TX_FSR   0x20     // TX Free Size (2 bytes, read-only, computed on read)
#define W5100_Sn_TX_RD    0x22     // TX Read Pointer (2 bytes): chip advances after transmit
#define W5100_Sn_TX_WR    0x24     // TX Write Pointer (2 bytes): host advances after filling TX buf
#define W5100_Sn_RX_RSR   0x26     // RX Received Size (2 bytes, read-only, computed on read)
#define W5100_Sn_RX_RD    0x28     // RX Read Pointer (2 bytes): host advances after consuming
#define W5100_Sn_RX_WR    0x2A     // RX Write Pointer (2 bytes): chip advances after buffering

// ── Socket Mode Register Values ─────────────────────────────────────
//
// Bits [3:0] of Sn_MR select the protocol.  Bits [7:4] are flag bits:
//   Sn_MR_ND (0x20): No Delayed ACK — TCP ACKs sent immediately
//   Sn_MR_MULTI (0x80): UDP multicast enable

#define W5100_Sn_MR_CLOSE    0x00  // Socket inactive (default state after reset)
#define W5100_Sn_MR_TCP      0x01  // TCP: reliable byte stream
#define W5100_Sn_MR_UDP      0x02  // UDP: unreliable datagrams
#define W5100_Sn_MR_IPRAW    0x03  // IPRAW: raw IP packets (bypasses TCP/UDP)
#define W5100_Sn_MR_MACRAW   0x04  // MACRAW: raw Ethernet frames (Socket 0 only)
#define W5100_Sn_MR_PPPOE    0x05  // PPPoE: PPP over Ethernet (Socket 0 only; not emulated)
#define W5100_Sn_MR_PROTO_MASK 0x0F // Mask for protocol field in Sn_MR
#define W5100_Sn_MR_ND       0x20  // No Delayed ACK: TCP responds immediately (reduces latency)
#define W5100_Sn_MR_MULTI    0x80  // UDP multicast mode (use with Sn_DHAR for group MAC)

// ── Socket Command Values ───────────────────────────────────────────
//
// Commands are written to Sn_CR.  The chip begins executing immediately
// and clears Sn_CR to 0x00 when done.  Software should poll Sn_CR == 0
// before issuing the next command (though fast chips clear it near-instantly).

#define W5100_Sn_CR_OPEN      0x01  // Initialize socket; allocate resources; set status
#define W5100_Sn_CR_LISTEN    0x02  // TCP server: wait for incoming SYN
#define W5100_Sn_CR_CONNECT   0x04  // TCP client: initiate connection to Sn_DIPR:Sn_DPORT
#define W5100_Sn_CR_DISCON    0x08  // TCP graceful close: send FIN, wait for FIN-ACK
#define W5100_Sn_CR_CLOSE     0x10  // Unconditional close: release resources, status = CLOSED
#define W5100_Sn_CR_SEND      0x20  // Transmit data from TX buffer (TX_RD to TX_WR)
#define W5100_Sn_CR_SEND_MAC  0x21  // MACRAW: send with destination MAC from Sn_DHAR (not emulated)
#define W5100_Sn_CR_SEND_KEEP 0x22  // TCP: send keepalive packet (not emulated)
#define W5100_Sn_CR_RECV      0x40  // Acknowledge that host has advanced RX_RD (no-op in emulation)

// ── Socket Status Values ────────────────────────────────────────────
//
// Sn_SR is a read-only register driven by the chip's per-socket state
// machine.  The host CPU polls it to track connection progress.
// Software must never write to Sn_SR.
//
// TCP state progression (client):
//   CLOSED → [OPEN] → INIT → [CONNECT] → SYNSENT → ESTABLISHED
//   ESTABLISHED → [DISCON] → FIN_WAIT → TIME_WAIT → CLOSED
//   ESTABLISHED → [remote close] → CLOSE_WAIT → LAST_ACK → CLOSED

#define W5100_SOCK_CLOSED       0x00  // No resources allocated
#define W5100_SOCK_INIT         0x13  // TCP socket initialized, not yet connected
#define W5100_SOCK_LISTEN       0x14  // TCP server waiting for incoming SYN
#define W5100_SOCK_SYNSENT      0x15  // TCP SYN transmitted, waiting for SYN-ACK
#define W5100_SOCK_SYNRECV      0x16  // TCP SYN received (server), sending SYN-ACK
#define W5100_SOCK_ESTABLISHED  0x17  // TCP connection fully established; data flows
#define W5100_SOCK_FIN_WAIT     0x18  // FIN sent, waiting for remote FIN
#define W5100_SOCK_CLOSING      0x1A  // Simultaneous close in progress
#define W5100_SOCK_TIME_WAIT    0x1B  // Waiting 2×MSL before final close
#define W5100_SOCK_CLOSE_WAIT   0x1C  // Remote sent FIN; local close pending
#define W5100_SOCK_LAST_ACK     0x1D  // FIN sent in response to remote FIN; waiting for ACK
#define W5100_SOCK_UDP          0x22  // UDP socket open and bound
#define W5100_SOCK_IPRAW        0x32  // IPRAW socket open
#define W5100_SOCK_MACRAW       0x42  // MACRAW socket open (Socket 0 only)

// ── Socket Interrupt Bits ───────────────────────────────────────────
//
// Sn_IR bits are W1C (write-one-to-clear).  Multiple events can be
// pending simultaneously; the host clears each event it handles.
// Implementation: mem[addr] &= ~val  (NOT: mem[addr] = val)

#define W5100_Sn_IR_SENDOK    0x10  // Last SEND command completed successfully
#define W5100_Sn_IR_TIMEOUT   0x08  // Connection attempt or send timed out (retries exhausted)
#define W5100_Sn_IR_RECV      0x04  // Data available in RX buffer (RX_RSR > 0)
#define W5100_Sn_IR_DISCON    0x02  // Remote peer closed the connection (TCP FIN received)
#define W5100_Sn_IR_CON       0x01  // TCP connection established (SYNSENT → ESTABLISHED transition)

// ── Memory Map ──────────────────────────────────────────────────────
//
// The W5100 internal 32KB space is addressed via the indirect port.
// The TX and RX buffer regions occupy the upper 16KB (0x4000–0x7FFF).
// Each 8KB pool is divided among the four sockets per TMSR/RMSR.

#define W5100_TX_BASE     0x4000   // TX buffer pool starts here
#define W5100_TX_END      0x6000   // TX buffer pool ends here (exclusive)
#define W5100_RX_BASE     0x6000   // RX buffer pool starts here
#define W5100_RX_END      0x8000   // RX buffer pool ends here (exclusive; also end of address space)
#define W5100_MEM_SIZE    0x8000   // Total emulated address space (32KB)

#define W5100_NUM_SOCKETS 4        // W5100 always has exactly 4 sockets
#define W5100_TX_BUF_SIZE 8192     // Total TX memory pool size (shared among sockets)
#define W5100_RX_BUF_SIZE 8192     // Total RX memory pool size (shared among sockets)

// ── Per-socket state ────────────────────────────────────────────────
//
// This struct holds state that cannot be represented in the W5100
// register file: the host-side OS socket handles and the derived
// buffer geometry values (computed from TMSR/RMSR on every write).
//
// Buffer geometry fields are cached here to avoid recomputing the
// bit-field extraction on every ring buffer access.

struct w5100_socket_t {
    // Host-side network connections via SDL3_net.
    // Exactly one of tcp_socket or udp_socket is non-null when the
    // socket is open; both are null when closed or in MACRAW mode.
    NET_StreamSocket *tcp_socket = nullptr;
    NET_DatagramSocket *udp_socket = nullptr;
    NET_Address *resolve_addr = nullptr;  // temporary; held during resolution only

    // Buffer geometry — derived from TMSR/RMSR and recomputed on write.
    // These define where in w5100_state_t::mem the socket's ring buffer lives.
    //
    // Physical address formula:  phys = base + (pointer & mask)
    // where pointer is TX_WR, TX_RD, RX_WR, or RX_RD (raw 16-bit counter).
    uint16_t tx_base = 0;   // Absolute address within mem[] where TX ring starts
    uint16_t tx_size = 0;   // Power-of-2 size: 1024, 2048, 4096, or 8192 bytes
    uint16_t tx_mask = 0;   // tx_size - 1: bitmask for ring-buffer modulo (phys offset)
    uint16_t rx_base = 0;   // Absolute address within mem[] where RX ring starts
    uint16_t rx_size = 0;   // Power-of-2 size: 1024, 2048, 4096, or 8192 bytes
    uint16_t rx_mask = 0;   // rx_size - 1: bitmask for ring-buffer modulo (phys offset)

    // Async TCP connect in progress.
    // Set to true in CONNECT command handling; cleared when
    // NET_WaitUntilConnected() returns non-zero (success or failure).
    bool connect_pending = false;
};

// ── Chip state ──────────────────────────────────────────────────────
//
// The complete emulated W5100 state.  A single instance of this struct
// represents one W5100 chip (one Uthernet II card in one slot).

struct w5100_state_t {
    // The full 32KB register and buffer memory array.
    // Common registers, socket registers, TX buffers, and RX buffers
    // all live here, accessed by their 16-bit addresses.
    uint8_t mem[W5100_MEM_SIZE] = {};

    // Indirect access address pointer.
    // Latched by writes to the ADDR_HI/ADDR_LO slot I/O ports.
    // The DATA port reads/writes mem[addr]; with MR_AI set, addr
    // increments automatically after each DATA access.
    uint16_t addr = 0;

    // Mode register (MR) shadow.
    // Kept separate from mem[0] so that the indirect access machinery
    // can be configured before the first DATA-port cycle; also simplifies
    // reset handling since MR is written before the memory array is zeroed.
    uint8_t mode_reg = 0;

    // Per-socket OS handles and buffer geometry.
    w5100_socket_t sockets[W5100_NUM_SOCKETS] = {};

    // Virtual NAT engine for MACRAW mode.
    // Translates between raw Ethernet frames (sent/received by the guest)
    // and host-OS TCP/IP connections, providing ARP, ICMP, DNS, and TCP/UDP
    // bridging without requiring raw socket privileges on the host.
    vnat_state_t vnat;

    // SDL3_net initialization guard.
    // NET_Init() is called once; this flag prevents double-initialization.
    bool net_initialized = false;
};

// ── Public API ──────────────────────────────────────────────────────
//
// These four functions are the complete external interface to the W5100
// emulation.  The Uthernet II device layer calls them from its slot I/O
// handlers and its per-frame update path.

// Initialize the chip: call NET_Init(), init VNAT, then reset.
// Safe to call multiple times; NET_Init is guarded by net_initialized.
void w5100_init(w5100_state_t *w);

// Reset chip to power-on state: close all sockets, clear mem[],
// write datasheet-defined default register values, recalculate geometry.
void w5100_reset(w5100_state_t *w);

// Read one byte from the W5100 internal address space.
// Dynamic values (TX_FSR, RX_RSR) are computed on read.
// Status/RX_RSR reads trigger process_sockets() (rate-limited to 1ms).
uint8_t w5100_read(w5100_state_t *w, uint16_t addr);

// Write one byte to the W5100 internal address space.
// Commands (Sn_CR) trigger exec_socket_cmd() immediately.
// IR/Sn_IR writes use W1C semantics (mem &= ~val).
// TMSR/RMSR writes trigger recalc_buffer_geometry().
void w5100_write(w5100_state_t *w, uint16_t addr, uint8_t val);

// Poll all sockets for network events.
// Called from w5100_read() when guest reads Sn_SR or Sn_RX_RSR.
// Completes async TCP connects, buffers incoming TCP/UDP/MACRAW data.
void w5100_process_sockets(w5100_state_t *w);
