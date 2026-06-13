/*
 *   Uthernet II — W5100-based Ethernet card for Apple IIgs
 *
 *   This file declares the device-level data structure and the slot
 *   initialization entry point for the Uthernet II emulation.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   HARDWARE BACKGROUND: The Uthernet II Card
 *   ──────────────────────────────────────────────────────────────────
 *
 *   The Uthernet II (designed by Jonno Downes, 2012) is a Wiznet W5100
 *   hardware TCP/IP offload card for the Apple II expansion slot bus.
 *   The W5100 is an Ethernet controller that implements the full TCP/IP
 *   stack in silicon, exposing a simple register/memory interface to the
 *   host CPU.  From the Apple II's perspective it is a dumb memory-mapped
 *   peripheral; all protocol complexity lives inside the chip.
 *
 *   The card was designed to be compatible with Marinetti, the IIgs TCP/IP
 *   stack (written by Richard Bennett, Apple II networking community).
 *   Marinetti queries the slot firmware byte to identify the card, then
 *   drives it via the 4 I/O registers described below.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   APPLE II SLOT I/O ADDRESS SPACE
 *   ──────────────────────────────────────────────────────────────────
 *
 *   The Apple II (IIe and IIgs) reserves 16 bytes of I/O space for each
 *   of the 8 expansion slots (slots 1–7):
 *
 *     Slot n device-select I/O: $C0n0 – $C0nF  (16 addresses)
 *
 *   For example:
 *     Slot 1 → $C090–$C09F
 *     Slot 2 → $C0A0–$C0AF
 *     Slot 3 → $C0B0–$C0BF
 *     Slot 4 → $C0C0–$C0CF      ← Uthernet II default placement
 *     Slot 5 → $C0D0–$C0DF
 *     Slot 6 → $C0E0–$C0EF
 *     Slot 7 → $C0F0–$C0FF
 *
 *   The general formula is:  base = $C080 + (slot × $10)
 *
 *   In GSSquared, `computer->mmu` is the MMU_II (MegaII) object that
 *   manages the IIe-compatibility layer.  `set_C0XX_read_handler` and
 *   `set_C0XX_write_handler` install callbacks into a flat dispatch table
 *   indexed by the low 8 bits of the $C0xx address (0x00–0xFF).  The
 *   handlers receive the full 16-bit address and a caller-supplied context
 *   pointer.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   WHY SLOT 4 (NOT SLOT 3)?
 *   ──────────────────────────────────────────────────────────────────
 *
 *   Slot 3 is the traditional home for the Apple IIe 80-column card,
 *   and more importantly, the IIgs ROM maps its internal 80-column
 *   firmware into the $C300–$C3FF slot ROM window when INTCXROM is
 *   clear.  The IIgs MegaII layer (MMU_IIe) also installs internal ROM
 *   for slot 3 via set_slot_rom(SLOT_3, ...) during init_map(), which
 *   would conflict with any device firmware placed there.
 *
 *   Marinetti historically defaulted to slot 2 on the IIe but switched
 *   to slot 4 as the recommended Uthernet II slot on the IIgs, where
 *   slot 4's $C400 firmware region has no built-in conflict.
 *
 *   Additionally, the A2GSPU video card occupies slot 3 in the combined
 *   system configurations (slot 3 is the standard video card slot on
 *   the IIgs, matching where a VideoOverlay or VidHD would live), so
 *   Uthernet II lives in slot 4 to avoid any possibility of collision.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   THE W5100'S INDIRECT REGISTER INTERFACE
 *   ──────────────────────────────────────────────────────────────────
 *
 *   The W5100 has a 32KB internal address space (0x0000–0x7FFF) divided
 *   into common registers, socket registers, TX buffers, and RX buffers.
 *   On most microprocessors the W5100 can be wired in "direct bus" mode
 *   where A0–A15 directly address its registers.  That would require 32KB
 *   of address space, which the Apple II's 16-byte slot window cannot
 *   provide.
 *
 *   The W5100 therefore offers an alternative: "indirect bus" mode (MR
 *   bit 0 = IND = 1).  In indirect mode the chip exposes exactly 4 byte
 *   ports to the host:
 *
 *     Port 0 (MR)      — Mode Register: direct, always accessible
 *     Port 1 (ADDR_HI) — High byte of 16-bit address pointer
 *     Port 2 (ADDR_LO) — Low byte of 16-bit address pointer
 *     Port 3 (DATA)    — Read/write a single byte at [ADDR_HI:ADDR_LO]
 *
 *   Typical access sequence to write a register at internal address A:
 *     1. Write A >> 8 to ADDR_HI
 *     2. Write A & 0xFF to ADDR_LO
 *     3. Write value to DATA
 *
 *   With auto-increment (MR bit 1 = AI = 1) set, DATA accesses advance
 *   the pointer automatically, so sequential registers can be written
 *   with a single ADDR_HI/ADDR_LO setup followed by repeated DATA writes.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   WHY ONLY A0–A1 ARE DECODED (4 REGISTERS × 4 MIRRORS = 16 ADDRESSES)
 *   ──────────────────────────────────────────────────────────────────
 *
 *   In indirect mode the W5100's physical bus needs only 2 address lines
 *   (A0–A1) to select among the 4 ports (00, 01, 10, 11).  The Uthernet II
 *   hardware connects these two lines to the Apple II's A0 and A1, and
 *   leaves A2–A3 unconnected (not decoded).  As a result, the same 4
 *   registers appear 4 times across the 16-byte slot window:
 *
 *     $C0C0 = MR,      $C0C4 = MR,      $C0C8 = MR,      $C0CC = MR
 *     $C0C1 = ADDR_HI, $C0C5 = ADDR_HI, $C0C9 = ADDR_HI, $C0CD = ADDR_HI
 *     $C0C2 = ADDR_LO, $C0C6 = ADDR_LO, $C0CA = ADDR_LO, $C0CE = ADDR_LO
 *     $C0C3 = DATA,    $C0C7 = DATA,    $C0CB = DATA,    $C0CF = DATA
 *
 *   Our emulator registers a handler for all 16 addresses and masks the
 *   address down to bits 0–1 with (address & 0x03) before dispatch.
 *
 *   ──────────────────────────────────────────────────────────────────
 *   SlotData BASE CLASS
 *   ──────────────────────────────────────────────────────────────────
 *
 *   Every GSSquared slot device inherits SlotData, which carries:
 *     - `id`     (device_id enum) — identifies the device type for UI/config
 *     - `_slot`  (SlotType_t)     — which physical slot the device occupies
 *
 *   This allows the UI (SlotButton, OSD) and the slot system (slots.cpp)
 *   to query any slot for its device identity via a uniform base pointer,
 *   without knowing the concrete device type.
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#pragma once

#include "computer.hpp"
#include "w5100.hpp"

/*
 * uthernet2_data — per-slot instance state
 *
 * One instance is heap-allocated per init_slot_uthernet2() call and lives
 * until the shutdown handler fires.  It bundles three things:
 *
 *   computer   — back-pointer used to reach the MMU and register lifecycle
 *                hooks.  Not used after init; stored for completeness.
 *
 *   w5100      — the full W5100 chip state (32KB register/memory file,
 *                per-socket SDL3_net handles, buffer geometry, VNAT).
 *                This is the core of the emulation; see w5100.hpp/.cpp.
 *
 *   slot_base  — the base $C0xx address for this slot's I/O window
 *                ($C080 + slot × $10).  Stored so the shutdown handler
 *                knows which 16 addresses it owns, though in practice it
 *                is not used after init because GSSquared does not support
 *                hot un-plugging of slot devices.
 *
 * Inheriting SlotData sets `id = DEVICE_ID_UTHERNET2` in the constructor,
 * making this instance visible to the slot query API.
 */
struct uthernet2_data : public SlotData {
    computer_t *computer = nullptr;
    w5100_state_t w5100;
    uint16_t slot_base = 0;    // $C080 + slot * $10

    uthernet2_data() {
        id = DEVICE_ID_UTHERNET2;
    }
};

/*
 * init_slot_uthernet2 — install Uthernet II into the given slot
 *
 * Called once at system configuration time (from devices.cpp, which
 * dispatches per-DEVICE_ID init functions keyed by the system config
 * slot table).
 *
 * Responsibilities:
 *   1. Allocate uthernet2_data on the heap (lives until shutdown).
 *   2. Initialize the W5100 chip state (clears memory, sets IND mode,
 *      initializes SDL3_net once).
 *   3. Register read/write handlers for all 16 slot I/O addresses with
 *      the MegaII MMU's C0XX dispatch table.
 *   4. Register a shutdown lambda that destroys all SDL3_net sockets,
 *      resets VNAT, and deletes the uthernet2_data object.
 *   5. Register a reset lambda that cold-resets the W5100 chip state
 *      (and closes all sockets) when the user hits the reset button.
 */
void init_slot_uthernet2(computer_t *computer, SlotType_t slot);
