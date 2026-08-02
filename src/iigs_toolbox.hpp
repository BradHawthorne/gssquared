#pragma once
// ============================================================================
// iigs_toolbox.hpp — headless Apple IIgs Tool Locator / GS-OS call trace +
// closed-loop hooks (result retention, trace scoping, stop-on-fault, headless
// breakpoint, loader-error surfacing, symbolization).
//
// Traces calls through the Tool Locator dispatch ($E10000) and the GS/OS
// dispatchers ($E100A8 / $E100B0): logs call->name on entry, carry + A (the
// error code) on return. Retains the last result per call so an assertion gate
// can check e.g. QDStartUp carry==0. All env-gated, stdout, canonical IIgs
// naming. Hooked from the CPU core's execute_next (one cheap branch when off;
// if constexpr-compiled out of the 6502/65C02 cores).
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>
#include <utility>
#include "cpu.hpp"
#include "iigs_symbols.hpp"
#include "debugger/trace_opcodes.hpp"

inline bool g_iigs_tbtrace_enabled = false;   // toolbox call/return trace
inline bool g_iigs_errhook_enabled = false;   // also trace GS/OS dispatch returns
inline bool g_iigs_brkdump_enabled = false;   // dump CPU state on BRK (crash)
inline bool g_iigs_stop_on_fault   = false;   // halt the spike on BRK/COP/SysFail
inline bool g_iigs_break_enabled   = false;   // headless breakpoint
inline uint32_t g_iigs_break_addr  = 0;       // 24-bit full-PC breakpoint
inline int  g_iigs_tbtrace_bank    = -1;      // only trace calls from this bank (-1 = all)
inline uint32_t g_iigs_trace_from  = 0;       // start tracing when PC first hits this (0 = always)
inline bool g_iigs_trace_armed     = false;   // trace_from has fired
inline int  g_iigs_brk_count       = 0;       // BRK/crash count (exit-taxonomy canary)
inline bool g_brkmem_on            = false;   // A2GSPU_BRKMEM: crash-path mem/PC ring dump
inline uint32_t g_pchist[256]      = {0};     // A1: ring of last PCs (ALL banks) into the fault
inline int  g_pchist_i             = 0;

// ---- A2GSPU_PCTRAP: one-shot execution trap on first entry to a PC range ----
// Env A2GSPU_PCTRAP="bank:lo-hi" (hex). When execution FIRST fetches an instruction
// whose full-PC is in the range, dump the register file + the recent PC ring (the
// CALLER that jumped in) exactly once. Catches a wild jump into uninitialized memory
// BEFORE the long garbage run scrolls the true caller out of the ring.
inline bool     g_pctrap_active = false;
inline uint32_t g_pctrap_lo     = 0;
inline uint32_t g_pctrap_hi     = 0;
inline bool     g_pctrap_fired  = false;
// Optional extra memory region dumped by PCTRAP/STACKTRAP (full 24-bit base, N bytes).
inline uint32_t g_trap_dump_base = 0;
inline uint32_t g_trap_dump_len  = 0;

// ---- A2GSPU_STACKTRAP: one-shot trap when the stack pointer S enters a range --
// Env A2GSPU_STACKTRAP="lo-hi" (hex, 16-bit S). Fires once the instant S first
// lands in [lo,hi], dumping the PC/regs + the PC ring. Catches a stray `tcs` or a
// corrupted RTL/RTS that parks the stack over data/code (e.g. S=$74AA sitting on
// the heartbeat taskheader at $74A3, whose pushes then clobber the task sig).
inline bool     g_stacktrap_active = false;
inline uint16_t g_stacktrap_lo     = 0;
inline uint16_t g_stacktrap_hi     = 0;
inline bool     g_stacktrap_fired  = false;

// ---- A2GSPU_STACKWATCH: CONTINUOUS stack-pointer tripwire ------------------
// Where STACKTRAP (above) is a ONE-SHOT trap when S first ENTERS a fixed range,
// this is a per-instruction WINDOW monitor for the garbage-S crash class (a wild
// `tcs`/`txs`, or executing garbage that parks S off its valid region — the
// S=$2355 / $01D9 sightings). Two orthogonal signals, both capped + symbolized:
//   (a) IMBALANCE — a single-instruction stack discontinuity (|dS| > threshold).
//       Normal push/pull/JSR/JSL/RTS/RTL/interrupt move S by <=4; only a stack
//       RELOCATION (txs/tcs) or a corrupted pull moves it far. The boot's few
//       legitimate native-mode stack setups fire this a handful of times (each
//       annotated + capped) — anything larger is the bug.
//   (b) OVER/UNDERFLOW — S left the declared valid window [lo,hi], edge-triggered
//       so one excursion is one event (not a flood). Zero-false-positive: the
//       caller declares the window.
// Env: A2GSPU_STACKWATCH=1 (imbalance only) or ="lo-hi" (hex; imbalance + window).
//      A2GSPU_STACKWATCH_JUMP=<hex> overrides the imbalance threshold (default $40).
// Off by default => one untaken branch. stderr (never touches the golden stdout).
inline bool     g_stackwatch_on      = false;
inline bool     g_stackwatch_has_win = false;
inline uint16_t g_stackwatch_lo      = 0;
inline uint16_t g_stackwatch_hi      = 0;
inline int      g_stackwatch_jump    = 0x40;
inline int      g_stackwatch_hits    = 0;
inline int      g_stackwatch_last_s  = -1;
inline bool     g_stackwatch_was_out = false;
// The stack pointer is stored 16-bit for the 65816, and 6502/65C02 code does not
// keep the high byte consistent: TXS writes $01FF while a push or an RTS leaves
// $00xx. Both describe the same physical byte in page 1, but comparing the raw
// value against a window makes the over/underflow check nonsense on a IIe --
// measured, watching a2engine: `TXS` reported S=$01FF and the very next `RTS`
// reported S=$00FA, and the window fired on a stack that had not moved a page.
//
// wiz5_config.hpp already uses the right idiom for 6502 stack addresses. Use it
// here too when the high byte is not a plausible 65816 native stack, so the
// tripwire measures the address the machine actually pushes to.
inline uint16_t iigs_stack_addr(const cpu_state *cpu) {
    uint16_t s = (uint16_t)cpu->sp;
    return (s < 0x0100) ? (uint16_t)(0x0100 | (s & 0xFF)) : s;
}

inline void iigs_stackwatch_check(cpu_state *cpu) {
    if (g_stackwatch_hits >= 64) return;
    uint16_t s = iigs_stack_addr(cpu);
    if (g_stackwatch_last_s >= 0) {                          // (a) imbalance
        int ds = (int)s - g_stackwatch_last_s;
        int ads = ds < 0 ? -ds : ds;
        if (ads > g_stackwatch_jump) {
            g_stackwatch_hits++;
            char sym[80]; iigs_sym_resolve(cpu->full_pc, sym, sizeof(sym));
            fprintf(stderr, "IIGS STACKWATCH: S $%04X->$%04X (d=%+d) at %02X/%04X%s%s "
                            "(imbalance / stack relocation)\n",
                    (unsigned)g_stackwatch_last_s, (unsigned)s, ds,
                    (unsigned)(cpu->full_pc >> 16), (unsigned)(cpu->full_pc & 0xFFFF),
                    sym[0] ? " " : "", sym);
        }
    }
    if (g_stackwatch_has_win) {                              // (b) over/underflow (edge)
        bool out = (s < g_stackwatch_lo || s > g_stackwatch_hi);
        if (out && !g_stackwatch_was_out) {
            g_stackwatch_hits++;
            char sym[80]; iigs_sym_resolve(cpu->full_pc, sym, sizeof(sym));
            fprintf(stderr, "IIGS STACKWATCH: S=$%04X OUTSIDE window [$%04X,$%04X] at "
                            "%02X/%04X%s%s (over/underflow)\n",
                    (unsigned)s, (unsigned)g_stackwatch_lo, (unsigned)g_stackwatch_hi,
                    (unsigned)(cpu->full_pc >> 16), (unsigned)(cpu->full_pc & 0xFFFF),
                    sym[0] ? " " : "", sym);
        }
        g_stackwatch_was_out = out;
    }
    g_stackwatch_last_s = s;
}

// ---- A2GSPU_WATCH: address-range access-watchpoint (v2) --------------------
// Env A2GSPU_WATCH="bank:lo-hi[,bank:lo-hi...]" (hex) traps CPU accesses into a
// range and prints the faulting PC + value, so a wrong-bank / stray store that
// corrupts code or data is caught AT the instruction doing it. One cheap branch
// on the write funnel when off. Batch-2 v2 extends the batch-1 capped write-watch:
//   A2GSPU_WATCH_MAX=<n>   hit cap (default 256; 0 = unlimited)
//   A2GSPU_WATCH_OUT=<f>   NDJSON to a file instead of stdout lines
//   A2GSPU_WATCH_CHANGE=1  log a write only when the value differs from last
//   A2GSPU_WATCH_READ=1    ALSO watch READS of the ranges (the bus_read funnel)
// With no v2 env the behaviour is byte-for-byte the batch-1 write-watch (stdout,
// 256-hit cap, "wrote ... ->" line). NDJSON carries CPU-state fields only (no
// host/interpreter residue).
struct WatchRange { uint32_t lo, hi; };       // inclusive full 24-bit addresses
inline bool       g_watch_on          = false;
inline WatchRange g_watch_ranges[8]   = {};
inline int        g_watch_count       = 0;
inline int        g_watch_hits        = 0;
inline FILE      *g_watch_out         = nullptr;
inline int        g_watch_max         = 256;
inline bool       g_watch_change_only = false;
inline bool       g_watch_read_on     = false;
inline int        g_watch_last[8]     = { -1, -1, -1, -1, -1, -1, -1, -1 };

// probe_peek of banks $00/$01 can return floating-bus $EE for handler/relocated pages
// that read_raw cannot see (see mmu.hpp). A sensor that trusts such a read lied ~6x in
// the owned-GS/OS boot campaign. This flags a SUSPECT reading so a diagnostic surfaces
// it ("suspect a liar") instead of silently trusting a floating-bus byte as memory
// truth. Observation-only; RETGUARD's validated stack-context probe_peek is unaffected.
inline bool iigs_probe_suspect(uint32_t addr, uint8_t val) {
    return (((addr >> 16) & 0xFF) <= 0x01) && (val == 0xEE);
}

