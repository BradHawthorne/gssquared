#pragma once
// ============================================================================
// a2gspu_bram_pin.hpp -- A2GSPU_BRAM: which battery RAM a run starts from.
//
// WHY: the stock RTC reads ./gs2-bram.bin from the WORKING DIRECTORY when it
// exists and writes it back at exit. A run's starting BRAM therefore depends
// on the directory it ran in and on every run that ran there before it, and
// no pin printed it. Measured: one 300-frame IIgs boot with seed, faketime and
// ramdisk pinned hashed 5F78F7C15131F43F from a fresh directory and
// 89FCD3DA7D947DFF from the repo root, whose BRAM file an earlier run had
// rewritten -- so the "unarmed hash" earlier commits quoted as a dual-mode
// check was never reproducible. A determinism pin that leaves an input
// unpinned and self-modifying is not a pin.
//
//   unset                 stock: read ./gs2-bram.bin if present, write it back at exit
//   A2GSPU_BRAM=default   the built-in initial contents; no file is read or written
//   A2GSPU_BRAM=<file>    load that 256-byte file; missing or short is fatal (exit 2);
//                         never written back -- an input is not an output
//
// Whatever the source, the RTC records the 256 bytes the machine STARTED with,
// and `A2GSPU PINS:` carries them as bram=<kind>/<house-FNV>, where kind is
// cwd | cwd-absent | default | file, or bram=none when the machine has no RTC.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "house_fnv.hpp"

inline bool        g_bram_pin_probed = false;
inline const char *g_bram_pin        = nullptr;   // env value; nullptr when unset or empty
inline const char *g_bram_kind       = "none";    // what the most recently built RTC started from
inline uint64_t    g_bram_hash       = 0;

inline const char *a2gspu_bram_pin() {
    if (!g_bram_pin_probed) {
        g_bram_pin_probed = true;
        const char *v = getenv("A2GSPU_BRAM");
        g_bram_pin = (v && *v) ? v : nullptr;
    }
    return g_bram_pin;
}

inline void a2gspu_bram_note(const char *kind, const uint8_t *bram) {
    uint64_t h = HOUSE_FNV_BASIS;
    for (int i = 0; i < 256; i++) { h ^= bram[i]; h *= HOUSE_FNV_PRIME; }
    g_bram_kind = kind;
    g_bram_hash = h;
}

// The PINS field value: "<kind>/<hash>" or "none". No path -- a path may hold
// spaces and PINS is a space-separated line; the caller knows the path it set.
inline void a2gspu_bram_field(char *buf, size_t n) {
    if (!strcmp(g_bram_kind, "none")) snprintf(buf, n, "none");
    else snprintf(buf, n, "%s/%016llX", g_bram_kind, (unsigned long long)g_bram_hash);
}
