#pragma once
// ============================================================================
// a2gspu_observe.hpp -- the one observer read (Docs/INSTRUMENTATION_SHAPE.md M11).
//
// RULE: an observe rail reads through the probe, never through the CPU's read
// path. The CPU's path has side effects: a $C0xx read throws a soft switch, a
// $Cn00 read on the II family selects a slot's $C800 ROM, and MMU_IIgs::read
// sets the cycle type for the byte it returns (fast ROM at $FC0000 and up), so
// a rail reading through it can change the machine it claims only to watch.
// Measured before this header existed: a 300-frame IIgs boot with a TAP session
// armed ended 754 cycles short of the same boot unarmed (10,634,635 against
// 10,635,389), with an identical SHR hash -- a disturbance only a cycle count
// could see.
//
// So every rail that reads guest memory to REPORT it calls a2obs_peek:
//   * IIgs: MMU_IIgs::probe_peek. It resolves banks $00/$01 through the same
//     aux and language-card mapping the CPU uses, answers $C100-$CFFF from the
//     Mega II page table and $E0/$E1 from the Mega II image, and returns the
//     floating-bus placeholder only for $C0xx, where no observation-free answer
//     exists (probe_readable says so).
//   * II family: the base probe is read_raw, which does not resolve the
//     language card or ROM banking -- switching CALLTRACE to it once produced a
//     trace that emitted nothing on -p 3 -- so outside $C000-$CFFF the II family
//     keeps the MMU's read, which on these machines has no side effect there;
//     $C000-$CFFF goes to the probe, never to a handler.
//
// Platform is decided once per MMU, from the MMU itself: only a IIgs MMU can
// read the Mega II image at $E1 without a handler (the liveness rail's test).
// ============================================================================
#include <cstdint>
#include "mmus/mmu.hpp"

inline MMU  *g_a2obs_mmu     = nullptr;
inline bool  g_a2obs_is_iigs = false;

inline bool a2obs_is_iigs(MMU *m) {
    if (m != g_a2obs_mmu) {
        g_a2obs_mmu = m;
        g_a2obs_is_iigs = m && m->probe_readable(0xE12000);
    }
    return g_a2obs_is_iigs;
}

// Side-effect-free byte at a 24-bit (IIgs) or 16-bit (II family) address.
inline uint8_t a2obs_peek(MMU *m, uint32_t addr) {
    if (a2obs_is_iigs(m)) return m->probe_peek(addr & 0xFFFFFF);
    // FORK (F13): probe_peek everywhere. The II family's only handler-backed read pages are
    // $C1-$CF (card ROM and card RAM windows), which were already probed; every other page
    // has a read pointer, so probe_peek returns the same byte without going through the
    // bus read, whose taps and handlers an observer must never reach.
    return m->probe_peek((uint16_t)(addr & 0xFFFF));
}

// Whether a2obs_peek returned memory or a placeholder it could not avoid.
inline bool a2obs_readable(MMU *m, uint32_t addr) {
    if (a2obs_is_iigs(m)) return m->probe_readable(addr & 0xFFFFFF);
    return m->probe_readable((uint16_t)(addr & 0xFFFF));
}

// Display soft-switch state for rails that report it, read from the video
// scanner (what the `vid` verb reads) instead of by reading $C01x through the
// CPU path. gs2.cpp installs it at arm; bit 0 TEXT, 1 MIXED, 2 PAGE2, 3 HIRES.
// Returns -1 when no scanner is available, so a rail can say so.
inline int (*g_a2obs_video_flags)() = nullptr;