extern int g_iigs_cur_frame;   // fwd (defined below with ITRACE): early decl so the WATCH `ts` field below can read the frame clock
inline void iigs_watch_emit(cpu_state *cpu, uint32_t addr, uint8_t data, const char *kind) {
    if (g_watch_max && g_watch_hits >= g_watch_max) {
        if (g_watch_hits == g_watch_max) {
            if (g_watch_out) fprintf(g_watch_out, "{\"event\":\"suppressed\"}\n");
            else printf("IIGS WATCH: (further hits suppressed)\n");
            g_watch_hits++;
        }
        return;
    }
    g_watch_hits++;
    // Harvested (wiz5): two ADDITIVE NDJSON fields, emitted only in the
    // A2GSPU_WATCH_OUT NDJSON sink (the console/text line is byte-unchanged):
    //   ts  = the current headless/CTRL frame counter -> joins a read/write to
    //         the frame-stamped display timeline (fully generic).
    //   ipc = a GENERIC interpreter instruction-pointer field: 0 unless
    //         A2GSPU_WATCH_IPC_ZP=<hexZP> configures a 2-byte little-endian ZP
    //         pointer to read (observation-free probe_peek), which attributes a
    //         store to the interpreter's current segment. For UCSD p-code
    //         (e.g. Wizardry) set A2GSPU_WATCH_IPC_ZP=9E; default-off so the
    //         rail carries no title-specific assumption.
    static const int ipc_zp = [] {
        const char *e = getenv("A2GSPU_WATCH_IPC_ZP");
        return e ? (int)strtol(e, nullptr, 16) : 0;
    }();
    const unsigned w_ipc = (g_watch_out && ipc_zp)
        ? (unsigned)(cpu->mmu->probe_peek(ipc_zp) |
                     (cpu->mmu->probe_peek((ipc_zp + 1) & 0xFFFF) << 8)) : 0u;
    if (g_watch_out)
        fprintf(g_watch_out,
                "{\"event\":\"watch\",\"kind\":\"%s\",\"pc\":%u,\"addr\":%u,"
                "\"data\":%u,\"s\":%u,\"d\":%u,\"dbr\":%u,\"ipc\":%u,\"ts\":%u}\n",
                kind, (unsigned)(cpu->full_pc & 0xFFFFFF), (unsigned)addr,
                (unsigned)data, (unsigned)cpu->sp, (unsigned)cpu->d, (unsigned)cpu->db,
                w_ipc, (unsigned)g_iigs_cur_frame);
    else
        printf("IIGS WATCH: PC=%02X/%04X %s $%02X %s %02X/%04X  S=$%04X D=$%04X DBR=$%02X\n",
               (unsigned)((cpu->full_pc >> 16) & 0xFF), (unsigned)(cpu->full_pc & 0xFFFF),
               kind[0] == 'w' ? "wrote" : "read ", data,
               kind[0] == 'w' ? "->" : "<-",
               (unsigned)((addr >> 16) & 0xFF), (unsigned)(addr & 0xFFFF),
               (unsigned)cpu->sp, (unsigned)cpu->d, (unsigned)cpu->db);
}

inline void iigs_watch_check(cpu_state *cpu, uint32_t addr, uint8_t data) {
    for (int i = 0; i < g_watch_count; i++) {
        if (addr >= g_watch_ranges[i].lo && addr <= g_watch_ranges[i].hi) {
            if (g_watch_change_only && (int)data == g_watch_last[i]) return;
            g_watch_last[i] = data;
            iigs_watch_emit(cpu, addr, data, "write");
            return;
        }
    }
}
// A2GSPU_WATCH_READ=1: break-on-read on the bus_read funnel (same ranges).
inline void iigs_watch_check_read(cpu_state *cpu, uint32_t addr, uint8_t data) {
    for (int i = 0; i < g_watch_count; i++) {
        if (addr >= g_watch_ranges[i].lo && addr <= g_watch_ranges[i].hi) {
            iigs_watch_emit(cpu, addr, data, "read");
            return;
        }
    }
}

// ---- A2GSPU_VALTRAP: value-provenance store trap ----------------------------
// WATCH answers "who wrote to address X"; VALTRAP answers "who wrote the VALUE V".
// bus_write is byte-granular, so a multi-byte pointer (e.g. $E06014) arrives as
// consecutive ascending-address little-endian byte writes. We keep a tiny ring of
// recent writes and, treating each write as the value's TOP byte, test whether the
// `width` bytes at [addr-(width-1) .. addr] spell the target value LE. On a match we
// emit the writer PC + regs -- the instruction that stored the bogus pointer. This
// binds a base-lost/wrong-source address that dest-based WATCH attributes only to the
// ROM consumer. Env A2GSPU_VALTRAP="<hexval>[:<width>]" (width 1-4, default 3).
// Off => 1 branch; observation-only, deterministic (no wall-clock/rand).
inline bool     g_valtrap_on    = false;
inline uint32_t g_valtrap_val   = 0;
inline int      g_valtrap_width = 3;
inline int      g_valtrap_max   = 64;
inline int      g_valtrap_hits  = 0;
inline uint32_t g_valtrap_ra[8] = {};
inline uint8_t  g_valtrap_rd[8] = {};
inline int      g_valtrap_ri    = 0;

inline void iigs_valtrap_check(cpu_state *cpu, uint32_t addr, uint8_t data) {
    // record this byte-write in the ring
    g_valtrap_ra[g_valtrap_ri] = addr;
    g_valtrap_rd[g_valtrap_ri] = data;
    g_valtrap_ri = (g_valtrap_ri + 1) & 7;
    // quick-reject: only proceed when the current byte is the value's TOP byte.
    if (data != (uint8_t)((g_valtrap_val >> (8 * (g_valtrap_width - 1))) & 0xFF)) return;
    // assemble [addr-(w-1) .. addr] LE from the most-recent ring write to each address.
    uint32_t base = addr - (uint32_t)(g_valtrap_width - 1);
    uint32_t built = 0;
    for (int k = 0; k < g_valtrap_width; k++) {
        uint32_t a = base + (uint32_t)k;
        int found = -1;
        for (int s = 0; s < 8; s++) {
            int idx = (g_valtrap_ri - 1 - s) & 7;
            if (g_valtrap_ra[idx] == a) { found = idx; break; }
        }
        if (found < 0) return;
        built |= (uint32_t)g_valtrap_rd[found] << (8 * k);
    }
    uint32_t mask = (g_valtrap_width >= 4) ? 0xFFFFFFFFu : ((1u << (8 * g_valtrap_width)) - 1u);
    if ((built & mask) != (g_valtrap_val & mask)) return;
    if (g_valtrap_max && g_valtrap_hits >= g_valtrap_max) return;
    g_valtrap_hits++;
    printf("IIGS VALTRAP: value $%0*X -> %02X/%04X  by PC=%02X/%04X  S=$%04X D=$%04X DBR=$%02X  (hit %d)\n",
           g_valtrap_width * 2, (unsigned)(g_valtrap_val & mask),
           (unsigned)((addr >> 16) & 0xFF), (unsigned)(addr & 0xFFFF),
           (unsigned)((cpu->full_pc >> 16) & 0xFF), (unsigned)(cpu->full_pc & 0xFFFF),
           (unsigned)cpu->sp, (unsigned)cpu->d, (unsigned)cpu->db, g_valtrap_hits);
}

// ---- A2GSPU_CONDTRAP="bank:pc@f=v": conditional flag-provenance trap -----------
// Trap at PC only when a processor flag holds a value, and report WHO last changed
// that flag -- the instruction whose branch this PC's routing depends on (the P3
// value-level hop: "who set the carry that routed check_express_seg to old_loader").
// A per-instruction shadow records, for each P bit, the PC of the instruction that
// last flipped it (P is diffed across consecutive landings; the CHANGER is the
// PREVIOUS instruction). Env f in {c,z,i,d,x,m,v,n}; v in {0,1}. Off => cheap;
// observation-only (reads regs, no writes) => golden-neutral.
inline bool     g_condtrap_on   = false;
inline uint32_t g_condtrap_pc   = 0;      // 24-bit trap PC
inline uint8_t  g_condtrap_bit  = 0;      // P bit index 0..7
inline uint8_t  g_condtrap_want = 0;      // required bit value 0/1
inline int      g_condtrap_max  = 8;
inline int      g_condtrap_hits = 0;
inline uint8_t  g_ct_prev_p     = 0;
inline uint32_t g_ct_prev_pc    = 0xFFFFFFFFu;
inline uint32_t g_ct_setter[8]  = {};     // last PC that flipped each P bit

inline void iigs_condtrap_step(cpu_state *cpu) {
    uint8_t p = cpu->p;
    if (g_ct_prev_pc != 0xFFFFFFFFu) {
        uint8_t chg = p ^ g_ct_prev_p;
        if (chg) for (int b = 0; b < 8; b++) if (chg & (1u << b)) g_ct_setter[b] = g_ct_prev_pc;
    }
    g_ct_prev_p = p;
    g_ct_prev_pc = cpu->full_pc & 0xFFFFFF;
    if (g_condtrap_hits >= g_condtrap_max) return;
    if ((cpu->full_pc & 0xFFFFFF) != g_condtrap_pc) return;
    if (((p >> g_condtrap_bit) & 1) != g_condtrap_want) return;
    g_condtrap_hits++;
    uint32_t stp = g_ct_setter[g_condtrap_bit];
    static const char *fn = "czidxmvn";       // bit0=C 1=Z 2=I 3=D 4=X 5=M 6=V 7=N
    printf("IIGS CONDTRAP: PC=%02X/%04X flag %c=%d  last-set-by PC=%02X/%04X  "
           "A=%04X X=%04X Y=%04X S=$%04X D=$%04X DBR=$%02X  (hit %d)\n",
           (unsigned)((cpu->full_pc >> 16) & 0xFF), (unsigned)(cpu->full_pc & 0xFFFF),
           fn[g_condtrap_bit], g_condtrap_want,
           (unsigned)((stp >> 16) & 0xFF), (unsigned)(stp & 0xFFFF),
           (unsigned)cpu->a, (unsigned)cpu->x, (unsigned)cpu->y,
           (unsigned)cpu->sp, (unsigned)cpu->d, (unsigned)cpu->db, g_condtrap_hits);
}

