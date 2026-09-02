#pragma once
// ============================================================================
// a2gspu_pins.hpp -- the identity of a headless run, printed before it starts.
//
// WHY: a measurement is only as good as the sentence "this binary, these
// inputs, these pins, produced this number". A downstream measurement report
// keeps that sentence by hand and records, as its own open item, that the
// build behind each figure is not pinned. The differencing technique it uses
// (two cold runs, the longer's counters minus the shorter's) rests on the
// runs sharing a prefix, which is only true if their pins are equal -- and
// nothing printed the pins.
//
// So every spike prints one line first:
//
//   A2GSPU PINS: seed=1 faketime=1780000000 ramdisk=1 platform=5 frames=300
//                rom=131072/<fnv> build=0.8.1726+883dc68(configure-time) exe=<fnv>
//
// seed / faketime / ramdisk are the determinism pins as the rails read them
// (unset is printed as unset, not defaulted). rom= is the size and house-FNV
// of the ROM image actually loaded. build= is the version header's string,
// which CMake fills in at CONFIGURE time and which therefore lags the tree --
// it is labelled so. exe= is the house-FNV of the running executable, which
// cannot lag: it is the binary that produced the run, whatever the header
// says. When the executable cannot be read back (argv[0] without a path) the
// field says unreadable rather than printing a hash of nothing.
//
// The companion is GSDIAG's cycles= field: with the SHR hash it is the
// run's determinism witness. Two runs with equal PINS lines must agree on
// both; A2GSPU_EXPECT_HASH=<hex> makes the second half a gate.
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "house_fnv.hpp"

inline const char    *g_a2gspu_argv0    = nullptr;
inline const uint8_t *g_a2gspu_rom_data = nullptr;
inline size_t         g_a2gspu_rom_size = 0;

inline uint64_t a2gspu_fnv_bytes(const uint8_t *p, size_t n) {
    uint64_t h = HOUSE_FNV_BASIS;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= HOUSE_FNV_PRIME; }
    return h;
}

// House-FNV of a file, or false if it cannot be opened.
inline bool a2gspu_fnv_file(const char *path, uint64_t &out) {
    if (!path || !*path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint64_t h = HOUSE_FNV_BASIS;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0)
        for (size_t i = 0; i < n; i++) { h ^= buf[i]; h *= HOUSE_FNV_PRIME; }
    fclose(f);
    out = h;
    return true;
}

inline void a2gspu_pins_print(int platform_id, int frames, const char *build_version) {
    const char *seed = getenv("A2GSPU_SEED");
    const char *ft   = getenv("A2GSPU_FAKETIME");
    const bool  ram  = getenv("A2GSPU_RAMDISK") != nullptr;
    char rom[48], exe[40];
    if (g_a2gspu_rom_data && g_a2gspu_rom_size)
        snprintf(rom, sizeof rom, "%zu/%016llX", g_a2gspu_rom_size,
                 (unsigned long long)a2gspu_fnv_bytes(g_a2gspu_rom_data, g_a2gspu_rom_size));
    else
        snprintf(rom, sizeof rom, "none");
    uint64_t eh = 0;
    if (a2gspu_fnv_file(g_a2gspu_argv0, eh)) snprintf(exe, sizeof exe, "%016llX", (unsigned long long)eh);
    else snprintf(exe, sizeof exe, "unreadable");
    printf("A2GSPU PINS: seed=%s faketime=%s ramdisk=%d platform=%d frames=%d rom=%s build=%s(configure-time) exe=%s\n",
           seed ? seed : "unset", ft ? ft : "unset", ram ? 1 : 0, platform_id, frames, rom,
           build_version ? build_version : "unknown", exe);
    fflush(stdout);
}
