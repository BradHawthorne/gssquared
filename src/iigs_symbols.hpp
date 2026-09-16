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
#include "a2record.hpp"   // frame_now(): the live-table walk is asked at most once per frame
#include <unordered_set>
#include "cpu.hpp"
#include "mmus/mmu.hpp"
#include "a2gspu_pins.hpp"     // g_a2gspu_rom_data / size: the ROM the run actually loaded

// iigs_diag.hpp defines it, and iigs_diag.hpp includes iigs_toolbox.hpp which
// includes this header, so the definition arrives after us. Declared here the
// same way memvu_storevis.hpp declares a2gspu_name_pc: a rail that cannot warn
// can only printf, and a printf never reaches GSDIAG warnings= or A2GSPU_STRICT.
inline void a2gspu_warn(const char *rail, const char *what);

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

// What the load actually consumed, for the readback below and for the summary
// line: a table that yielded nothing must be able to say so out loud.
inline std::string g_iigs_sym_path;
inline int g_iigs_sym_n_dbg = 0;      // lines read as the .final.dbg form
inline int g_iigs_sym_n_plain = 0;    // lines read as plain "HEXADDR name"

// Which form the file turned out to be in. "none" is the answer that used to be
// unavailable: the summary said "loaded 0 code symbols" and left the reader to
// guess whether the file was empty, the wrong format, or full of EQU constants.
inline const char *iigs_symbols_format_word() {
    if (g_iigs_sym_n_dbg && g_iigs_sym_n_plain) return "mixed";
    if (g_iigs_sym_n_dbg)   return "dbg";
    if (g_iigs_sym_n_plain) return "plain";
    return "none";
}

inline void iigs_symbols_load(const char *path) {
    g_iigs_sym_path = path ? path : "";
    g_iigs_sym_n_dbg = g_iigs_sym_n_plain = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        // A WARNING, NOT A PRINTF. This printed "IIGS SYM: cannot open X" and
        // the run still ended "GSDIAG: status=OK ... warnings=0", exit 0 --
        // even under A2GSPU_STRICT=1, which exists precisely to refuse a run
        // that could not do what it was asked. Every sym field in every ITRACE
        // / WATCH / TRAP / BRK record downstream is null because of this, so
        // the harness has to be able to see it.
        char m[640];
        snprintf(m, sizeof m, "cannot open '%s' -- every sym field will be null", path ? path : "(null)");
        a2gspu_warn("IIGS SYM", m);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        const char *s = strstr(line, "symbol \"");
        const char *t = strstr(line, " type ");
        const char *a = strstr(line, "addr $");
        if (!s || !t || !a) {
            // ALSO ACCEPT THE PLAIN "HEXADDR name" FORM, exactly as
            // iigs_rom_symbols_load below already does and as the shipped
            // assets/symbols/apple2gs/<ROM-FNV>.sym is written. This parser
            // demanded all three .final.dbg tokens on every line and `continue`d
            // otherwise, so a perfectly good plain table loaded 0 symbols and
            // said "loaded 0 code symbols" as though that were an outcome.
            // Two parsers for two forms of the same thing, in the same header,
            // and only one of them was reachable from A2GSPU_SYMBOLS.
            unsigned aa; char nm[128];
            if (line[0] != '#' && sscanf(line, "%x %127s", &aa, nm) == 2) {
                g_iigs_syms.push_back({ aa & 0xFFFFFF, std::string(nm) });
                g_iigs_sym_n_plain++;
            }
            continue;
        }
        // keep only code symbols
        if (!strstr(t, "LABEL") && !strstr(t, "ENTRY")) continue;
        const char *nm = s + 8;
        const char *eq = strchr(nm, '"');
        if (!eq) continue;
        // Store the RAW link-relative .dbg address; the runtime load base is
        // applied at RESOLVE time so it can be set/auto-inferred after load.
        uint32_t addr = (uint32_t)strtoul(a + 6, nullptr, 16) & 0xFFFFFF;
        g_iigs_syms.push_back({addr, std::string(nm, (size_t)(eq - nm))});
        g_iigs_sym_n_dbg++;
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
    // A table that yielded nothing is not a loaded table. Same reason as the
    // fopen failure above: silence here reads downstream as "this program has
    // no symbols", which is a different and wrong statement.
    if (g_iigs_syms.empty()) {
        char m[700];
        snprintf(m, sizeof m, "'%s' yielded 0 symbols (neither the .final.dbg "
                 "'symbol \"NAME\" type LABEL addr $XXXXXX' form nor plain "
                 "'HEXADDR name') -- every sym field will be null", path);
        a2gspu_warn("IIGS SYM", m);
    }
    printf("IIGS SYM: loaded %zu code symbols from '%s' (format=%s dbg=%d plain=%d, base $%06X); %zu reused-local names flagged\n",
           g_iigs_syms.size(), path, iigs_symbols_format_word(),
           g_iigs_sym_n_dbg, g_iigs_sym_n_plain,
           g_iigs_sym_base, g_iigs_name_multi.size());
}