// ---- A2GSPU_LOADTRACE="bank[:lo-hi]": runtime segment-overlay tracker ----------
// The System Loader OVERLAYS segments into a bank, so a fixed address holds DIFFERENT
// code at different times -- which defeats end-of-run DUMPs and is the ROOT of the
// loader's symbol aliasing (many routines map to one runtime address). LOADTRACE
// watches WRITES into the tracked region, coalesces contiguous ascending write-bursts
// (>= threshold = a segment being read/relocated into memory), and logs each load with
// its range, frame, and writer PC, flagging when it OVERLAYS a prior load -- a queryable
// "what code was loaded where, WHEN" timeline. A2GSPU_LOADTRACE_MIN=<n> = burst threshold
// (default 64). Off => 1 branch; observation-only (only inspects writes already on the
// bus, never dispatches one) => golden-byte-neutral.
extern int      g_iigs_cur_frame;       // fwd: headless spike frame counter (defined below with ITRACE)
inline bool     g_loadtrace_on = false;
inline uint32_t g_lt_lo = 0, g_lt_hi = 0;
inline uint32_t g_lt_min = 64;
inline uint32_t g_lt_cur_lo = 0, g_lt_cur_hi = 0, g_lt_cur_pc = 0;
inline int      g_lt_cur_frame = 0;
inline bool     g_lt_in_burst = false;
inline int      g_lt_loads = 0;
inline uint32_t g_lt_hist_lo[128] = {}, g_lt_hist_hi[128] = {};
inline int      g_lt_hist_n = 0;

inline void iigs_loadtrace_flush() {
    if (!g_lt_in_burst) return;
    g_lt_in_burst = false;
    if (g_lt_cur_hi - g_lt_cur_lo + 1 < g_lt_min) return;    // too small = data/stack, not a segment
    int ov = -1;
    int lim = (g_lt_hist_n < 128) ? 0 : g_lt_hist_n - 128;
    for (int i = g_lt_hist_n - 1; i >= lim; i--) {
        int idx = i & 127;
        if (g_lt_cur_lo <= g_lt_hist_hi[idx] && g_lt_cur_hi >= g_lt_hist_lo[idx]) { ov = idx; break; }
    }
    g_lt_loads++;
    printf("IIGS LOADTRACE: seg #%d [%02X/%04X-%04X] %u bytes @frame %d by PC=%02X/%04X%s\n",
           g_lt_loads, (unsigned)((g_lt_cur_lo >> 16) & 0xFF), (unsigned)(g_lt_cur_lo & 0xFFFF),
           (unsigned)(g_lt_cur_hi & 0xFFFF), (unsigned)(g_lt_cur_hi - g_lt_cur_lo + 1), g_lt_cur_frame,
           (unsigned)((g_lt_cur_pc >> 16) & 0xFF), (unsigned)(g_lt_cur_pc & 0xFFFF),
           ov >= 0 ? "" : "");
    if (ov >= 0)
        printf("                OVERLAYS prior seg [%02X/%04X-%04X]\n",
               (unsigned)((g_lt_hist_lo[ov] >> 16) & 0xFF), (unsigned)(g_lt_hist_lo[ov] & 0xFFFF),
               (unsigned)(g_lt_hist_hi[ov] & 0xFFFF));
    g_lt_hist_lo[g_lt_hist_n & 127] = g_lt_cur_lo;
    g_lt_hist_hi[g_lt_hist_n & 127] = g_lt_cur_hi;
    g_lt_hist_n++;
}

inline void iigs_loadtrace_write(cpu_state *cpu, uint32_t addr, uint8_t /*data*/) {
    if (addr < g_lt_lo || addr > g_lt_hi) return;
    if (g_lt_in_burst && addr + 4 >= g_lt_cur_lo && addr <= g_lt_cur_hi + 4) {
        if (addr < g_lt_cur_lo) g_lt_cur_lo = addr;
        if (addr > g_lt_cur_hi) g_lt_cur_hi = addr;
        return;
    }
    iigs_loadtrace_flush();                 // discontiguous -> close prior, open new
    g_lt_in_burst = true;
    g_lt_cur_lo = g_lt_cur_hi = addr;
    g_lt_cur_pc = cpu->full_pc & 0xFFFFFF;
    g_lt_cur_frame = g_iigs_cur_frame;
}

// ---- A2GSPU_LCTRACE: Language-Card softswitch ($C080-$C08F, any bank) access log ----
// LC read-state is READ-triggered (`lda $C081` -> read-ROM), so WATCH (write-only) misses
// it. This logs every LC-switch access with the PC + decoded read-RAM/ROM + $D000 bank, to
// find who leaves a bank's LC in read-ROM when its LC-RAM code must run. Off => 1 branch.
inline bool g_lctrace_on   = false;
inline int  g_lctrace_hits = 0;
inline void iigs_lc_trace(cpu_state *cpu, uint32_t addr, bool is_write) {
    if ((addr & 0xFFF0) != 0xC080) return;          // not an LC softswitch
    int x = addr & 0x0F;
    bool read_ram = ((x & 1) == ((x >> 1) & 1));     // low nibble 00/11 -> read RAM; 01/10 -> read ROM
    int dbank = (x & 8) ? 1 : 2;                     // A3: $D000 bank 1 vs 2
    if (g_lctrace_hits < 400) {
        char sym[64]; iigs_sym_resolve(cpu->full_pc, sym, sizeof(sym));
        printf("IIGS LC: %s $%06X -> bank$%02X %s (D000-b%d) from PC=%02X/%04X%s%s\n",
               is_write ? "wr" : "rd", addr, (unsigned)((addr >> 16) & 0xFF),
               read_ram ? "read-RAM" : "read-ROM", dbank,
               (unsigned)((cpu->full_pc >> 16) & 0xFF), (unsigned)(cpu->full_pc & 0xFFFF),
               sym[0] ? " " : "", sym);
    } else if (g_lctrace_hits == 400) printf("IIGS LC: (further hits suppressed)\n");
    g_lctrace_hits++;
}

// ---- kernel-vs-ROM region classifier (ends the "symbol lies in bank-0" confound) ----
// Bank-0 $D000-$FFFF is the Language-Card region: it holds the GS/OS LC-RAM kernel OR the
// IIgs ROM Monitor depending on LC read-state, and $C000-$CFFF is I/O + slot/expansion ROM,
// and banks $F0-$FF are ROM — yet iigs_sym_resolve() blindly maps ANY bank-0 PC to the
// nearest kernel symbol, so a trace of ROM/slot firmware reads as kernel (every confound
// this session). This classifies a *live* PC definitively by comparing the byte the CPU
// actually FETCHES (probe_peek) to the ROM image: a match in the LC/ROM region => executing
// ROM, not kernel. Returns "" for a genuine kernel-RAM PC. Lazy-loads the ROM (A2GSPU_ROM).
inline uint8_t *g_iigs_rom = nullptr; inline long g_iigs_rom_len = 0; inline bool g_iigs_rom_tried = false;
inline void iigs_rom_load_once() {
    if (g_iigs_rom_tried) return; g_iigs_rom_tried = true;
    const char *p = SDL_getenv("A2GSPU_ROM");
    const char *path = p ? p : "resources/roms/apple2gs/main.rom";   // relative to the run dir; A2GSPU_ROM overrides
    FILE *f = fopen(path, "rb"); if (!f) return;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0 && n <= 0x40000) { g_iigs_rom = (uint8_t *)malloc((size_t)n); if (g_iigs_rom) g_iigs_rom_len = (long)fread(g_iigs_rom, 1, (size_t)n, f); }
    fclose(f);
}
inline const char *iigs_pc_region(cpu_state *cpu, uint32_t pc) {
    uint8_t bank = (pc >> 16) & 0xFF; uint16_t lo = pc & 0xFFFF;
    if (bank >= 0xF0) return "ROM";
    if ((bank==0||bank==1||bank==0xE0||bank==0xE1) && lo>=0xC000 && lo<0xD000) return "IO/slot";
    // During GS/OS BOOT the only valid code banks are $00/$01 (kernel/loader) and $E0/$E1
    // (Mega-II). Execution elsewhere = a wild jump into uninitialized RAM (e.g. bank $B0 read
    // as a sea of $B0=BCS). (Would need loosening once apps load code into higher banks, but
    // the boot never gets there.)
    if (!(bank==0||bank==1||bank==0xE0||bank==0xE1)) return "wild-RAM";
    if ((bank==0||bank==1) && lo>=0xD000) {                 // LC region: RAM kernel or ROM Monitor?
        iigs_rom_load_once();
        if (g_iigs_rom && g_iigs_rom_len >= 0x20000) {
            uint8_t e0 = cpu->mmu->probe_peek(pc);
            uint8_t e1 = cpu->mmu->probe_peek((pc & 0xFF0000) | ((lo + 1) & 0xFFFF));
            if (e0 == g_iigs_rom[0x10000 + lo] && e1 == g_iigs_rom[0x10000 + ((lo + 1) & 0xFFFF)])
                return "LC-ROM";                            // CPU is fetching ROM Monitor, not the kernel
        }
    }
    return "";
}

// Region tag, ROM-symbolized (A2GSPU_ROM_SYMBOLS). Turns a bare "<ROM>"/"<LC-ROM>"
// into "<ROM ToolDisp+$12>" when the ROM symbol table named the PC; only ROM/LC-ROM
// regions consult the ROM table. No table loaded => unchanged (the default no-op).
inline void iigs_region_tag(uint32_t pc, const char *rgn, char *buf, size_t n) {
    char r[80];
    if ((rgn[0] == 'R' || rgn[0] == 'L') && iigs_rom_sym_resolve(pc, r, sizeof(r))[0])
        snprintf(buf, n, "<%s %s>", rgn, r);
    else
        snprintf(buf, n, "<%s>", rgn);
}

// One-shot wild-jump detector: fires the instant execution enters a non-code bank during
// boot ($02-$DF), dumping regs + the symbolized caller ring — pinning the routine that
// jumped/returned into garbage (e.g. a caller that mishandles an SCM carry-set error).
inline void iigs_cpu_state_dump_regs(cpu_state *cpu, const char *why);  // fwd (defined below)
inline void iigs_print_ring_symbolized(int count);                     // fwd (defined below)
inline bool g_wildjump_fired = false;
inline void iigs_wildjump_check(cpu_state *cpu) {
    if (g_wildjump_fired) return;
    uint8_t b = (cpu->full_pc >> 16) & 0xFF;
    if (b==0 || b==1 || b==0xE0 || b==0xE1 || b>=0xF0) return;   // valid boot code bank
    g_wildjump_fired = true;
    printf("IIGS WILDPC: execution entered non-code bank $%02X at %02X/%04X (wild jump during boot)\n",
           (unsigned)b, (unsigned)b, (unsigned)(cpu->full_pc & 0xFFFF));
    iigs_cpu_state_dump_regs(cpu, "WILDPC");
    iigs_print_ring_symbolized(80);
}

