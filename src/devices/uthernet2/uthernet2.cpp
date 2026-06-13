/*
 *   Uthernet II Slot I/O Handlers
 *
 *   This file implements the thin glue layer between the Apple II slot I/O
 *   bus and the W5100 chip emulation.  It is intentionally small: the
 *   hardware interface is just 4 byte-wide registers, so this file's main
 *   job is to decode the Apple II address into one of those registers,
 *   forward reads and writes to w5100.cpp, and manage the GSSquared
 *   device plugin lifecycle (init, shutdown, reset).
 *
 *   ──────────────────────────────────────────────────────────────────
 *   HOW THE UTHERNET II HARDWARE INTERFACE WORKS
 *   ──────────────────────────────────────────────────────────────────
 *
 *   The W5100 has a 32KB internal address space, but the Apple II slot
 *   I/O window is only 16 bytes wide ($C0n0–$C0nF for slot n).  The
 *   Uthernet II card solves this by wiring the W5100 in indirect bus mode:
 *   only 4 register ports are exposed to the Apple II, and the Apple II
 *   uses those ports to set a 16-bit address pointer and then read/write
 *   one byte at a time through a DATA port.  This is analogous to how
 *   the 6522 VIA or SCC 8530 are programmed through a small window of
 *   address/data registers.
 *
 *   The 4 register ports, their hardware addresses (for slot 4), and
 *   their purposes:
 *
 *     +0 ($C0C0) MR      — Mode Register.  Direct access (not indirect).
 *                          Bit 7 (RST): software reset, self-clearing.
 *                          Bit 1 (AI): auto-increment after DATA access.
 *                          Bit 0 (IND): indirect mode.  Always 1 on
 *                          Uthernet II (the hardware wires this).
 *
 *     +1 ($C0C1) ADDR_HI — High byte of the 16-bit W5100 address pointer.
 *                          Write only (reading it back returns the latch).
 *
 *     +2 ($C0C2) ADDR_LO — Low byte of the 16-bit W5100 address pointer.
 *
 *     +3 ($C0C3) DATA    — Read or write one byte at the current address
 *                          pointer.  If AI bit is set in MR, the pointer
 *                          increments after the access (with TX/RX buffer
 *                          boundary wraparound).
 *
 *   ──────────────────────────────────────────────────────────────────
 *   WHY ONLY A0–A1 ARE DECODED
 *   ──────────────────────────────────────────────────────────────────
 *
 *   In indirect mode the W5100 chip itself only needs 2 address lines
 *   (A0–A1) to distinguish its 4 ports.  The Uthernet II PCB connects
 *   the Apple II's A0 and A1 to the W5100's BA0 and BA1, and the other
 *   address lines (A2–A3) go unconnected.  This means bits 2 and 3 of
 *   the offset within the 16-byte slot window are ignored by the chip,
 *   and the same 4 registers mirror at offsets +0/+4/+8/+12.
 *
 *   Our emulation replicates this by:
 *     (a) registering handlers for all 16 addresses, and
 *     (b) reducing the incoming address to bits 0–1 with (address & 0x03)
 *         before the register switch, exactly as the hardware does.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   WHY MR ALWAYS HAS IND BIT SET
 *   ──────────────────────────────────────────────────────────────────
 *
 *   On a real Uthernet II the W5100's BUSMODE pin is hardwired to
 *   select indirect mode at power-on, and the IND bit in MR (bit 0)
 *   cannot be cleared — the hardware simply ignores attempts to clear it.
 *   Our emulation enforces this by ORing W5100_MR_IND into every MR
 *   write (see the UTHERNET2_REG_MR case in uthernet2_write()).  The
 *   W5100 also mirrors MR in mem[W5100_MR] (address 0x0000), so we
 *   update both the fast-path `mode_reg` field and the memory-backed copy.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   AUTO-INCREMENT AND TX/RX BUFFER WRAPAROUND
 *   ──────────────────────────────────────────────────────────────────
 *
 *   When the AI bit (MR bit 1) is set, the address pointer increments
 *   after every DATA port read or write.  This allows the Apple II to
 *   pump or drain sequential bytes — a TX or RX buffer region — with
 *   a single ADDR_HI/ADDR_LO setup and N DATA accesses, avoiding the
 *   overhead of re-setting the address pointer before every byte.
 *   Marinetti uses this heavily: writing a packet into the TX buffer
 *   sets the base address once and then writes all payload bytes via
 *   repeated DATA writes.
 *
 *   The W5100 TX buffer occupies addresses 0x4000–0x5FFF and the RX
 *   buffer 0x6000–0x7FFF (in the default 8KB/8KB single-socket layout;
 *   the actual per-socket boundaries depend on TMSR/RMSR).  If the
 *   address pointer reaches the end of a buffer region during
 *   auto-increment, it wraps back to the start of that same region
 *   rather than spilling into the next region or the common registers.
 *   This ring-buffer wraparound is defined in the W5100 datasheet as
 *   part of the indirect-mode auto-increment specification.
 *
 *   Our auto_increment() function implements exactly this wraparound:
 *   it checks whether the post-increment address equals W5100_TX_END
 *   (0x6000) and if so resets to W5100_TX_BASE (0x4000), and likewise
 *   for the RX boundary.  Note that the wraparound is only applied at
 *   the top-level buffer boundaries (TX_BASE/TX_END, RX_BASE/RX_END),
 *   not at per-socket sub-buffer boundaries; the W5100 datasheet
 *   specifies this behavior and it matches the actual hardware.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   RST BIT HANDLING
 *   ──────────────────────────────────────────────────────────────────
 *
 *   Writing a byte to MR with bit 7 (W5100_MR_RST) set triggers a
 *   software reset: the W5100 clears all registers to power-on defaults,
 *   closes all sockets, and resets the VNAT state.  The RST bit is
 *   self-clearing on the real chip (it reads back as 0 after reset), and
 *   we model this by calling w5100_reset() instead of storing the value.
 *   If RST is not set, the value (with IND forced) is stored normally.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   GSSQUARED DEVICE PLUGIN PATTERN
 *   ──────────────────────────────────────────────────────────────────
 *
 *   GSSquared follows a consistent pattern for slot devices:
 *
 *   1. An init function (init_slot_X) is called at system startup,
 *      identified by DEVICE_ID_X in the system config slot table.
 *      The devices.cpp dispatch table maps device IDs to init functions.
 *
 *   2. The init function allocates a per-slot state struct (a SlotData
 *      subclass) on the heap.  This struct owns the device's entire
 *      runtime state and is passed as the `context` pointer to every
 *      I/O handler callback.
 *
 *   3. I/O handlers are registered with the MegaII MMU (MMU_II /
 *      MMU_IIe) via set_C0XX_read_handler / set_C0XX_write_handler.
 *      These install function pointers + context into a flat C0XX
 *      dispatch table.  When the CPU executes a read or write to a
 *      $C0xx address, the MMU looks up the handler by address offset
 *      and calls it with the context pointer.
 *
 *   4. A shutdown lambda is registered with computer->register_shutdown_
 *      handler().  It is called when GSSquared exits cleanly.  It must
 *      release all OS resources (sockets, resolved addresses) and delete
 *      the state struct.
 *
 *   5. A reset lambda is registered with computer->register_reset_handler().
 *      It is called when the user presses the reset button (or the
 *      emulator performs a cold start).  For Uthernet II, reset closes
 *      all sockets and clears all W5100 state, matching the hardware
 *      behavior of power-cycling the card.
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#include "uthernet2.hpp"
#include "mmus/mmu_ii.hpp"

/*
 * Register offsets within the 16-byte slot I/O window.
 *
 * Only bits 0–1 of the offset are significant (A0–A1 decoded).
 * Bits 2–3 are unconnected on the Uthernet II PCB, so the same 4
 * registers mirror at every 4-byte boundary across the 16-byte window.
 *
 * These constants are the canonical port numbers used in the switch
 * statements below after masking with (address & 0x03).
 */