// ---- readback (M8): `rail read A2GSPU_SYMBOLS` -----------------------------
// The rail had no reader at all, so the only way to learn that a symbol table
// loaded nothing was to read the stdout line at arm time -- which a CTRL
// session that armed the rail through the environment never sees.
inline void iigs_symbols_format_status(char *buf, size_t n) {
    snprintf(buf, n, "status=OK A2GSPU_SYMBOLS=%s format=%s count=%zu base=$%06X",
             g_iigs_sym_path.empty() ? "(unset)" : g_iigs_sym_path.c_str(),
             iigs_symbols_format_word(), g_iigs_syms.size(),
             (unsigned)g_iigs_sym_base);
}
inline a2rails::Registrar g_reader_symbols{"A2GSPU_SYMBOLS", iigs_symbols_format_status};

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
// tools/a2rail/romfpt.py, shipped as assets/symbols/apple2gs/<house-FNV of
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
    // Installed beside the ROMs (build/resources/symbols/, through the same base
    // path ResourceFile uses), else the source tree for a run from the fork root
    // before the build assembled resources. The gate runs the emulator from its
    // own directory, where a cwd-relative path found nothing and every toolbox
    // PC went unnamed without a word about why.
    const unsigned long long h = a2gspu_fnv_bytes(g_a2gspu_rom_data, g_a2gspu_rom_size);
    char path[640], src[128];
    snprintf(path, sizeof path, "%ssymbols/apple2gs/%016llX.sym", g_a2gspu_base_path.c_str(), h);
    snprintf(src, sizeof src, "assets/symbols/apple2gs/%016llX.sym", h);
    FILE *f = fopen(path, "rb");
    if (!f) { f = fopen(src, "rb"); if (f) snprintf(path, sizeof path, "%s", src); }
    if (!f) { printf("IIGS SYMS: no toolbox table for this ROM (%s and %s absent); toolbox PCs stay unnamed\n", path, src); return; }
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

// ---- RAM-resident tool sets, from the LIVE tool pointer table ---------------
// The ROM table above names ROM entry points. Under GS/OS that is the minority:
// System 6 on ROM 01 moves the system TPT into RAM ($7E/F5ED on a pinned
// GS/OS 6.0.1 desktop; the address depends on the boot), patches most ROM tool
// sets (their FPTs are in RAM, entries pointing into RAM for the patched
// functions and back into ROM for the rest), and loads the RAM-only sets
// (Window, Menu, Control, Dialog, ...) wherever the Memory Manager puts them on
// this boot: 24 of 35 sets have RAM function tables on that desktop. No file
// can hold those addresses; the machine has to be asked. So the namer walks the
// live TPT -- pointer at $E1/03C0, a count, one long per set to its FPT, a
// count, one long per function, each the function's address minus one for
// the dispatcher's RTL -- through probe_peek (no side effects), keeps
// the RAM entry points in a second sorted table, and walks again only when a
// RAM address misses and the TPT has moved or grown since the last walk (an
// application loading a tool set does exactly that). Names come from
// toolbox.names, written by tools/a2rail/romfpt.py --names; a function not
// listed is fnXX and a set not listed is ToolNN, both true statements.
inline std::vector<IigsSym> g_iigs_live_syms;
inline uint32_t g_iigs_live_tpt = 0, g_iigs_live_count = 0;
inline int      g_iigs_live_checked_frame = -2;

