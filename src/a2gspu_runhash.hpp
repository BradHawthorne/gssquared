#pragma once
// ============================================================================
// a2gspu_runhash.hpp -- M9's combined run hash, and the gate that demands it.
//
// GSDIAG's hash= is the screen and cycles= is the clock. Two runs can agree on
// both and still have done different work: a screen that settled early hides
// everything after it, and a cycle count is the same for any program that
// spends the same time. The run hash folds in what the CPU actually did --
// instructions executed and stores made -- so a harness that differences two
// runs can refuse to subtract when the work differs, not only when the picture
// or the clock does.
//
//   run = house-FNV-1a-64 over four little-endian 64-bit values, in order:
//         insns, stores, hash (the GSDIAG screen hash), cycles
//
// insns and stores come from the progress taps (a2gspu_progress_taps.hpp),
// which a headless run arms before its first frame; stores are CPU stores
// through the store funnel, RMW phantom writes excluded. GSDIAG prints all
// three: insns= stores= run=. A2GSPU_EXPECT_RUN=<hex> is the inline gate, the
// run-hash twin of A2GSPU_EXPECT_HASH.
//
// Observation only: the taps count, they never touch emulated state, and the
// unarmed-hash gate proves a run with them reproduces the unarmed hash and
// cycle count.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include "a2gspu_progress_taps.hpp"
#include "a2record.hpp"      // cycles_now: the one clock records and taps share
#include "house_fnv.hpp"

inline uint64_t a2gspu_run_hash(uint64_t screen_hash, uint64_t cycles) {
    uint64_t h = HOUSE_FNV_BASIS;
    const uint64_t v[4] = { g_progress_insns, g_progress_stores, screen_hash, cycles };
    for (uint64_t x : v)
        for (int i = 0; i < 8; i++) { h ^= (x >> (8 * i)) & 0xFF; h *= HOUSE_FNV_PRIME; }
    return h;
}

// Arm the counters for a headless run. Idempotent; A2GSPU_LIVENESS arms the
// same taps and binds the same clock.
inline void a2gspu_progress_taps_arm(bool is_iigs) {
    g_progress_taps_iigs = is_iigs;
    if (!g_progress_now) g_progress_now = [] { return (uint64_t)a2record::cycles_now(); };
    g_progress_taps = true;
}

// A2GSPU_EXPECT_RUN: returns true when the gate was asked (and sets *fail on a
// mismatch or an unparseable value -- a gate that cannot read its own argument
// has compared nothing, which is not a pass).
inline bool a2gspu_expect_run_gate(uint64_t screen_hash, uint64_t cycles, bool *fail) {
    const char *e = getenv("A2GSPU_EXPECT_RUN");
    if (!e) return false;
    char *end = nullptr;
    const uint64_t want = strtoull(e, &end, 16);
    const uint64_t cur = a2gspu_run_hash(screen_hash, cycles);
    if (end == e || *end != '\0') {
        printf("A2GSPU EXPECT_RUN: UNPARSEABLE '%s' (cur=%016llX) -- not a hex run hash; gate fails\n",
               e, (unsigned long long)cur);
        *fail = true;
        return true;
    }
    const bool match = g_progress_taps && want == cur;
    printf("A2GSPU EXPECT_RUN: %s (cur=%016llX want=%016llX insns=%llu stores=%llu)%s\n",
           match ? "MATCH" : "MISMATCH", (unsigned long long)cur, (unsigned long long)want,
           (unsigned long long)g_progress_insns, (unsigned long long)g_progress_stores,
           g_progress_taps ? "" : " -- progress taps were not armed; no run hash exists");
    if (!match) *fail = true;
    return true;
}
