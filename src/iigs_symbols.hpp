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
#include <unordered_map>
#include <unordered_set>

struct IigsSym { uint32_t addr; std::string name; };
inline std::vector<IigsSym> g_iigs_syms;
inline bool g_iigs_syms_loaded = false;
inline uint32_t g_iigs_sym_base = 0;        // runtime load base (subtracted at resolve)
inline bool     g_iigs_sym_base_locked = false;  // true = A2GSPU_SYM_BASE pinned it (no auto)

// SYMBOL-TRUTH (A2GSPU_SYM_SUSPECT=1): names that resolve at >1 DISTINCT address are
// "reused local labels" — the resolver's nearest-preceding pick can silently land on
// the wrong proc (this class produced 3 false roots in the owned-GS/OS loader debug:
// cont6, Setup_Buf_Ptrs, Do_Path_Segment). When armed, iigs_sym_resolve appends a
// bracketed [SUSPECT reused] so EVERY consumer (ITRACE/WATCH/TBTRACE/RETGUARD/BRK) is
// warned the label may be aliased -> verify by PC/disasm. Off => output byte-identical.
inline std::unordered_set<std::string> g_iigs_name_multi;   // names at >1 distinct addr
inline bool g_iigs_sym_suspect = false;                     // A2GSPU_SYM_SUSPECT

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
    // SYMBOL-TRUTH: flag names that appear at >1 distinct address (reused local labels).
    g_iigs_name_multi.clear();
    std::unordered_map<std::string, uint32_t> firstaddr;
    for (const auto &s : g_iigs_syms) {
        auto it = firstaddr.find(s.name);
        if (it == firstaddr.end()) firstaddr[s.name] = s.addr;
        else if (it->second != s.addr) g_iigs_name_multi.insert(s.name);
    }
    printf("IIGS SYM: loaded %zu code symbols from '%s' (base $%06X); %zu reused-local names flagged\n",
           g_iigs_syms.size(), path, g_iigs_sym_base, g_iigs_name_multi.size());
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
    // SYMBOL-TRUTH: append a bracketed warning when the picked name is a reused local
    // label (present at >1 address) — the nearest-preceding pick may be the wrong proc.
    if (g_iigs_sym_suspect && g_iigs_name_multi.count(g_iigs_syms[best].name)) {
        size_t len = strlen(buf);
        if (len < n) snprintf(buf + len, n - len, " [SUSPECT reused]");
    }
    return buf;
}

// ============================================================================
// A2GSPU_ROM_SYMBOLS=<file> — name ROM-resident PCs (bank $Fx ROM + the firmware
// dispatch vectors) in traces instead of the bare "<ROM>"/"<LC-ROM>" region tag.
// Reuses the .final.dbg parser (same `symbol "NAME" type LABEL addr $XXXXXX`
// lines) — and ALSO accepts a plain "HEXADDR name" line — into a SEPARATE table
// (ROM is fixed, so no runtime relocation base is applied). When the env is set
// but the file is absent/empty, a small built-in seed of documented Apple IIgs
// firmware entry points is used so ROM PCs still get a name; the AUTHORITATIVE
// source is a real ROM .dbg (the DATA DEPENDENCY this notes). Off by default
// (env unset => table empty => iigs_rom_sym_resolve returns "" => no change).
// ============================================================================
inline std::vector<IigsSym> g_iigs_rom_syms;
inline bool g_iigs_rom_syms_loaded = false;

// Documented IIgs firmware / dispatch entry points (the seed used when no ROM
// .dbg is supplied). Bank-$Fx ROM INTERIOR routines need a real ROM .dbg; these
// cover the well-known vectors the boot passes through.
inline void iigs_rom_symbols_seed() {
    static const struct { uint32_t a; const char *n; } seed[] = {
        { 0xE10000, "ToolDisp"      },   // Tool Locator dispatch
        { 0xE10004, "UserToolDisp"  },   // user Tool Locator dispatch
        { 0xE100A8, "GSOS_ProDOS16" },   // GS/OS class-1 (ProDOS 16) dispatch
        { 0xE100B0, "GSOS_Disp"     },   // GS/OS class-0 dispatch
        { 0xFF69,   "MonitorEntry"  },   // ROM Monitor entry (bank $FF)
    };
    for (auto &s : seed) g_iigs_rom_syms.push_back({ s.a & 0xFFFFFF, std::string(s.n) });
}

inline void iigs_rom_symbols_load(const char *path) {
    FILE *f = path ? fopen(path, "rb") : nullptr;
    if (f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            const char *s = strstr(line, "symbol \"");
            const char *t = strstr(line, " type ");
            const char *a = strstr(line, "addr $");
            if (!s || !t || !a) {                       // accept a plain "HEXADDR name" seed line
                unsigned aa; char nm[128];
                if (sscanf(line, "%x %127s", &aa, nm) == 2)
                    g_iigs_rom_syms.push_back({ aa & 0xFFFFFF, std::string(nm) });
                continue;
            }
            if (!strstr(t, "LABEL") && !strstr(t, "ENTRY")) continue;
            const char *nm = s + 8; const char *eq = strchr(nm, '"');
            if (!eq) continue;
            uint32_t addr = (uint32_t)strtoul(a + 6, nullptr, 16) & 0xFFFFFF;
            g_iigs_rom_syms.push_back({ addr, std::string(nm, (size_t)(eq - nm)) });
        }
        fclose(f);
    }
    if (g_iigs_rom_syms.empty()) {
        iigs_rom_symbols_seed();
        printf("IIGS ROMSYM: '%s' absent/empty -> seeded %zu built-in ROM entry points "
               "(authoritative source = a ROM .dbg)\n",
               path ? path : "(none)", g_iigs_rom_syms.size());
    }
    std::sort(g_iigs_rom_syms.begin(), g_iigs_rom_syms.end(),
              [](const IigsSym &a, const IigsSym &b) { return a.addr < b.addr; });
    g_iigs_rom_syms_loaded = !g_iigs_rom_syms.empty();
    printf("IIGS ROMSYM: %zu ROM symbols active\n", g_iigs_rom_syms.size());
}

// Resolve a ROM-resident PC to NAME+off (fixed ROM, no relocation base). "" when
// no table is loaded or no symbol is within range.
inline const char *iigs_rom_sym_resolve(uint32_t full_pc, char *buf, size_t n) {
    buf[0] = 0;
    if (!g_iigs_rom_syms_loaded) return buf;
    uint32_t pc = full_pc & 0xFFFFFF;
    int lo = 0, hi = (int)g_iigs_rom_syms.size() - 1, best = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (g_iigs_rom_syms[m].addr <= pc) { best = m; lo = m + 1; } else hi = m - 1;
    }
    if (best < 0) return buf;
    uint32_t off = pc - g_iigs_rom_syms[best].addr;
    if (off > 0x2000) return buf;              // too far from any ROM symbol
    if (off == 0) snprintf(buf, n, "%s", g_iigs_rom_syms[best].name.c_str());
    else snprintf(buf, n, "%s+$%X", g_iigs_rom_syms[best].name.c_str(), off);
    return buf;
}