// The walk happens at the first RAM lookup, which can be in the middle of a
// report, so it never prints; it leaves one line here, and the line goes out at
// the next boundary: the frame tick, the start of the STOREVIS report, or exit.
inline std::string g_iigs_syms_note;
inline void iigs_syms_flush_note() {
    if (g_iigs_syms_note.empty()) return;
    fputs(g_iigs_syms_note.c_str(), stdout);
    g_iigs_syms_note.clear();
}
inline std::unordered_map<uint32_t, std::string> g_iigs_toolnames;   // (tool << 8) | func; func 0 = the set's name
inline bool     g_iigs_toolnames_tried = false;

inline void iigs_toolnames_load_once() {
    if (g_iigs_toolnames_tried) return;
    g_iigs_toolnames_tried = true;
    char path[640];
    snprintf(path, sizeof path, "%ssymbols/apple2gs/toolbox.names", g_a2gspu_base_path.c_str());
    FILE *f = fopen(path, "rb");
    if (!f) f = fopen("assets/symbols/apple2gs/toolbox.names", "rb");
    if (!f) { printf("IIGS SYMS: toolbox.names absent; RAM tool sets are named ToolNN.fnXX\n"); return; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned t, fn; char nm[128];
        if (line[0] == '#') continue;
        if (sscanf(line, "%u %u %127s", &t, &fn, nm) == 3 && t < 256 && fn < 256) g_iigs_toolnames[(t << 8) | fn] = nm;
    }
    fclose(f);
}

inline std::string iigs_tool_func_name(unsigned tool, unsigned fn) {
    static const char *housekeeping[7] = { "", "BootInit", "StartUp", "ShutDown", "Version", "Reset", "Status" };
    iigs_toolnames_load_once();   // whoever asks first: the live walk, a call context, a report
    std::string set = "Tool" + std::to_string(tool);
    auto it = g_iigs_toolnames.find(tool << 8);
    if (it != g_iigs_toolnames.end()) set = it->second;
    std::string func;
    auto jt = g_iigs_toolnames.find((tool << 8) | fn);
    if (jt != g_iigs_toolnames.end()) func = jt->second;
    else if (fn >= 1 && fn <= 6) func = housekeeping[fn];
    else { char b[16]; snprintf(b, sizeof b, "fn%02X", fn); func = b; }
    return set + "." + func;
}

// Walk the live TPT if it has moved or grown since the last walk. Returns true
// when the live table changed. Costs a few thousand peeks when it walks and two
// when it does not; it is asked at most once per frame.
inline bool iigs_live_toolsets_refresh() {
    cpu_state *cpu = g_a2gspu_cpu_for_names;
    if (!cpu || !cpu->mmu) return false;
    const int frame = a2record::frame_now();
    if (g_iigs_live_checked_frame == frame) return false;
    g_iigs_live_checked_frame = frame;
    auto peek32 = [&](uint32_t a) {
        uint32_t v = 0;
        for (int i = 0; i < 4; i++) v |= (uint32_t)cpu->mmu->probe_peek((a + i) & 0xFFFFFF) << (8 * i);
        return v & 0xFFFFFF;
    };
    const uint32_t tpt = peek32(0xE103C0);
    if (tpt == 0 || tpt >= 0xFE0000) return false;            // no table yet, or the ROM's own: the static table covers it
    const uint32_t count = peek32(tpt);
    if (count == 0 || count > 64) return false;
    if (tpt == g_iigs_live_tpt && count == g_iigs_live_count) return false;
    iigs_toolnames_load_once();
    std::vector<IigsSym> live;
    int sets = 0;
    for (uint32_t t = 1; t <= count; t++) {
        const uint32_t fpt = peek32(tpt + 4 * t);
        if (fpt == 0 || fpt >= 0xFE0000) continue;            // absent, or in ROM: the static table names it
        const uint32_t fc = peek32(fpt);
        if (fc == 0 || fc > 300) continue;
        sets++;
        for (uint32_t fn = 1; fn <= fc; fn++) {
            const uint32_t e = peek32(fpt + 4 * fn);
            if (e == 0 || e >= 0xFE0000) continue;            // a ROM entry the patch left alone
            // An entry is the function's address MINUS ONE: the dispatcher pushes
            // it and executes RTL, which lands one byte later.
            live.push_back({ (e + 1) & 0xFFFFFF, iigs_tool_func_name(t, fn) });
        }
    }
    std::sort(live.begin(), live.end(), [](const IigsSym &a, const IigsSym &b) { return a.addr < b.addr; });
    live.erase(std::unique(live.begin(), live.end(), [](const IigsSym &a, const IigsSym &b) { return a.addr == b.addr; }), live.end());
    g_iigs_live_syms.swap(live);
    g_iigs_live_tpt = tpt; g_iigs_live_count = count;
    char note[200];
    snprintf(note, sizeof note, "IIGS SYMS: live tool pointer table at $%02X/%04X: %u tool sets, %d with RAM function tables, %zu RAM entry points named\n",
             (tpt >> 16) & 0xFF, tpt & 0xFFFF, count, sets, g_iigs_live_syms.size());
    g_iigs_syms_note += note;
    static bool at_exit_registered = false;
    if (!at_exit_registered) { at_exit_registered = true; atexit(iigs_syms_flush_note); }
    return true;
}