// ---- A2GSPU_ITRACE: additive, env-gated, per-instruction execution trace ----
// A standalone full-instruction trace (distinct from the toolbox-scoped trace
// above): logs PC(bank:addr), opcode byte, decoded mnemonic+operand, and the
// full register file, for a bounded window, so a far-RTL-into-garbage crash can
// be pinned to the exact faulting routine. Two arm modes (OR'd):
//   A2GSPU_ITRACE_FROM=<hexPC> : arm when full_pc first equals this 24-bit PC.
//   A2GSPU_ITRACE_FRAME=<N>    : arm at the start of headless frame N.
// A2GSPU_ITRACE_N=<count> caps the number of logged instructions (default 256).
// OFF by default (g_iigs_itrace_enabled gates the per-instruction call site to a
// single cheap branch when off; mirrors the BRKDUMP gating). Logs to stderr so
// it does not corrupt the SHR/golden stdout stream the gates parse.
inline bool     g_iigs_itrace_enabled = false; // master gate (any arm mode set)
inline uint32_t g_iigs_itrace_from    = 0;     // PC arm address (0 = no PC arm)
inline bool     g_iigs_itrace_use_pc  = false; // FROM was supplied
inline int      g_iigs_itrace_frame   = -1;    // frame arm (-1 = no frame arm)
inline bool     g_iigs_itrace_armed   = false; // window is open
inline int      g_iigs_itrace_n       = 256;   // max instructions to log
inline int      g_iigs_itrace_logged  = 0;     // logged so far
inline int      g_iigs_cur_frame      = 0;     // updated by the headless spike loop
// ---- ITRACE v2 (batch-2 port): PC-RANGE gate + effective-address/mem logging +
// file sink + window re-arm. When [lo,hi] is set the window is open whenever pc is
// in range (traces a routine on EVERY entry — a hot loop / re-entered handler — not
// just one window). The per-instruction "@<ea>=<val>" field names which byte a
// load/store actually touches (a routine's data flow, in all 65816 modes).
inline uint32_t g_iigs_itrace_lo      = 0;     // PC-range low (0/0 = disabled)
inline uint32_t g_iigs_itrace_hi      = 0;     // PC-range high (inclusive)
inline FILE    *g_iigs_itrace_out     = nullptr; // file sink (else stderr)
inline int      g_iigs_itrace_rearm   = 0;     // PC-armed window re-open count

inline void iigs_cpu_state_dump_regs(cpu_state *cpu, const char *why);  // fwd
inline const char *iigs_sym_resolve(uint32_t full_pc, char *buf, size_t n);  // fwd (also below)

// ============================================================================
// A2GSPU_MILESTONES=<path> — boot-progress ledger. The file lists "HHHHHH name"
// (24-bit hex addr + label). Prints the FIRST time each milestone PC executes
// (frame + name); at spike end prints a REACHED / *** NOT REACHED *** table.
// Turns crash-driven debugging (probe one routine at a time) into progress-driven
// ("here's how far the boot got"). Off by default; generic, public-safe.
// ============================================================================
struct IigsMilestone { uint32_t addr; std::string name; int frame; };
inline std::vector<IigsMilestone> g_milestones;
inline bool g_milestones_on = false;
inline int  g_milestones_unhit = 0;   // milestones not yet reached; enables an early-out

// All milestone output goes to stderr (not stdout): iigs_milestones_report() is
// called immediately before the golden-hash/SHR-assert gate parses stdout, so a
// stray stdout line would corrupt the captured golden stream.
inline void iigs_milestones_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "IIGS MILESTONES: cannot open '%s'\n", path); return; }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char nm[128]; unsigned a;
        if (sscanf(line, "%x %127s", &a, nm) == 2)
            g_milestones.push_back({a & 0xFFFFFF, std::string(nm), -1});
    }
    fclose(f);
    g_milestones_on = !g_milestones.empty();
    g_milestones_unhit = (int)g_milestones.size();
    fprintf(stderr, "IIGS MILESTONES: %zu loaded from '%s'\n", g_milestones.size(), path);
}
inline void iigs_milestone_check(cpu_state *cpu, int frame) {
    if (g_milestones_unhit <= 0) return;   // all reached — stop the per-instruction scan
    uint32_t pc = cpu->full_pc;
    for (auto &m : g_milestones)
        if (m.frame < 0 && m.addr == pc) {
            m.frame = frame; g_milestones_unhit--;
            fprintf(stderr, "IIGS MILESTONE: reached %s @%02X/%04X (frame %d)\n",
                   m.name.c_str(), (unsigned)(pc >> 16), (unsigned)(pc & 0xFFFF), frame);
        }
}
inline void iigs_milestones_report(void) {
    if (!g_milestones_on) return;
    int hit = 0; for (auto &m : g_milestones) if (m.frame >= 0) hit++;
    fprintf(stderr, "IIGS MILESTONES: %d/%zu reached\n", hit, g_milestones.size());
    for (auto &m : g_milestones)
        if (m.frame < 0) fprintf(stderr, "  *** NOT REACHED: %s ($%06X)\n", m.name.c_str(), m.addr);
}
// True when a milestone ledger is loaded but not every milestone was reached — i.e.
// the boot did not run to completion. Feeds the honest GSDIAG verdict (STALLED).
inline bool iigs_boot_incomplete(void) {
    if (!g_milestones_on) return false;
    for (auto &m : g_milestones) if (m.frame < 0) return true;
    return false;
}

// ============================================================================
// #1 symbolized PC-history ring  +  #2 no-BRK hang / wild-loop detector.
// The raw hex BRKHIST/PCTRAP/STACKTRAP ring is only legible after hand-resolving
// each PC against the symbol table; this prints a NAME+offset tail and COLLAPSES
// consecutive identical resolutions, so a wild-jump crash path (or a garbage crawl
// through unmapped memory) reads at a glance. The hang detector catches a degenerate
// loop — the SAME opcode executed thousands of times in a row (e.g. a wild jump into
// a bank of uninitialized $B0 bytes = BCS-with-carry-set crawling forever) — which
// produces NO BRK, so the plain "no crash" check would otherwise call a silent hang
// "OK". Both use the loaded A2GSPU_SYMBOLS table + the g_pchist ring above.
// ============================================================================
inline void iigs_print_ring_symbolized(int count) {
    if (!g_iigs_syms_loaded || g_pchist_i == 0) return;
    int start = (g_pchist_i > count) ? g_pchist_i - count : 0;
    printf("IIGS BRKHIST (symbolized tail, oldest->newest; runs collapsed):\n ");
    char sym[80], cur[112], last[112] = {0}; int rep = 0;
    for (int k = start; k < g_pchist_i; k++) {
        uint32_t pc = g_pchist[k & 255];
        iigs_sym_resolve(pc, sym, sizeof(sym));
        snprintf(cur, sizeof(cur), "%02X/%04X%s%s", (unsigned)((pc>>16)&0xFF),
                 (unsigned)(pc&0xFFFF), sym[0] ? " " : "", sym[0] ? sym : "?");
        if (strcmp(cur, last) == 0) { rep++; continue; }
        if (rep > 0) { printf(" (x%d)", rep + 1); rep = 0; }
        printf("  %s", cur);
        snprintf(last, sizeof(last), "%s", cur);
    }
    if (rep > 0) printf(" (x%d)", rep + 1);
    printf("\n");
}

inline bool     g_iigs_hang_detected  = false;
inline uint32_t g_iigs_hang_pc        = 0;
inline int      g_iigs_hang_last_op   = -1;
inline int      g_iigs_hang_run       = 0;
// same opcode N× in a row = degenerate loop. Set ABOVE the largest legitimate bounded
// crawl: a MVN/MVP block move is excluded below, but a bounded "wild crawl" through a
// filled region (e.g. 49KB of $EE read as INC-abs) or a large clear loop can still hit
// tens of thousands of same-opcode steps and TERMINATE — firing at 16K falsely halted the
// boot mid-early-init and masked the real (later) hang. 1<<20 passes any bounded op
// (<=64K bytes) yet still catches a truly unbounded/looping spin. A2GSPU_HANG_THRESHOLD overrides.
inline int      g_iigs_hang_threshold = 1 << 20;
inline void iigs_hang_check(cpu_state *cpu) {
    if (g_iigs_hang_detected) return;
    int op = cpu->mmu->probe_peek(cpu->full_pc);   // observation-free (shares page table)
    // MVN($54)/MVP($44) block moves LEGITIMATELY re-execute at the same PC once per
    // byte (up to 65536×) — they are not a hang. Excluding them avoids flagging a large
    // (even a wrong-count/runaway, but terminating) block move as an infinite loop.
    if (op == 0x54 || op == 0x44) { g_iigs_hang_last_op = op; g_iigs_hang_run = 0; return; }
    if (op == g_iigs_hang_last_op) {
        if (++g_iigs_hang_run >= g_iigs_hang_threshold) {
            g_iigs_hang_detected = true;
            g_iigs_hang_pc = cpu->full_pc;
            printf("IIGS HANG: opcode $%02X executed %dx consecutively near %02X/%04X "
                   "(degenerate loop / wild code, no BRK)\n", (unsigned)op, g_iigs_hang_run + 1,
                   (unsigned)((cpu->full_pc>>16)&0xFF), (unsigned)(cpu->full_pc&0xFFFF));
            iigs_cpu_state_dump_regs(cpu, "HANG");
            iigs_print_ring_symbolized(80);
        }
    } else { g_iigs_hang_last_op = op; g_iigs_hang_run = 0; }
}

