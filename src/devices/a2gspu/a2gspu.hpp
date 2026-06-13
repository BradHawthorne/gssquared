/*
 *   A2GSPU - Apple IIgs Graphics and Sound Processing Unit
 *   Emulator device plugin for GSSquared
 *
 *   Two backends sharing a single write observer:
 *   - HW: streams bus writes over USB serial to physical RP2350 card
 *   - EMU: renders UHR modes locally via SDL3 (no hardware needed)
 *
 *   Copyright (c) 2026 Brad Hawthorne
 *   Licensed under GPL-3.0 (matching GSSquared)
 */

#pragma once

#include <string>
#include "computer.hpp"
#include "a2gspu_emu.hpp"
#include "a2gspu_proto.hpp"

class MMU_IIgs;
class display_state_t;
class KeyGloo;
class ADB_Mouse;
class VideoScannerII;

struct a2gspu_data : public SlotData {
    computer_t *computer = nullptr;
    cpu_state *cpu = nullptr;

    // ── Cached pointers (set once at init, except ds which is lazy) ──
    MMU_IIgs *mmu_gs = nullptr;       // IIgs MMU — NOT computer->mmu (that's MegaII)
    display_state_t *ds = nullptr;    // lazy-init: null during device init, set on first frame
    KeyGloo *keygloo = nullptr;       // for file-based keyboard injection (independent of user's KB)
    ADB_Mouse *adb_mouse = nullptr;   // for file-based mouse injection (independent of user's mouse)
    // NOT USED — intentionally left null after init.
    // The display system recreates VideoScannerII whenever the video mode
    // changes (e.g., switching between text and SHR), which invalidates any
    // raw pointer cached here.  Storing a stale pointer would be a silent
    // dangling-pointer bug: the old scanner object is destroyed, but the
    // pointer still looks valid.  Instead, code that needs the scanner reads
    // ds->video_scanner directly each frame; ds itself is stable for the
    // lifetime of the computer and always holds the current scanner pointer.
    VideoScannerII *video_scanner = nullptr;

    // ── HW backend (USB serial to physical card) ───────────────
    void *serial_handle = nullptr;    // HANDLE on Windows. Null when disconnected; auto-reconnect
                                      // tries COM ports every ~1s until a CDC device is found.

    // ── EMU backend (local UHR rendering) ──────────────────────
    a2gspu_emu_state emu;             // shadows SHR memory via write observer for local rendering

    // ── Mosaic slot I/O protocol ────────────────────────────────
    a2gspu_proto_state proto;         // register-mapped tile commands from IIgs 6502 code

    // ── Key injection buffer (drains a few chars per frame) ────
    std::string key_queue;            // rate-limited to 4 keys/frame to avoid 16-entry ADB overflow

    // ── Tracked registers (from write observer) ────────────────
    uint8_t reg_c021 = 0;            // $C021 MONOCOLOR — bit 7 set = mono mode (BUS_FLAG_MONO)

    // ── Dirty-page diffing for HW frame sends ──────────────────
    // Prev state is ONLY updated when a frame is actually sent (wr==1).
    // If a frame is skipped (busy), prev state is preserved so the next frame
    // re-detects and re-sends the changes. This prevents "ghost" artifacts
    // where the card misses data that we thought we sent.
    uint8_t prev_text1[1024] = {};
    uint8_t prev_hgr[8192] = {};
    uint8_t prev_hgr_aux[8192] = {};
    uint8_t *prev_shr = nullptr;      // 32KB, heap-allocated on first SHR frame
    // Initialised to 0xFF, which is not a valid mode_flags combination.
    // This acts as a sentinel: on the very first frame, the comparison
    // (mode_flags != prev_mode_flags) is always true, guaranteeing that the
    // initial mode is sent to the card even when mode_flags starts at its
    // natural default of 0x08 (text mode).  Without this sentinel, a zero-
    // initialised prev_mode_flags could match a mode_flags value of 0x00 and
    // silently skip sending the initial frame.
    uint8_t prev_mode_flags = 0xFF;
    uint8_t prev_regs[4] = {};        // C029, C022, C034, C035 — mode change detection
    bool prev_valid = false;          // false until first successful send

    // Most fields use C++11 in-class default initialisers (= nullptr, = 0,
    // = false, = {}) declared above, so no assignments are needed here.
    // The constructor body only sets id, which cannot use an in-class
    // initialiser because DEVICE_ID_A2GSPU is defined after SlotData and
    // the member is inherited, not declared in this struct.
    a2gspu_data() {
        id = DEVICE_ID_A2GSPU;
    }
};

void init_slot_a2gspu(computer_t *computer, SlotType_t slot);