#define UTHERNET2_REG_MR      0  // Mode Register (direct; always accessible)
#define UTHERNET2_REG_ADDR_HI 1  // Address pointer high byte
#define UTHERNET2_REG_ADDR_LO 2  // Address pointer low byte
#define UTHERNET2_REG_DATA    3  // Data port (indirect read/write at address pointer)

/*
 * auto_increment — advance the address pointer after a DATA port access
 *
 * Called after every DATA read or write when the AI (auto-increment) bit
 * is set in MR.  The pointer is unconditionally incremented first, then
 * checked against the TX and RX buffer end addresses.
 *
 * Wraparound semantics (from W5100 datasheet §5.1.2, Indirect Bus I/F):
 *   - If the pointer reaches W5100_TX_END (0x6000), reset to W5100_TX_BASE
 *     (0x4000).  This keeps sequential TX buffer writes within the TX
 *     region regardless of how many bytes are written.
 *   - If the pointer reaches W5100_RX_END (0x8000), reset to W5100_RX_BASE
 *     (0x6000).  Same rationale for RX.
 *   - Accesses outside the TX/RX region (common registers, socket regs)
 *     do not wrap; the caller is responsible for not auto-incrementing
 *     past the end of the common or socket register blocks.
 *
 * Note: only W5100_MR_AI is tested here, not W5100_MR_IND.  On the
 * Uthernet II, IND is always set (hardwired), but AI is optional and
 * controlled by software.  Marinetti enables it before buffer I/O and
 * may leave it enabled across multiple accesses.
 */