// ============================================================================
// A2GSPU_RETGUARD — flag an RTS/RTL whose return target (bank $00/$01) lands on a
// $00 byte: real code never returns into a BRK, so a $00 target = zeroed padding /
// unloaded gap / wild return. THREE walls this session were exactly this ($8101
// gldr padding, $01C4FF Loader gap, $00BA zero-page). DEFAULT is intentionally the
// narrow, zero-false-positive $00-target tripwire; it does NOT catch a bad return
// into a live-but-wrong opcode, nor targets outside banks 0/1. A2GSPU_RETGUARD=
// strict ADDS the noisier symbol-gap heuristic (>$400 gap = no nearby symbol) for a
// thorough sweep. Uses the loaded symbol table (A2GSPU_SYMBOLS). Observation-free
// (probe_peek — see below). Off by default. RTL reads a 3-byte bank+addr; RTS a
// 2-byte same-bank addr.
// ============================================================================
inline bool g_retguard_on = false;
inline bool g_retguard_strict = false;   // A2GSPU_RETGUARD=strict: also flag symbol-gap returns
inline int  g_retguard_hits = 0;
inline void iigs_retguard_step(cpu_state *cpu) {
    if (g_retguard_hits >= 64) return;
    uint32_t pc = cpu->full_pc;
    // probe_peek is observation-free AND sees relocated bank-0 RAM: it shares the
    // page_table with read(), so read_raw reflects soft-switched/relocated RAM; it
    // diverges from read() ONLY on handler-served I/O pages, which are never $00
    // BRK-padding. Using read() here would dispatch $C0xx side effects and inject a
    // phantom bus-oracle event on the exact wild-return-into-I/O case we exist to
    // catch — masking the bug and tainting the golden. So: probe_peek throughout.
    uint8_t op = cpu->mmu->probe_peek(pc);
    if (op != 0x60 && op != 0x6B) return;            // RTS / RTL
    uint16_t s = (uint16_t)cpu->sp;
    // Native mode wraps the full 16-bit stack; emulation mode (E=1) confines pulls
    // to page 1 ($01FF->$0100), so hold the high byte and wrap only the low byte.
    // Early IIgs boot runs in E-mode, where a 16-bit wrap would read the wrong page.
    auto sp = [&](int n){
        uint16_t a = cpu->E ? (uint16_t)((s & 0xFF00) | ((s + n) & 0xFF))
                            : (uint16_t)(s + n);
        return cpu->mmu->probe_peek(a);
    };
    uint32_t tgt;
    if (op == 0x60) tgt = ((pc & 0xFF0000) | (((sp(1) | (sp(2) << 8)) + 1) & 0xFFFF));
    else            tgt = (((uint32_t)sp(3) << 16) | (((sp(1) | (sp(2) << 8)) + 1) & 0xFFFF));
    uint32_t bank = (tgt >> 16) & 0xFF;
    if (bank != 0x00 && bank != 0x01) return;        // ROM/$E1 returns are fine
    // ROBUST low-noise signal: a return LANDING ON A $00 (BRK) byte. Real code
    // never RTS/RTL's into a BRK; a $00 target = zeroed padding / unloaded gap /
    // wild return ($8101 gldr pad, $00BA zero-page). The prior "no nearby symbol"
    // heuristic false-fired on valid returns in sparsely-symboled kernel regions
    // (598 valid returns had target bytes 68/20/A9/... never $00), so it is dropped
    // as the default. A2GSPU_RETGUARD=strict re-enables the symbol-gap heuristic
    // for a thorough (noisy) sweep.
    uint8_t tbyte = cpu->mmu->probe_peek(tgt);
    bool bad = (tbyte == 0x00);
    if (!bad && g_retguard_strict) {
        char sym[80]; iigs_sym_resolve(tgt, sym, sizeof(sym));
        const char *p = strstr(sym, "+$");
        bad = !sym[0] || (p && strtoul(p + 2, nullptr, 16) > 0x400);
    }
    if (!bad) return;
    g_retguard_hits++;
    char here[80]; iigs_sym_resolve(pc, here, sizeof(here));
    char ts[80]; iigs_sym_resolve(tgt, ts, sizeof(ts));
    fprintf(stderr, "IIGS RETGUARD: %s @%02X/%04X %s -> %02X/%04X %s (%s)\n",
            op == 0x60 ? "RTS" : "RTL", (unsigned)(pc >> 16), (unsigned)(pc & 0xFFFF),
            here, (unsigned)bank, (unsigned)(tgt & 0xFFFF), ts,
            tbyte == 0x00 ? "target=BRK/padding" : "return into unsymboled region");
}

// A2GSPU_CALLTRACE: call/return-flow trace. Logs JSR/JSL/RTS/RTL/RTI/BRK with a
// depth-indented, symbol-annotated line so the kernel's call tree is legible and
// a bad return (rts to a stale/garbage address) is obvious vs a real dispatch.
// Off by default; one cheap branch when off (mirrors ITRACE). Reads the opcode
// via probe_peek (observation-free). Arm mode mirrors ITRACE: FROM=<hexPC> opens
// the window at first hit; unset = armed from frame 0. stderr, symbol-annotated.
inline bool     g_calltrace_enabled = false;
inline uint32_t g_calltrace_from    = 0;
inline bool     g_calltrace_use_pc  = false;
inline bool     g_calltrace_armed   = false;
inline int      g_calltrace_n       = 4000;
inline int      g_calltrace_logged  = 0;
inline int      g_calltrace_depth   = 0;
inline int      g_calltrace_skip    = 0;    // A2GSPU_CALLTRACE_SKIP: ignore the first N hits of the arm PC
inline const char *iigs_sym_resolve(uint32_t full_pc, char *buf, size_t n);  // fwd

// ---- A2GSPU_CALLSTREAM=<file>: symbol-FREE NDJSON of the toolbox/GS-OS call sequence
// (seq, call word, kind, RAW caller return-addr, S/D/DBR at the call; carry+err at the
// matching return). The data layer for the automated ours-vs-pristine first-divergence
// differ tools/gdiff/calldiff.py (the "call #260 finder"). Immune to symbol aliasing
// (raw hex only). Off => untaken branch; on => writes ONLY to its file (stdout untouched).
inline FILE *g_callstream_out = nullptr;
inline bool  g_callstream_on  = false;
inline int   g_callstream_seq = 0;

// Banking-correct, mostly-non-disturbing opcode read for the instrumentation
// rails.  See the note at the fetch site in iigs_calltrace_step().
static inline uint8_t iigs_instr_peek(cpu_state *cpu, uint32_t a) {
    uint16_t off = (uint16_t)(a & 0xFFFF);
    if (off >= 0xC000 && off <= 0xCFFF) return cpu->mmu->probe_peek(a);
    return cpu->mmu->read(a);
}

inline void iigs_calltrace_step(cpu_state *cpu) {
    if (!g_calltrace_armed && g_calltrace_use_pc &&
        cpu->full_pc == g_calltrace_from) {
        // A2GSPU_CALLTRACE_SKIP=N: wait for the (N+1)th hit of the arm PC, so a
        // loop-resident PC (e.g. the crashing iteration of a boot retry loop)
        // can be reached instead of the first benign pass.
        if (g_calltrace_skip > 0) { g_calltrace_skip--; return; }
        g_calltrace_armed = true;
        fprintf(stderr, "IIGS CALLTRACE: armed at %02X/%04X (frame %d)\n",
                cpu->pb, cpu->pc, g_iigs_cur_frame);
    }
    if (!g_calltrace_armed || g_calltrace_logged >= g_calltrace_n) return;
    uint32_t pc = cpu->full_pc;
    // Opcode fetch for instrumentation.  probe_peek() is the side-effect-free
    // read, but it returns the RAW page-table byte and so does not resolve
    // ROM / language-card banking the way a real fetch does -- on the II family
    // that made every opcode compare here fail, and CALLTRACE emitted nothing at
    // all on -p 3 even with thousands of JSRs executing.  read() resolves banking
    // correctly (it is what ITRACE already uses), at the cost of soft-switch side
    // effects in the $C000-$CFFF I/O page -- so fall back to probe_peek there to
    // keep the probe non-disturbing.
    uint8_t  op = iigs_instr_peek(cpu, pc);
    if (op != 0x20 && op != 0x22 && op != 0xFC && op != 0x60 &&
        op != 0x6B && op != 0x40 && op != 0x00) return;
    // Operand bytes need the same banking-correct read as the opcode.  With
    // probe_peek these came back as floating-bus $EE for banks $00/$01 on the
    // IIgs, so every logged call target read "-> 00/EEEE" -- the trace named the
    // caller correctly and the callee not at all.
    auto pk = [&](uint32_t o){ return iigs_instr_peek(cpu, (pc & 0xFF0000) | ((pc + o) & 0xFFFF)); };
    char here[80]; iigs_sym_resolve(pc, here, sizeof(here));
    char tgt[80]; tgt[0] = 0;
    const char *kind = "?";
    int ddisp = g_calltrace_depth;
    if (op == 0x20) {                       // JSR abs (same bank)
        kind = "JSR"; uint32_t t = (pc & 0xFF0000) | (pk(1) | (pk(2) << 8));
        char s[80]; iigs_sym_resolve(t, s, sizeof(s));
        snprintf(tgt, sizeof(tgt), "%02X/%04X %s", (unsigned)(t >> 16), (unsigned)(t & 0xFFFF), s);
    } else if (op == 0x22) {                // JSL long
        kind = "JSL"; uint32_t t = pk(1) | (pk(2) << 8) | (pk(3) << 16);
        char s[80]; iigs_sym_resolve(t, s, sizeof(s));
        snprintf(tgt, sizeof(tgt), "%02X/%04X %s", (unsigned)(t >> 16), (unsigned)(t & 0xFFFF), s);
    } else if (op == 0xFC) { kind = "JSR(x)"; }
    else if (op == 0x60)   { kind = "RTS"; if (g_calltrace_depth > 0) g_calltrace_depth--; ddisp = g_calltrace_depth; }
    else if (op == 0x6B)   { kind = "RTL"; if (g_calltrace_depth > 0) g_calltrace_depth--; ddisp = g_calltrace_depth; }
    else if (op == 0x40)   { kind = "RTI"; if (g_calltrace_depth > 0) g_calltrace_depth--; ddisp = g_calltrace_depth; }
    else if (op == 0x00)   { kind = "BRK"; }
    int ind = ddisp; if (ind > 24) ind = 24;
    fprintf(stderr, "IIGS CALL: %*s%-6s @%02X/%04X %-22s%s%s\n",
            ind * 2, "", kind, (unsigned)(pc >> 16), (unsigned)(pc & 0xFFFF), here,
            tgt[0] ? " -> " : "", tgt);
    // JSR/JSL and JSR(abs,x) all push a return frame; count all three so the depth
    // indent stays balanced against their RTS. (Interrupt-driven RTIs can still skew
    // the indent — it is a legibility aid, not a verified nesting level.)
    if (op == 0x20 || op == 0x22 || op == 0xFC) g_calltrace_depth++;
    g_calltrace_logged++;
}

