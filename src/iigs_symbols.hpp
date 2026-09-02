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
#include "cpu.hpp"
#include "mmus/mmu.hpp"
#include "a2gspu_pins.hpp"     // g_a2gspu_rom_data / size: the ROM the run actually loaded

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
    // IIgs dispatch vectors (bank $E1 RAM, fixed by the firmware) and ProDOS 8's
    // MLI entry (bank 0 global page).
    static const struct { uint32_t a; const char *n; } seed[] = {
        { 0xE10000, "ToolDisp"      },   // Tool Locator dispatch
        { 0xE10004, "UserToolDisp"  },   // user Tool Locator dispatch
        { 0xE100A8, "GSOS_ProDOS16" },   // GS/OS class-1 (ProDOS 16) dispatch
        { 0xE100B0, "GSOS_Disp"     },   // GS/OS class-0 dispatch
        { 0x00BF00, "MLI"           },   // ProDOS 8 Machine Language Interface
    };
    for (auto &s : seed) g_iigs_rom_syms.push_back({ s.a & 0xFFFFFF, std::string(s.n) });
    // The Apple II Monitor entry points documented since the Apple II Reference
    // Manual and kept by every later machine for compatibility. On the IIgs the
    // same code sits in bank $FF (the last 16 KB of the ROM image) and is seen
    // in bank $00 when the language card maps ROM, so both forms are named.
    // Documented entry points only: routine INTERIORS still need a ROM .dbg,
    // which is the data dependency that keeps a granule like $FF/4600 unnamed.
    static const struct { uint16_t a; const char *n; } mon[] = {
        { 0xF800, "PLOT" },     { 0xF819, "HLINE" },    { 0xF828, "VLINE" },   { 0xF832, "CLRSCR" },
        { 0xF836, "CLRTOP" },   { 0xF847, "GBASCALC" }, { 0xF864, "SETCOL" },  { 0xF871, "SCRN" },
        { 0xF941, "PRNTAX" },   { 0xF948, "PRBLNK" },   { 0xF94A, "PRBL2" },   { 0xFA62, "RESET" },
        { 0xFB1E, "PREAD" },
        { 0xFB2F, "INIT" },     { 0xFB39, "SETTXT" },   { 0xFB40, "SETGR" },   { 0xFB4B, "SETWND" },
        { 0xFBDD, "BELL1" },    { 0xFC22, "VTAB" },     { 0xFC24, "VTABZ" },   { 0xFC42, "CLREOP" },
        { 0xFC58, "HOME" },     { 0xFC62, "CR" },       { 0xFC66, "LF" },      { 0xFC70, "SCROLL" },
        { 0xFC9C, "CLREOL" },   { 0xFCA8, "WAIT" },     { 0xFD0C, "RDKEY" },   { 0xFD1B, "KEYIN" },
        { 0xFD35, "RDCHAR" },   { 0xFD67, "GETLNZ" },   { 0xFD6A, "GETLN" },   { 0xFD6F, "GETLN1" },
        { 0xFD8E, "CROUT" },    { 0xFDDA, "PRBYTE" },   { 0xFDE3, "PRHEX" },   { 0xFDED, "COUT" },
        { 0xFDF0, "COUT1" },    { 0xFE2C, "MOVE" },     { 0xFE80, "SETINV" },  { 0xFE84, "SETNORM" },
        { 0xFE89, "SETKBD" },   { 0xFE93, "SETVID" },   { 0xFF2D, "PRERR" },   { 0xFF3A, "BELL" },
        { 0xFF3F, "RESTORE" },  { 0xFF4A, "SAVE" },     { 0xFF58, "IORTS" },   { 0xFF59, "OLDRST" },
        { 0xFF65, "MON" },      { 0xFF69, "MONZ" },
    };
    for (auto &m : mon) {
        g_iigs_rom_syms.push_back({ (uint32_t)m.a,            std::string(m.n) });
        g_iigs_rom_syms.push_back({ 0xFF0000u | (uint32_t)m.a, std::string(m.n) });
    }
}

// ---- names by default -------------------------------------------------------
// The ROM seed above is loaded by A2GSPU_ROM_SYMBOLS; without that env the old
// consumers (ITRACE/TBTRACE region tags) print "<ROM>" exactly as before, so no
// parser moves. NEW attribution outputs -- the STOREVIS writers line, TRAP
// records -- go through this instead: the application table when it is loaded,
// else the documented entry points, seeded on first use. "" means unnamed, and
// every consumer prints that as unnamed/null rather than as a bare address.
inline bool g_iigs_default_seeded = false;

