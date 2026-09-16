#pragma once
// ============================================================================
// a2gspu_progress_taps.hpp -- what the machine did, counted where it did it.
//
// Two consumers. The spike's run hash (GSDIAG run=, M9) is taken over insns and
// stores. The liveness sample (a2gspu_liveness.hpp) hashes the screen and the text page
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

inline bool     g_progress_taps = false;        // set by a headless spike (run hash) and by A2GSPU_LIVENESS
inline bool     g_progress_taps_iigs = false;   // which surfaces the ranges describe
inline uint64_t (*g_progress_now)() = nullptr;  // the machine clock, bound at arm
inline uint64_t g_progress_insns = 0;
inline uint64_t g_progress_stores = 0;         // CPU stores through bus_write (RMW phantom writes excluded)
inline uint64_t g_progress_screen_store = 0, g_progress_text_store = 0, g_progress_kbd_poll = 0;
inline bool     g_progress_screen_seen = false, g_progress_text_seen = false, g_progress_kbd_seen = false;
inline uint64_t g_progress_kbd_reads = 0;       // every keyboard read, both families

inline void a2gspu_progress_note_store(uint32_t addr) {
    const uint32_t bank = (addr >> 16) & 0xFF, a = addr & 0xFFFF;
    bool screen, text;
    if (g_progress_taps_iigs) {
        screen = (bank == 0xE1 && a >= 0x2000 && a <= 0x9FFF);
        text   = (bank == 0xE0 && a >= 0x0400 && a <= 0x07FF);
    } else {
        screen = (bank == 0x00 && a >= 0x2000 && a <= 0x3FFF);
        text   = (bank == 0x00 && a >= 0x0400 && a <= 0x07FF);
    }
    if (screen)    { g_progress_screen_store = g_progress_now(); g_progress_screen_seen = true; }
    else if (text) { g_progress_text_store   = g_progress_now(); g_progress_text_seen   = true; }
}

inline void a2gspu_progress_note_kbd() {
    g_progress_kbd_poll = g_progress_now();
    g_progress_kbd_seen = true;
    g_progress_kbd_reads++;
}