// Per-instruction trace step. Called at the landing PC (cpu->full_pc), BEFORE
// fetch, identically to iigs_tb_on_landing. Reads the 3 operand bytes straight
// off the MMU so the decode reflects the live image (catches a corrupted byte).
// The operand width of IMM/REP/SEP-affected ops is approximated from the disasm
// table's fixed size (this is a crash post-mortem, not a cycle model). v2 adds a
// PC-RANGE window, a per-instruction effective-address/value column, a file sink,
// and window RE-ARM (A2GSPU_ITRACE_REARM) so several windows come from one run.
inline void iigs_itrace_step(cpu_state *cpu) {
    // Window exhausted: optionally re-arm on the NEXT hit of the arm PC.
    if (g_iigs_itrace_armed && g_iigs_itrace_logged >= g_iigs_itrace_n &&
        g_iigs_itrace_rearm > 0 && g_iigs_itrace_use_pc) {
        g_iigs_itrace_armed = false;
        g_iigs_itrace_logged = 0;
        g_iigs_itrace_rearm--;
        fprintf(stderr, "IIGS ITRACE: window closed; %d re-arm(s) remain\n",
                g_iigs_itrace_rearm);
    }
    // Arm on PC match — independent of the frame arm.
    if (!g_iigs_itrace_armed && g_iigs_itrace_use_pc &&
        cpu->full_pc == g_iigs_itrace_from) {
        g_iigs_itrace_armed = true;
        fprintf(stderr, "IIGS ITRACE: armed at PC=%02X/%04X (frame %d)\n",
                cpu->pb, cpu->pc, g_iigs_cur_frame);
    }
    // PC-RANGE mode: armed exactly while pc is in [lo,hi]. Re-evaluated every step,
    // so a routine is traced on every entry (a hot loop / re-entered handler).
    if (g_iigs_itrace_lo || g_iigs_itrace_hi) {
        g_iigs_itrace_armed =
            (cpu->full_pc >= g_iigs_itrace_lo && cpu->full_pc <= g_iigs_itrace_hi);
    }
    if (!g_iigs_itrace_armed) return;
    if (g_iigs_itrace_logged >= g_iigs_itrace_n) return;

    uint32_t pc = cpu->full_pc;
    uint8_t  op = cpu->mmu->read(pc);
    uint8_t  b1 = cpu->mmu->read((pc & 0xFF0000) | ((pc + 1) & 0xFFFF));
    uint8_t  b2 = cpu->mmu->read((pc & 0xFF0000) | ((pc + 2) & 0xFFFF));
    uint8_t  b3 = cpu->mmu->read((pc & 0xFF0000) | ((pc + 3) & 0xFFFF));

    const disasm_entry       *de = &disasm_table[op];
    const address_mode_entry *am = &address_mode_formats[de->mode];

    // Build the operand string from the format + the live bytes. REL/REL_L
    // also show the resolved branch target.
    char operand[48]; operand[0] = '\0';
    char extra[40];   extra[0] = '\0';
    switch (de->mode) {
        case IMP: case ACC:
            break;
        case IMM: case REL: case ZP: case ZP_X: case ZP_Y:
        case INDEX_INDIR: case INDIR_INDEX: case ZP_IND:
        case REL_S: case REL_S_Y: case IND_LONG: case IND_Y_LONG:
            snprintf(operand, sizeof(operand), am->format, b1);
            if (de->mode == REL) {
                int8_t d = (int8_t)b1;
                uint16_t tgt = (uint16_t)((pc & 0xFFFF) + 2 + d);
                snprintf(extra, sizeof(extra), " ->%02X/%04X",
                         (unsigned)(pc >> 16), tgt);
            }
            break;
        case ABS: case ABS_X: case ABS_Y: case INDIR: case ABS_IND_X:
        case REL_L: case ABS_IND_LONG: case IMM_S:
            snprintf(operand, sizeof(operand), am->format,
                     (unsigned)(b1 | (b2 << 8)));
            if (de->mode == REL_L) {
                int16_t d = (int16_t)(b1 | (b2 << 8));
                uint16_t tgt = (uint16_t)((pc & 0xFFFF) + 3 + d);
                snprintf(extra, sizeof(extra), " ->%02X/%04X",
                         (unsigned)(pc >> 16), tgt);
            }
            break;
        case ABSL: case ABSL_X:
            snprintf(operand, sizeof(operand), am->format,
                     (unsigned)(b1 | (b2 << 8) | (b3 << 16)));
            break;
        case MOVE:
            snprintf(operand, sizeof(operand), am->format, b1, b2);
            break;
        default:
            snprintf(operand, sizeof(operand), "?%02X", b1);
            break;
    }

    // Effective address + memory byte for load/store modes (v2). This names WHICH
    // byte the routine reads (a transform input) or stores to (an output), so a
    // routine's data flow is read directly off the trace (all 65816 modes).
    uint32_t ea = 0xFFFFFFFF; int mval = -1;
    {
        uint16_t dp = cpu->d; uint32_t dbr = (uint32_t)cpu->db << 16;
        auto rd = [&](uint32_t a){ return cpu->mmu->read(a & 0xFFFFFF); };
        switch (de->mode) {
            case ZP:    ea = (uint16_t)(dp + b1); break;
            case ZP_X:  ea = (uint16_t)(dp + b1 + cpu->x); break;
            case ZP_Y:  ea = (uint16_t)(dp + b1 + cpu->y); break;
            case ABS:   ea = dbr | (b1 | (b2 << 8)); break;
            case ABS_X: ea = dbr + (b1 | (b2 << 8)) + cpu->x; break;
            case ABS_Y: ea = dbr + (b1 | (b2 << 8)) + cpu->y; break;
            case ABSL:  ea = b1 | (b2 << 8) | (b3 << 16); break;
            case ABSL_X:ea = (b1 | (b2 << 8) | (b3 << 16)) + cpu->x; break;
            case INDIR_INDEX: { uint16_t pa=(uint16_t)(dp+b1); uint16_t p=rd(pa)|(rd((uint16_t)(pa+1))<<8); ea=dbr+p+cpu->y; } break;
            case INDEX_INDIR: { uint16_t pa=(uint16_t)(dp+b1+cpu->x); uint16_t p=rd(pa)|(rd((uint16_t)(pa+1))<<8); ea=dbr+p; } break;
            case ZP_IND:      { uint16_t pa=(uint16_t)(dp+b1); uint16_t p=rd(pa)|(rd((uint16_t)(pa+1))<<8); ea=dbr+p; } break;
            case IND_LONG:    { uint16_t pa=(uint16_t)(dp+b1); ea=rd(pa)|(rd((uint16_t)(pa+1))<<8)|(rd((uint16_t)(pa+2))<<16); } break;
            case IND_Y_LONG:  { uint16_t pa=(uint16_t)(dp+b1); ea=(rd(pa)|(rd((uint16_t)(pa+1))<<8)|(rd((uint16_t)(pa+2))<<16))+cpu->y; } break;
            default: break;
        }
        if (ea != 0xFFFFFFFF) {
            uint16_t lo16 = ea & 0xFFFF;
            if (!(lo16 >= 0xC000 && lo16 <= 0xC0FF)) mval = rd(ea);  // skip soft-switch I/O
        }
    }
    char eabuf[24]; eabuf[0] = '\0';
    if (ea != 0xFFFFFFFF) {
        if (mval >= 0) snprintf(eabuf, sizeof(eabuf), " @%06X=%02X", ea & 0xFFFFFF, mval);
        else           snprintf(eabuf, sizeof(eabuf), " @%06X", ea & 0xFFFFFF);
    }

    char sym[80]; const char *rgn = iigs_pc_region(cpu, pc);
    if (rgn[0]) iigs_region_tag(pc, rgn, sym, sizeof(sym));  // ROM/IO/LC-ROM (ROM-symbolized via A2GSPU_ROM_SYMBOLS)
    else iigs_sym_resolve(pc, sym, sizeof(sym));
    FILE *out = g_iigs_itrace_out ? g_iigs_itrace_out : stderr;
    fprintf(out,
        "ITRACE %02X/%04X: %02X %-4s %-12s%s "
        "A=%04X X=%04X Y=%04X S=%04X D=%04X DBR=%02X P=%02X e=%d%s%s%s\n",
        cpu->pb, cpu->pc, op, de->opcode, operand, extra,
        cpu->a, cpu->x, cpu->y, cpu->sp, cpu->d, cpu->db, cpu->p, (int)cpu->E,
        eabuf, sym[0] ? "  " : "", sym);
    g_iigs_itrace_logged++;
    if (g_iigs_itrace_logged == g_iigs_itrace_n)
        fprintf(stderr, "IIGS ITRACE: window full (%d instrs)\n",
                g_iigs_itrace_n);
}

// ============================================================================
// A2GSPU_SNAP region-logger (batch-2 port). On hitting any of up to 8 configured
// SNAP_PCS (16-bit PC compare), dump the SNAP_LO..HI memory window as hex plus the
// CPU registers, one NDJSON record per hit, to SNAP_OUT. Aim the PCs at a routine's
// entry + exit and the window at its working buffer: consecutive records give the
// (before, after) pair — how the routine transformed the buffer — with no PC-chasing.
//   A2GSPU_SNAP_LO/HI=<hex24>   the window to dump (inclusive)
//   A2GSPU_SNAP_PCS="p1,p2,..." up to 8 trigger PCs (16-bit, hex)
//   A2GSPU_SNAP_OUT=<file>      NDJSON sink (its presence also arms the logger)
// Off by default (g_snap_out null => one untaken branch). Reads via mmu->read(),
// skipping the $C000-$C0FF soft-switch page (sensor-safe).
// ============================================================================
inline FILE    *g_snap_out    = nullptr;
inline uint32_t g_snap_pcs[8] = {0};
inline int      g_snap_npc    = 0;
inline uint32_t g_snap_lo = 0, g_snap_hi = 0;
inline int      g_snap_max = 8000, g_snap_count = 0;

inline void iigs_snap_step(cpu_state *cpu) {
    if (!g_snap_out || g_snap_count >= g_snap_max) return;
    uint32_t pc = cpu->full_pc & 0xFFFF;
    bool hit = false;
    for (int i = 0; i < g_snap_npc; i++) if (g_snap_pcs[i] == pc) { hit = true; break; }
    if (!hit) return;
    auto rd = [&](uint32_t a){ return cpu->mmu->read(a & 0xFFFFFF); };
    fprintf(g_snap_out,
        "{\"seq\":%d,\"pc\":%u,\"a\":%u,\"x\":%u,\"y\":%u,\"s\":%u,\"d\":%u,"
        "\"dbr\":%u,\"lo\":%u,\"data\":\"",
        g_snap_count, (unsigned)pc, (unsigned)cpu->a, (unsigned)cpu->x,
        (unsigned)cpu->y, (unsigned)cpu->sp, (unsigned)cpu->d, (unsigned)cpu->db,
        (unsigned)g_snap_lo);
    for (uint32_t a = g_snap_lo; a <= g_snap_hi; a++) {
        uint16_t lo16 = a & 0xFFFF;
        uint8_t v = (lo16 >= 0xC000 && lo16 <= 0xC0FF) ? 0 : rd(a);  // skip soft-switch I/O
        fprintf(g_snap_out, "%02x", v);
    }
    fprintf(g_snap_out, "\"}\n");
    g_snap_count++;
}

