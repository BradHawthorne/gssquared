#pragma once
// ============================================================================
// iigs_symbols.hpp — load a linkiigs .final.dbg symbol table and resolve a
// 24-bit PC to NAME+offset, for legible toolbox traces / BRK dumps.
//
// Parses the `.debug_symbols` section lines:
//     symbol "NAME" type LABEL addr $00XXXX file N line N [export]
// Only LABEL/ENTRY symbols are kept (EQU constants are not code addresses).
// The .dbg addresses are link-relative; a GS/OS-loaded app is relocated to its
// runtime bank, so A2GSPU_SYM_BASE=<hex> adds a load-base offset. Env-gated by
// the caller (A2GSPU_SYMBOLS=<path>). Header-only, generic IIgs naming.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <algorithm>

struct IigsSym { uint32_t addr; std::string name; };
inline std::vector<IigsSym> g_iigs_syms;
inline bool g_iigs_syms_loaded = false;
inline uint32_t g_iigs_sym_base = 0;        // runtime load base (subtracted at resolve)
inline bool     g_iigs_sym_base_locked = false;  // true = A2GSPU_SYM_BASE pinned it (no auto)

inline void iigs_symbols_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { printf("IIGS SYM: cannot open '%s'\n", path); return; }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        const char *s = strstr(line, "symbol \"");
        const char *t = strstr(line, " type ");
        const char *a = strstr(line, "addr $");
        if (!s || !t || !a) continue;
        // keep only code symbols
        if (!strstr(t, "LABEL") && !strstr(t, "ENTRY")) continue;
        const char *nm = s + 8;
        const char *eq = strchr(nm, '"');
        if (!eq) continue;
        // Store the RAW link-relative .dbg address; the runtime load base is
        // applied at RESOLVE time so it can be set/auto-inferred after load.
        uint32_t addr = (uint32_t)strtoul(a + 6, nullptr, 16) & 0xFFFFFF;
        g_iigs_syms.push_back({addr, std::string(nm, (size_t)(eq - nm))});
    }
    fclose(f);
    std::sort(g_iigs_syms.begin(), g_iigs_syms.end(),
              [](const IigsSym &a, const IigsSym &b) { return a.addr < b.addr; });
    g_iigs_syms_loaded = !g_iigs_syms.empty();
    printf("IIGS SYM: loaded %zu code symbols from '%s' (base $%06X)\n",
           g_iigs_syms.size(), path, g_iigs_sym_base);
}

// Resolve full_pc -> "NAME+$off" into buf; "" if no table / no nearby symbol.
// Subtracts the runtime load base so a relocated GS/OS app's PC maps back to its
// link-relative .dbg address.
inline const char *iigs_sym_resolve(uint32_t full_pc, char *buf, size_t n) {
    buf[0] = 0;
    if (!g_iigs_syms_loaded) return buf;
    uint32_t pc = full_pc - g_iigs_sym_base;
    int lo = 0, hi = (int)g_iigs_syms.size() - 1, best = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (g_iigs_syms[m].addr <= pc) { best = m; lo = m + 1; } else hi = m - 1;
    }
    if (best < 0) return buf;
    uint32_t off = pc - g_iigs_syms[best].addr;
    if (off > 0x4000) return buf;          // too far from any symbol -> unknown
    if (off == 0) snprintf(buf, n, "%s", g_iigs_syms[best].name.c_str());
    else snprintf(buf, n, "%s+$%X", g_iigs_syms[best].name.c_str(), off);
    return buf;
}
