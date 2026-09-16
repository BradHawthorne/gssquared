#pragma once
// ============================================================================
// a2gspu_liveness_taps.hpp -- what A2GSPU_LIVENESS needs from the machine.
//
// The liveness sample (a2gspu_liveness.hpp) hashes the screen and the text page
// every N frames. A hash that stops changing says nothing moved on that surface;
// it cannot say why. These taps add the facts that separate the cases:
//
//   insns            instructions executed since arm: a CPU that stopped (STP, a
//                    held reset, a stopped clock) versus one still running
//   screen_store /   the cycle of the last CPU store that landed on a hashed
//   text_store       surface: a program redrawing identical bytes versus one that
//                    stopped drawing at all
//   kbd_poll         the cycle of the last keyboard read: a wait-for-key loop
//                    versus one that no longer asks
//
// A store counts where the machine placed it. On a IIgs that is a direct store
// to $E1:2000-9FFF / $E0:0400-07FF, or a bank $00/$01 store the MMU shadowed into
// them (tapped at the MMU's own shadow decision, so a store it did not mirror is
// not counted). On the II family it is the CPU address in bank $00, the same
// ranges the sample hashes ($2000-3FFF, $0400-07FF).
//
// Kept apart from the rail so the files that carry the taps (the CPU store
// funnel, the IIgs shadow write, the two keyboard devices) include a few
// globals, not the rail and its dependencies. Unarmed, each tap is one untaken
// branch; armed, it never touches emulated state.
// ============================================================================
#include <cstdint>

inline bool     g_liveness_taps = false;        // set when A2GSPU_LIVENESS arms
inline bool     g_liveness_taps_iigs = false;   // which surfaces the ranges describe
inline uint64_t (*g_liveness_now)() = nullptr;  // the machine clock, bound at arm
inline uint64_t g_liveness_insns = 0;
inline uint64_t g_liveness_screen_store = 0, g_liveness_text_store = 0, g_liveness_kbd_poll = 0;
inline bool     g_liveness_screen_seen = false, g_liveness_text_seen = false, g_liveness_kbd_seen = false;
inline uint64_t g_liveness_kbd_reads = 0;       // every keyboard read, both families

inline void a2gspu_liveness_note_store(uint32_t addr) {
    const uint32_t bank = (addr >> 16) & 0xFF, a = addr & 0xFFFF;
    bool screen, text;
    if (g_liveness_taps_iigs) {
        screen = (bank == 0xE1 && a >= 0x2000 && a <= 0x9FFF);
        text   = (bank == 0xE0 && a >= 0x0400 && a <= 0x07FF);
    } else {
        screen = (bank == 0x00 && a >= 0x2000 && a <= 0x3FFF);
        text   = (bank == 0x00 && a >= 0x0400 && a <= 0x07FF);
    }
    if (screen)    { g_liveness_screen_store = g_liveness_now(); g_liveness_screen_seen = true; }
    else if (text) { g_liveness_text_store   = g_liveness_now(); g_liveness_text_seen   = true; }
}

inline void a2gspu_liveness_note_kbd() {
    g_liveness_kbd_poll = g_liveness_now();
    g_liveness_kbd_seen = true;
    g_liveness_kbd_reads++;
}