static void auto_increment(w5100_state_t *w) {
    if (!(w->mode_reg & W5100_MR_AI)) return;

    w->addr++;

    // Wrap at TX buffer end → TX buffer start
    if (w->addr == W5100_TX_END)
        w->addr = W5100_TX_BASE;
    // Wrap at RX buffer end → RX buffer start
    else if (w->addr == W5100_RX_END)
        w->addr = W5100_RX_BASE;
}

/*
 * uthernet2_read — Apple II slot I/O read handler
 *
 * Invoked by the MMU whenever the CPU reads from any of the 16 addresses
 * in this slot's I/O window ($C0n0–$C0nF for slot n).
 *
 * The `address` parameter is the full 16-bit bus address (e.g., 0xC0C3
 * for a DATA port read on slot 4).  We extract bits 0–1 to select the
 * register port.
 *
 * MR, ADDR_HI, ADDR_LO: return the current latch values directly.
 *   Reading ADDR_HI/ADDR_LO is unusual in practice (Marinetti rarely
 *   reads the address pointer back) but is architecturally valid on the
 *   W5100 and must work correctly.
 *
 * DATA: read one byte from the W5100's 32KB address space via w5100_read(),
 *   which handles dynamic register computation (TX_FSR, RX_RSR) and
 *   network polling on status/size register reads.  After the read, call
 *   auto_increment() to advance the pointer if AI is set.
 */
static uint8_t uthernet2_read(void *context, uint32_t address) {
    uthernet2_data *ud = (uthernet2_data *)context;
    w5100_state_t *w = &ud->w5100;
    uint8_t reg = address & 0x03;  // only A0-A1 decoded; bits 2-3 ignored

    switch (reg) {
        case UTHERNET2_REG_MR:
            // Return the mode register latch.  IND bit (0x01) is always set.
            return w->mode_reg;

        case UTHERNET2_REG_ADDR_HI:
            // Return high byte of the 16-bit address pointer.
            return (uint8_t)(w->addr >> 8);

        case UTHERNET2_REG_ADDR_LO:
            // Return low byte of the 16-bit address pointer.
            return (uint8_t)(w->addr & 0xFF);

        case UTHERNET2_REG_DATA: {
            // Read one byte from the W5100 at the current address pointer.
            // w5100_read() may trigger network I/O polling (for Sn_SR, RX_RSR
            // reads) and returns dynamically computed values for TX_FSR and
            // RX_RSR rather than stale in-memory bytes.
            uint8_t val = w5100_read(w, w->addr);
            // Advance the pointer after the read if AI is set.
            auto_increment(w);
            return val;
        }

        default:
            // Unreachable (reg is always 0-3), but satisfies the compiler.
            return 0;
    }
}

