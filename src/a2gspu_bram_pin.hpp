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
//   unset, automated run  read ./gs2-bram.bin in the working directory if present, write
//                         it back at exit (A2GSPU_SPIKE_FRAMES or A2GSPU_CTRL: see below)
//   unset, windowed run   upstream's per-machine file, <prefs>/bram/<machine-uuid>.bin
//   A2GSPU_BRAM=default   the built-in initial contents; no file is read or written
//   A2GSPU_BRAM=<file>    load that 256-byte file; missing or short is fatal (exit 2);
//                         never written back -- an input is not an output
//
// Whatever the source, the RTC records the 256 bytes the machine STARTED with,
// and `A2GSPU PINS:` carries them as bram=<kind>/<house-FNV>, where kind is
// cwd | cwd-absent | prefs | prefs-absent | default | file, or bram=none when the
// machine has no RTC.
//
// AN AUTOMATED RUN KEEPS ITS BRAM IN ITS WORKING DIRECTORY. Upstream moved BRAM into
// the user's preferences folder, one file per machine UUID. For a person at the
// window that is the point -- settings follow the machine. For a harness it made
// every unpinned run on the workstation share one file: the first run wrote it and
// every later run started warm, whatever directory it ran in. Rosetta v3's pinned
// Finder boot, which gets cold BRAM from a fresh directory per run, hashed
// 2E304BF0147B220A once and 9774CB5F4633060A (its documented warm-BRAM hash) after.
// So a run the preferences gate already calls automated (gs2.cpp: A2GSPU_SPIKE_FRAMES
// or A2GSPU_CTRL) uses ./gs2-bram.bin, as it did before the port.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "house_fnv.hpp"
#include "a2gspu_run_kind.hpp"   // a2gspu_automated_run(): where an unpinned BRAM lives

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