// Frame-arm hook: called from the headless spike loop once per frame so an
// A2GSPU_ITRACE_FRAME=N arm opens the window at the right frame even when no
// single PC is known. Idempotent; only opens, never closes.
inline void iigs_itrace_frame_tick(int frame) {
    g_iigs_cur_frame = frame;
    if (!g_iigs_itrace_armed && g_iigs_itrace_frame >= 0 &&
        frame >= g_iigs_itrace_frame) {
        g_iigs_itrace_armed = true;
        fprintf(stderr, "IIGS ITRACE: armed at frame %d\n", frame);
    }
}

struct IigsPendingCall {
    uint32_t ret_addr;    // full 24-bit address the matching RTL returns to
    uint16_t callword;    // tool word (func<<8 | toolset); 0 for a GS/OS call
    uint8_t  kind;        // 0 = toolbox, 1 = GS/OS class-1, 2 = GS/OS class-0
    uint8_t  caller_bank; // bank of the caller (for trace scoping)
};
inline std::vector<IigsPendingCall> g_iigs_pending;

// Last (carry, A) result per tool word — consumed by the assertion gate.
inline std::map<uint16_t, std::pair<bool, uint16_t>> g_iigs_last_result;
// Last GS/OS dispatch carry-set error code (surfaces $1104 / $110A loader errors).
inline uint16_t g_iigs_last_gsos_err = 0;

inline const char *iigs_tool_name(uint16_t w) {
    switch (w) {
        case 0x0201: return "TLStartUp";      case 0x0301: return "TLShutDown";
        case 0x0E01: return "LoadTools";      case 0x0F01: return "LoadOneTool";
        case 0x0202: return "MMStartUp";      case 0x0302: return "MMShutDown";
        case 0x0902: return "NewHandle";      case 0x1002: return "DisposeHandle";
        case 0x2003: return "GetNewID";       case 0x1D03: return "DeleteID";
        case 0x0303: return "MTStartUp";      case 0x0403: return "MTShutDown";
        case 0x0204: return "QDStartUp";      case 0x0304: return "QDShutDown";
        case 0x1504: return "ClearScreen";    case 0x5404: return "PaintRect";
        case 0x3704: return "SetSolidPenPat"; case 0x0206: return "EMStartUp";
        default: return "(tool)";
    }
}

// ============================================================================
// A2GSPU_SAVE_AT / A2GSPU_RESTORE — save/restore at an ARBITRARY breakpoint (the
// run-to-a-point / modify / re-run primitive). A2GSPU_SAVE_AT=<hexPC> writes a full
// machine snapshot (the SAME cpu_state + MMU_IIgs image the SNAP_SAVE path writes)
// the FIRST time execution reaches that 24-bit PC, then halts the spike — so the
// checkpoint is captured at the EXACT instruction (before its fetch), not at
// end-of-frame. The heavy lift (a2gspu_cpu_save + MMU_IIgs::A2GSPU_snapshot, which
// live in gs2.cpp with the mmu_iigs handle) is invoked through a host-registered
// callback; when SAVE_AT is unset the callback stays null and this is one untaken
// branch. A2GSPU_RESTORE=<file> is parsed in gs2.cpp as an alias of SNAP_LOAD (load
// at start — the existing mechanism this generalizes). Off by default; public-safe.
// ============================================================================
inline bool     g_save_at_enabled = false;
inline uint32_t g_save_at_addr    = 0;
inline bool     g_save_at_fired   = false;
inline void   (*g_save_at_fn)(cpu_state *cpu) = nullptr;   // set by the host (gs2.cpp)

// ============================================================================
// A2GSPU_POKE — mid-run state injection / splice at a PC (the control primitive).
// A2GSPU_POKE="<hexPC>:<action>[;<action>...]" applies, the FIRST time execution
// reaches <hexPC>, a list of DELIBERATE mutations: set a register/flag byte
// (A/X/Y/S/D/P/DBR/PB), force-branch (PC=<hex>), poke memory (M<hex24>=<hexByte>),
// force-return (RTS pops 2+1, RTL pops 3+1), or skip N bytes (SKIP=<n>). This is
// NOT faithful hardware behaviour — it is a deliberate splice — so every applied
// action is logged loudly ("IIGS POKE ...") and it is one-shot + fully gated, so it
// can never be mistaken for real emulated behaviour. Parsed in gs2.cpp into
// g_poke_actions; applied here. Off by default => one untaken branch.
// ============================================================================
struct IigsPokeAction { char kind; uint32_t addr; uint32_t val; };
inline bool                        g_poke_on    = false;
inline uint32_t                    g_poke_pc    = 0;
inline bool                        g_poke_fired = false;
inline int                         g_poke_nth   = 1;   // A2GSPU_POKE_NTH: fire on the Nth PC hit (default 1 = first)
inline int                         g_poke_hits  = 0;   // occurrence counter for Nth-hit targeting
inline std::vector<IigsPokeAction> g_poke_actions;

inline void iigs_poke_apply(cpu_state *cpu) {
    fprintf(stderr, "IIGS POKE: *** DELIBERATE state injection at PC=%02X/%04X "
                    "(a splice, NOT faithful HW behaviour) ***\n", cpu->pb, cpu->pc);
    for (auto &a : g_poke_actions) {
        switch (a.kind) {
            case 'A': cpu->a  = (uint16_t)a.val; fprintf(stderr, "IIGS POKE:   A=$%04X\n",   cpu->a);  break;
            case 'X': cpu->x  = (uint16_t)a.val; fprintf(stderr, "IIGS POKE:   X=$%04X\n",   cpu->x);  break;
            case 'Y': cpu->y  = (uint16_t)a.val; fprintf(stderr, "IIGS POKE:   Y=$%04X\n",   cpu->y);  break;
            case 'S': cpu->sp = (uint16_t)a.val; fprintf(stderr, "IIGS POKE:   S=$%04X\n",   cpu->sp); break;
            case 'D': cpu->d  = (uint16_t)a.val; fprintf(stderr, "IIGS POKE:   D=$%04X\n",   cpu->d);  break;
            case 'P': cpu->p  = (uint8_t)a.val;  fprintf(stderr, "IIGS POKE:   P=$%02X\n",   cpu->p);  break;
            case 'B': cpu->db = (uint8_t)a.val;  fprintf(stderr, "IIGS POKE:   DBR=$%02X\n", cpu->db); break;
            case 'K': cpu->pb = (uint8_t)a.val;  fprintf(stderr, "IIGS POKE:   PB=$%02X\n",  cpu->pb); break;
            case 'J': cpu->pc = (uint16_t)a.val; fprintf(stderr, "IIGS POKE:   force-branch PC=$%04X\n", cpu->pc); break;
            case 'M': cpu->mmu->write(a.addr, (uint8_t)a.val);
                      fprintf(stderr, "IIGS POKE:   M[$%06X]=$%02X\n", a.addr, (uint8_t)(a.val & 0xFF)); break;
            case 'R': { // force-return RTS: pull 2 bytes off the stack, PC = addr+1 (same bank)
                      uint16_t s = cpu->sp;
                      uint8_t lo = cpu->mmu->read((s + 1) & 0xFFFF);
                      uint8_t hi = cpu->mmu->read((s + 2) & 0xFFFF);
                      cpu->sp = (uint16_t)(s + 2);
                      cpu->pc = (uint16_t)(((hi << 8) | lo) + 1);
                      fprintf(stderr, "IIGS POKE:   force-RTS -> %02X/%04X\n", cpu->pb, cpu->pc); break; }
            case 'L': { // force-return RTL: pull 3 bytes off the stack, PC/PB = addr+1
                      uint16_t s = cpu->sp;
                      uint8_t lo = cpu->mmu->read((s + 1) & 0xFFFF);
                      uint8_t hi = cpu->mmu->read((s + 2) & 0xFFFF);
                      uint8_t bk = cpu->mmu->read((s + 3) & 0xFFFF);
                      cpu->sp = (uint16_t)(s + 3);
                      cpu->pc = (uint16_t)(((hi << 8) | lo) + 1);
                      cpu->pb = bk;
                      fprintf(stderr, "IIGS POKE:   force-RTL -> %02X/%04X\n", cpu->pb, cpu->pc); break; }
            case 'N': cpu->pc = (uint16_t)(cpu->pc + a.val);
                      fprintf(stderr, "IIGS POKE:   skip %u bytes -> PC=$%04X\n", a.val, cpu->pc); break;
        }
    }
}

// ============================================================================
// A2GSPU_INTLOG — interrupt-entry logger. Logs the fired reason (IRQ / BRK / COP),
// the vector location the handler address is read from, and the state at entry
// (PC / P / S / e), at the IRQ dispatch (base_6502.cpp) and inside brk_cop for
// BRK/COP. NMI and ABORT have DEFINED vectors (cpu.hpp NMI_VECTOR / ABORTB_VECTOR
// and their native forms) but the 65816 core never DISPATCHES them, so there is no
// entry site to hook — iigs_intlog_nmi_abort_stub() documents that gap; wiring it
// is deferred until NMI/ABORT dispatch is actually implemented (past-DR
// faithfulness). Off by default; call sites guard on g_intlog_on. stderr.
// ============================================================================
inline bool g_intlog_on   = false;
inline int  g_intlog_hits = 0;
inline void iigs_intlog(cpu_state *cpu, const char *reason, uint16_t vector) {
    if (g_intlog_hits >= 512) return;
    g_intlog_hits++;
    fprintf(stderr, "IIGS INT: %-3s vector=$%04X  at PC=%02X/%04X P=%02X S=$%04X e=%d\n",
            reason, vector, cpu->pb, cpu->pc, cpu->p, cpu->sp, (int)cpu->E);
}
// Log-only stub: NMI/ABORT are defined-but-never-dispatched by the core (see the
// cpu.hpp vectors). No call site exists yet; full dispatch + logging is deferred.
inline void iigs_intlog_nmi_abort_stub(cpu_state *cpu, const char *reason, uint16_t vector) {
    if (!g_intlog_on) return;
    iigs_intlog(cpu, reason, vector);
}