// The ROM's toolbox function entry points, read out of the ROM by
// tools/a2rail/romfpt.py and shipped as assets/symbols/apple2gs/<house-FNV of
// the ROM>.sym, so a different ROM gets no wrong names. Loaded on first use
// beside the Monitor seed. A PC between two entries is named after the
// preceding one: right for a function body, wrong for a shared helper (the
// boot's DOC clear is one, inside SoundBootInit but after GetTableAddress's
// entry); a dispatcher trap is the authority when it matters.
inline bool g_iigs_toolbox_tried = false;
inline int  g_iigs_toolbox_loaded = 0;
inline void iigs_toolbox_symbols_load_once() {
    if (g_iigs_toolbox_tried) return;
    g_iigs_toolbox_tried = true;
    if (!g_a2gspu_rom_data || !g_a2gspu_rom_size) return;
    char path[128];
    snprintf(path, sizeof path, "assets/symbols/apple2gs/%016llX.sym",
             (unsigned long long)a2gspu_fnv_bytes(g_a2gspu_rom_data, g_a2gspu_rom_size));
    FILE *f = fopen(path, "rb");
    if (!f) { printf("IIGS SYMS: no toolbox table for this ROM (%s absent); toolbox PCs stay unnamed\n", path); return; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned a; char nm[128];
        if (line[0] == '#') continue;
        if (sscanf(line, "%x %127s", &a, nm) == 2) { g_iigs_rom_syms.push_back({ a & 0xFFFFFF, std::string(nm) }); g_iigs_toolbox_loaded++; }
    }
    fclose(f);
    printf("IIGS SYMS: %d toolbox entry points from %s\n", g_iigs_toolbox_loaded, path);
}

// The bank-0 form of a Monitor entry point is only a name when the ROM is
// mapped there. Under GS/OS bank 0 $D000-$FFFF is language-card RAM holding
// the kernel, and naming $00/F800 "PLOT" during a GS/OS boot is a false name
// -- worse than none (the first version of this did exactly that). So a bank-0
// address at or above $D000 is checked against the ROM the run actually
// loaded: the $C000-$FFFF window is the image's last 16 KB, and if the four
// bytes the CPU sees at the PC are not the ROM's bytes, the name is withheld.
// The check reads through the MMU probe (side-effect-free; it resolves the
// language-card state the way the CPU sees it); gs2.cpp sets the cpu at arm
// time. Without it, or on a ROM too small to have that window, no bank-0 high
// name is claimed.
inline cpu_state *g_a2gspu_cpu_for_names = nullptr;
inline bool a2gspu_bank0_rom_mapped(uint32_t lo) {
    if (!g_a2gspu_cpu_for_names || !g_a2gspu_rom_data || g_a2gspu_rom_size < 0x4000 || lo < 0xD000) return false;
    const uint8_t *win = g_a2gspu_rom_data + g_a2gspu_rom_size - 0x4000;
    for (uint32_t k = 0; k < 4; k++) {
        const uint32_t a = lo + k;
        if (a > 0xFFFF) break;
        if (g_a2gspu_cpu_for_names->mmu->probe_peek(a) != win[a - 0xC000]) return false;
    }
    return true;
}

inline const char *a2gspu_name_pc(uint32_t full_pc, char *buf, size_t n) {
    buf[0] = 0;
    if (g_iigs_syms_loaded && iigs_sym_resolve(full_pc, buf, n)[0]) return buf;
    if (g_iigs_rom_syms.empty() || !g_iigs_toolbox_tried) {
        if (g_iigs_rom_syms.empty()) iigs_rom_symbols_seed();
        iigs_toolbox_symbols_load_once();
        std::sort(g_iigs_rom_syms.begin(), g_iigs_rom_syms.end(),
                  [](const IigsSym &a, const IigsSym &b) { return a.addr < b.addr; });
        g_iigs_default_seeded = true;
    }
    // Same search as iigs_rom_sym_resolve, without the loaded-flag gate, and with
    // a tighter window: an entry point names the routine it begins, not the
    // next 8 KB of ROM.
    const uint32_t pc = full_pc & 0xFFFFFF;
    int lo = 0, hi = (int)g_iigs_rom_syms.size() - 1, best = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (g_iigs_rom_syms[m].addr <= pc) { best = m; lo = m + 1; } else hi = m - 1;
    }
    if (best < 0) return buf;
    const uint32_t sa = g_iigs_rom_syms[best].addr;
    if ((sa >> 16) == 0 && (sa & 0xFFFF) >= 0xD000 && !a2gspu_bank0_rom_mapped(pc & 0xFFFF)) return buf;
    const uint32_t off = pc - sa;
    // Monitor entry points are dense and short; toolbox functions are not. A
    // longer reach names a function body while a shared helper past its end
    // still resolves to the preceding entry, which the table header warns of.
    if (off > 0x800) return buf;
    if (off == 0) snprintf(buf, n, "%s", g_iigs_rom_syms[best].name.c_str());
    else snprintf(buf, n, "%s+$%X", g_iigs_rom_syms[best].name.c_str(), off);
    return buf;
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