// The live table's answer for a RAM address, or empty. Same window as the ROM
// table: an entry names the function it begins, not the next 8 KB.
inline const char *iigs_live_name_pc(uint32_t pc, char *buf, size_t n) {
    buf[0] = 0;
    if (g_iigs_live_syms.empty()) return buf;
    int lo = 0, hi = (int)g_iigs_live_syms.size() - 1, best = -1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (g_iigs_live_syms[m].addr <= pc) { best = m; lo = m + 1; } else hi = m - 1;
    }
    if (best < 0) return buf;
    const uint32_t off = pc - g_iigs_live_syms[best].addr;
    if (off > 0x800) return buf;
    if (off == 0) snprintf(buf, n, "%s", g_iigs_live_syms[best].name.c_str());
    else snprintf(buf, n, "%s+$%X", g_iigs_live_syms[best].name.c_str(), off);
    return buf;
}

// The default table, ready to search: the documented entry points seeded, the
// ROM's own toolbox table loaded, and the whole thing SORTED -- in that order,
// once, whichever caller arrives first (main() at arm, so the loader's one
// line prints whole; or the first lookup, on a path that never armed). The
// first version keyed this on the loader's own flag, so an arm-time load left
// the vector unseeded and unsorted for every lookup after it: a binary search
// over an unsorted table named the Tool Locator dispatch null.
inline void iigs_names_ready() {
    if (g_iigs_default_seeded) return;
    if (g_iigs_rom_syms.empty()) iigs_rom_symbols_seed();
    iigs_toolbox_symbols_load_once();
    std::sort(g_iigs_rom_syms.begin(), g_iigs_rom_syms.end(),
              [](const IigsSym &a, const IigsSym &b) { return a.addr < b.addr; });
    g_iigs_default_seeded = true;
}

#include "iigs_gsos_names.hpp"   // GSOS.<segment>+off, verified against the live page

inline const char *a2gspu_name_pc(uint32_t full_pc, char *buf, size_t n) {
    buf[0] = 0;
    if (g_iigs_syms_loaded && iigs_sym_resolve(full_pc, buf, n)[0]) return buf;
    iigs_names_ready();
    // RAM first, from the live tool pointer table: a patched ROM function or a
    // RAM-only tool set lives here, and no static table can know where.
    if ((full_pc & 0xFFFFFF) < 0xFE0000) {
        if (iigs_live_name_pc(full_pc & 0xFFFFFF, buf, n)[0]) return buf;
        if (iigs_live_toolsets_refresh() && iigs_live_name_pc(full_pc & 0xFFFFFF, buf, n)[0]) return buf;
        // Then GS/OS's own kernel (the System Loader's entry points already named
        // above, as tool set 17, win over its segment name).
        if (iigs_gsos_name_pc(full_pc & 0xFFFFFF, buf, n)[0]) return buf;
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