// ============================================================================
// A2GSPU_MODETRACE — CPU mode-transition events. Logs emulation-flag changes via
// XCE (e 0<->1) and accumulator/index width changes via REP/SEP (the M/X bits of
// P), each with the PC of the causing instruction. Makes the E/M/X width-mode
// timeline (a frequent 65816 confound) legible. Off by default; the opcode sites
// guard on g_modetrace_on and are compiled out of the 6502/65C02 cores. stderr.
// ============================================================================
inline bool g_modetrace_on = false;

inline void iigs_tb_on_landing(cpu_state *cpu) {
    uint32_t lpc = cpu->full_pc;

    // Headless breakpoint + the instant-stop substrate (#10/#3): a full 24-bit
    // PC compare (not the GUI's 16-bit-only matcher). Set halt -> the in-flight
    // frame finishes, then run_one_frame's top-of-frame guard exits the spike.
    if (g_iigs_break_enabled && lpc == g_iigs_break_addr) {
        iigs_cpu_state_dump_regs(cpu, "BREAK");
        cpu->halt = 2 /* HLT_USER */;
    }

    // A2GSPU_SAVE_AT: snapshot the machine at the EXACT instruction the first time
    // PC reaches the target, then halt (the arbitrary-breakpoint checkpoint). The
    // save itself runs in the host callback (has the gs2.cpp save fns + mmu handle).
    if (g_save_at_enabled && !g_save_at_fired && lpc == g_save_at_addr) {
        g_save_at_fired = true;
        if (g_save_at_fn) g_save_at_fn(cpu);
        cpu->halt = 2 /* HLT_USER */;
    }
    // A2GSPU_POKE: one-shot DELIBERATE state injection/splice at the target PC. With
    // A2GSPU_POKE_NTH=<n> the splice fires on the Nth time PC reaches poke_pc (default
    // 1 = first hit) -- lets a scout target a "write-N" wall (e.g. the 24th queue write).
    if (g_poke_on && !g_poke_fired && lpc == g_poke_pc) {
        if (++g_poke_hits >= g_poke_nth) {
            g_poke_fired = true;
            iigs_poke_apply(cpu);
        }
    }

    if (!g_iigs_tbtrace_enabled && !g_callstream_on) return;

    // trace_from: begin logging only once PC first reaches the trigger address.
    if (g_iigs_trace_from && !g_iigs_trace_armed && lpc == g_iigs_trace_from)
        g_iigs_trace_armed = true;
    bool log_window = (g_iigs_trace_from == 0) || g_iigs_trace_armed;

    char sym[80];

    // (1) Return match.
    if (!g_iigs_pending.empty() && lpc == g_iigs_pending.back().ret_addr) {
        IigsPendingCall pend = g_iigs_pending.back();
        g_iigs_pending.pop_back();
        bool m8 = cpu->E || (cpu->p & 0x20);
        uint16_t aval = m8 ? (uint16_t)(cpu->a & 0xFF) : cpu->a;
        bool carry = (cpu->p & FLAG_C) != 0;
        if (g_callstream_on)
            fprintf(g_callstream_out, "{\"ev\":\"r\",\"word\":%u,\"kind\":%u,\"carry\":%d,\"err\":%u}\n",
                    pend.callword, pend.kind, carry ? 1 : 0, aval);
        bool bank_ok = (g_iigs_tbtrace_bank < 0) || (pend.caller_bank == g_iigs_tbtrace_bank);
        if (pend.kind == 0) {
            g_iigs_last_result[pend.callword] = {carry, aval};   // for the assert gate
            if (g_iigs_tbtrace_enabled && log_window && bank_ok)
                printf("IIGS TOOLBOX: ret %s ($%04X) carry=%d err=$%04X\n",
                       iigs_tool_name(pend.callword), pend.callword, carry ? 1 : 0, aval);
        } else {
            if (carry) g_iigs_last_gsos_err = aval;
            if (g_iigs_tbtrace_enabled && log_window && bank_ok && (g_iigs_errhook_enabled || carry))
                printf("IIGS GSOS: ret (class-%d) carry=%d err=$%04X%s\n",
                       pend.kind == 1 ? 1 : 0, carry ? 1 : 0, aval,
                       carry ? "  <-- SYSTEM/LOADER ERROR" : "");
        }
    }

    // (2) Call detect.
    uint8_t kind = 0xFF;
    if (lpc == 0xE10000) kind = 0;
    else if (lpc == 0xE100A8) kind = 1;
    else if (lpc == 0xE100B0) kind = 2;
    if (kind == 0xFF) return;

    uint16_t sp = cpu->sp;
    uint32_t pcl = cpu->mmu->read((sp + 1) & 0xFFFF);
    uint32_t pch = cpu->mmu->read((sp + 2) & 0xFFFF);
    uint32_t pbr = cpu->mmu->read((sp + 3) & 0xFFFF);
    uint32_t ret = (pbr << 16) | ((((pch << 8) | pcl) + 1) & 0xFFFF);
    uint8_t caller_bank = (uint8_t)pbr;
    bool bank_ok = (g_iigs_tbtrace_bank < 0) || (caller_bank == g_iigs_tbtrace_bank);

    // Auto-bridge the symbolizer load base (best-effort): a GS/OS-loaded app's
    // first toolbox call reveals the runtime bank GS/OS relocated it to. Correct
    // to the bank; may be off by the within-bank load offset. A2GSPU_SYM_BASE
    // pins it explicitly (locked). App region = banks $02..$DF (not ROM/Mega II).
    if (g_iigs_tbtrace_enabled && g_iigs_syms_loaded && !g_iigs_sym_base_locked && g_iigs_sym_base == 0 &&
        caller_bank >= 0x02 && caller_bank < 0xE0) {
        g_iigs_sym_base = (uint32_t)caller_bank << 16;
        printf("IIGS SYM: auto-base $%06X (from bank-$%02X toolbox call)\n",
               g_iigs_sym_base, caller_bank);
    }

    uint16_t callword = 0;
    if (kind == 0) {
        bool x8 = cpu->E || (cpu->p & 0x10);
        callword = x8 ? (uint16_t)(cpu->x & 0xFF) : cpu->x;
        if (g_iigs_tbtrace_enabled && log_window && bank_ok) {
            iigs_sym_resolve(ret - 1, sym, sizeof(sym));
            printf("IIGS TOOLBOX: call %s ($%04X)  from %02X/%04X%s%s\n",
                   iigs_tool_name(callword), callword,
                   (unsigned)(((ret - 1) >> 16) & 0xFF), (unsigned)((ret - 1) & 0xFFFF),
                   sym[0] ? " " : "", sym);
            // SysFailMgr ($1503) = fatal system-failure display. Dump the caller +
            // the pushed params (error code + message ptr) so a boot that dies here
            // reveals WHICH fatal condition fired (the stack words above the JSL RTA).
            if (callword == 0x1503) {
                auto rd16 = [&](uint16_t a){ return (uint16_t)(cpu->mmu->read(a) | (cpu->mmu->read((a+1)&0xFFFF)<<8)); };
                printf("IIGS SYSFAIL: caller=$%06X stack +4=$%04X +6=$%04X +8=$%04X +10=$%04X A=$%04X\n",
                       ret - 1, rd16((sp+4)&0xFFFF), rd16((sp+6)&0xFFFF), rd16((sp+8)&0xFFFF),
                       rd16((sp+10)&0xFFFF), (cpu->E || (cpu->p & 0x20)) ? (uint16_t)(cpu->a & 0xFF) : cpu->a);
                // The $FE98xx "caller" is just the ROM tool dispatcher; the true
                // invoker is deeper — dump the PC ring so the real caller chain shows.
                printf("IIGS SYSFAIL caller ring (oldest->newest):\n ");
                int hs = (g_pchist_i > 48) ? g_pchist_i - 48 : 0;
                for (int k = hs; k < g_pchist_i; k++)
                    printf(" %02X/%04X", (g_pchist[k & 255] >> 16) & 0xFF, g_pchist[k & 255] & 0xFFFF);
                printf("\n");
                // A2GSPU_TRAPDUMP="base:len" — dump a memory region at the fatal death
                // (observation-free probe_peek), e.g. the heartbeat taskheader.
                if (g_trap_dump_len) {
                    printf("IIGS SYSFAIL dump $%06X..+%u probe_peek:", g_trap_dump_base, g_trap_dump_len);
                    int suspects = 0;
                    for (uint32_t a = g_trap_dump_base; a < g_trap_dump_base + g_trap_dump_len; a++) {
                        uint8_t v = cpu->mmu->probe_peek(a);
                        if (iigs_probe_suspect(a, v)) suspects++;
                        printf(" %02X", v);
                    }
                    if (suspects)
                        printf("  [** %d SUSPECT $EE: bank $00/$01 floating-bus, do NOT trust as memory truth **]",
                               suspects);
                    printf("\n");
                }
            }
        }
    } else if (log_window && bank_ok && g_iigs_errhook_enabled) {
        printf("IIGS GSOS: call class-%d dispatch @ $%06X\n", kind == 1 ? 1 : 0, lpc);
    }
    if (g_callstream_on)
        fprintf(g_callstream_out,
                "{\"ev\":\"c\",\"seq\":%d,\"word\":%u,\"kind\":%u,\"caller\":%u,\"s\":%u,\"d\":%u,\"dbr\":%u}\n",
                g_callstream_seq++, callword, kind, (unsigned)((ret - 1) & 0xFFFFFF),
                (unsigned)cpu->sp, (unsigned)cpu->d, (unsigned)caller_bank);
    g_iigs_pending.push_back({ret, callword, kind, caller_bank});
    if (g_iigs_pending.size() > 128) g_iigs_pending.erase(g_iigs_pending.begin());
}

// Regs-only CPU-state dump (no MMU/disassembler dependency) — safe from the
// CPU core (BRK/break). Symbolizes the PC when a symbol table is loaded.
inline void iigs_cpu_state_dump_regs(cpu_state *cpu, const char *why) {
    char sym[80]; const char *rgn = iigs_pc_region(cpu, cpu->full_pc);
    if (rgn[0]) iigs_region_tag(cpu->full_pc, rgn, sym, sizeof(sym));  // ROM/IO/LC-ROM (ROM-symbolized via A2GSPU_ROM_SYMBOLS)
    else iigs_sym_resolve(cpu->full_pc, sym, sizeof(sym));
    printf("IIGS CPU [%s]: PC=%02X/%04X%s%s A=%04X X=%04X Y=%04X S=%04X D=%04X "
           "DBR=%02X P=%02X e=%d\n",
           why ? why : "", cpu->pb, cpu->pc, sym[0] ? " " : "", sym,
           cpu->a, cpu->x, cpu->y, cpu->sp, cpu->d, cpu->db, cpu->p, (int)cpu->E);
}