/*
 * uthernet2_write — Apple II slot I/O write handler
 *
 * Invoked by the MMU whenever the CPU writes to any of the 16 addresses
 * in this slot's I/O window.  Register selection is the same as above.
 *
 * MR write: two sub-cases:
 *   (a) RST bit (0x80) set — trigger a full W5100 software reset.
 *       The RST bit is self-clearing on the real chip; we model this by
 *       not storing the value, and instead calling w5100_reset() which
 *       reinitializes all state to power-on defaults.
 *   (b) RST bit clear — store the value but always force IND bit (0x01)
 *       set.  The Uthernet II hardware wires BUSMODE to select indirect
 *       mode permanently; software cannot disable it.  We mirror the new
 *       mode_reg value into mem[W5100_MR] (address 0x0000) so that a
 *       read of the W5100 MR via the indirect DATA port also reflects the
 *       current mode.
 *
 * ADDR_HI write: replace bits 15–8 of the address pointer.  The low byte
 *   is preserved.  Software typically writes ADDR_HI then ADDR_LO to
 *   set up the full address before DATA accesses.
 *
 * ADDR_LO write: replace bits 7–0 of the address pointer.  The high byte
 *   is preserved.
 *
 * DATA write: write one byte to the W5100's 32KB address space via
 *   w5100_write(), which handles register-specific side effects (socket
 *   command execution, buffer geometry recalculation, interrupt clear
 *   semantics, read-only register protection).  Then auto_increment().
 */
static void uthernet2_write(void *context, uint32_t address, uint8_t value) {
    uthernet2_data *ud = (uthernet2_data *)context;
    w5100_state_t *w = &ud->w5100;
    uint8_t reg = address & 0x03;  // only A0-A1 decoded

    switch (reg) {
        case UTHERNET2_REG_MR:
            if (value & W5100_MR_RST) {
                // RST bit set: perform a full software reset.
                // This closes all sockets, clears all W5100 registers to
                // power-on defaults, and resets the VNAT state.  The RST
                // bit is self-clearing — we do not store `value`.
                w5100_reset(w);
            } else {
                // Store new mode, forcing IND always set.
                // The Uthernet II hardwires indirect bus mode; software
                // cannot disable it.  OR in W5100_MR_IND to enforce this.
                w->mode_reg = value | W5100_MR_IND;
                // Mirror into the memory-backed register at address 0x0000
                // so that reading MR via the DATA port (indirect access to
                // address 0x0000) also returns the correct value.
                w->mem[W5100_MR] = w->mode_reg;
            }
            break;

        case UTHERNET2_REG_ADDR_HI:
            // Replace the high byte of the 16-bit address pointer.
            // Preserve the existing low byte to allow HI and LO writes
            // to be issued in either order without corrupting the other half.
            // Marinetti always writes HI-then-LO (the canonical W5100 ordering),
            // but we support either order for compatibility with other potential drivers.
            w->addr = ((uint16_t)value << 8) | (w->addr & 0x00FF);
            break;

        case UTHERNET2_REG_ADDR_LO:
            // Replace the low byte of the 16-bit address pointer.
            // Preserve the existing high byte.
            w->addr = (w->addr & 0xFF00) | value;
            break;

        case UTHERNET2_REG_DATA:
            // Write one byte to the W5100 at the current address pointer.
            // w5100_write() handles:
            //   - Socket Sn_CR writes: executes the socket command
            //     (OPEN, CONNECT, SEND, RECV, CLOSE, DISCON).
            //   - Sn_SR writes: ignored (status is read-only).
            //   - IR / Sn_IR writes: write-one-to-clear semantics.
            //   - TX_FSR, RX_RSR writes: ignored (computed read-only).
            //   - TMSR / RMSR writes: triggers buffer geometry recalc.
            //   - All other addresses: stored in mem[addr].
            w5100_write(w, w->addr, value);
            // Advance the pointer after the write if AI is set.
            auto_increment(w);
            break;
    }
}

/*
 * init_slot_uthernet2 — install Uthernet II emulation into the given slot
 *
 * This is the device plugin entry point, called once at emulator startup
 * from devices.cpp when the system configuration includes DEVICE_ID_UTHERNET2
 * in a slot.  The `slot` parameter is the physical slot number (1–7);
 * for Marinetti compatibility this is typically slot 4.
 *
 * ADDRESS FORMULA:
 *   Each slot's device-select I/O window starts at $C080 + (slot × $10).
 *   Slot 4: $C080 + 4×$10 = $C0C0–$C0CF.
 *   The 16 addresses in this window map to the 4 W5100 registers (mirrored
 *   4× because only A0–A1 are decoded), as described above.
 *
 * MMU REGISTRATION:
 *   `computer->mmu` is the MMU_II (MegaII) object, which owns the C0XX
 *   dispatch table.  set_C0XX_read_handler / set_C0XX_write_handler each
 *   take a (uint16_t address, handler_t) pair where handler_t is a struct
 *   containing a function pointer and a context pointer.  The MMU stores
 *   these in a flat array indexed by the low 8 bits of the address.
 *
 *   We register all 16 addresses in the loop, not just the 4 unique ones.
 *   This is both simpler and correct: the address decoding (& 0x03) in
 *   the handlers will reduce any of the 16 to the canonical port.
 *
 * SHUTDOWN HANDLER:
 *   Registered via computer->register_shutdown_handler() as a lambda.
 *   Called when GSSquared exits cleanly (not on crash).  Must:
 *     - Destroy all SDL3_net TCP and UDP sockets.
 *     - Release any pending NET_Address resolve handles.
 *     - Reset the VNAT subsystem (closes its internal connections).
 *     - Delete the heap-allocated uthernet2_data.
 *   Returning `true` signals success to the shutdown handler chain.
 *
 * RESET HANDLER:
 *   Registered via computer->register_reset_handler() as a lambda.
 *   Called when the Apple II reset button is pressed or the emulator
 *   performs a cold/warm start.  w5100_reset() closes all sockets and
 *   reinitializes the W5100 to power-on defaults — the equivalent of
 *   physically removing and reinserting the card.
 *   Returning `true` signals success to the reset handler chain.
 */
void init_slot_uthernet2(computer_t *computer, SlotType_t slot) {
    uthernet2_data *ud = new uthernet2_data();
    ud->computer = computer;
    // Compute the base address for this slot's 16-byte I/O window.
    // Slot 1 → $C090, slot 2 → $C0A0, ..., slot 4 → $C0C0, etc.
    ud->slot_base = 0xC080 + (uint16_t)slot * 0x10;

    // Initialize the W5100 chip: call NET_Init() once (guarded), set
    // power-on register defaults, initialize VNAT, and compute buffer
    // geometry from the default TMSR/RMSR (2KB per socket).
    w5100_init(&ud->w5100);

    // Register I/O handlers for all 16 addresses in this slot's window.
    // Only A0-A1 are decoded, so the 4 registers mirror across the window.
    // The handlers use (address & 0x03) to reduce any of the 16 addresses
    // to the canonical 4 register indices, matching the hardware behavior.
    for (int i = 0; i < 16; i++) {
        computer->mmu->set_C0XX_write_handler(ud->slot_base + i, {uthernet2_write, ud});
        computer->mmu->set_C0XX_read_handler(ud->slot_base + i, {uthernet2_read, ud});
    }

    // Shutdown handler: called when the emulator exits cleanly.
    // Releases all SDL3_net resources associated with each socket, then
    // resets VNAT (which closes its own internal TCP/UDP connections and
    // clears the response frame buffer), then frees the device state.
    // Each resource is explicitly nulled after destruction to prevent
    // double-free if (hypothetically) the handler were called twice.
    computer->register_shutdown_handler([ud]() {
        for (int i = 0; i < W5100_NUM_SOCKETS; i++) {
            if (ud->w5100.sockets[i].tcp_socket) {
                NET_DestroyStreamSocket(ud->w5100.sockets[i].tcp_socket);
                ud->w5100.sockets[i].tcp_socket = nullptr;
            }
            if (ud->w5100.sockets[i].udp_socket) {
                NET_DestroyDatagramSocket(ud->w5100.sockets[i].udp_socket);
                ud->w5100.sockets[i].udp_socket = nullptr;
            }
            if (ud->w5100.sockets[i].resolve_addr) {
                NET_UnrefAddress(ud->w5100.sockets[i].resolve_addr);
                ud->w5100.sockets[i].resolve_addr = nullptr;
            }
        }
        vnat_reset(&ud->w5100.vnat);
        delete ud;
        return true;
    });

    // Reset handler: called on Apple II reset (warm or cold start).
    // w5100_reset() closes all sockets, clears the 32KB W5100 memory file,
    // resets the address pointer to 0, restores power-on register defaults
    // (MR=IND, RTR=0x07D0, RCR=8, TMSR/RMSR=0x55 for 2KB×4), and
    // recalculates buffer geometry.  From Marinetti's perspective, the
    // card has been replugged.
    computer->register_reset_handler([ud](bool) {
        w5100_reset(&ud->w5100);
        return true;
    });
}
