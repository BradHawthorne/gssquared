#pragma once
#include "a2gspu_rails_registry.hpp"   // a2rails::Registrar: this rail registers its own reader (M8)
#include "iigs_ioregs.hpp"     // names for $C000-$C0FF, so a device total is never a bare number
inline const char *a2gspu_name_pc(uint32_t full_pc, char *buf, size_t n);     // iigs_symbols.hpp (names by default)
inline std::string iigs_tool_func_name(unsigned tool, unsigned fn);            // iigs_symbols.hpp: a call word's name (report path only)
inline void iigs_syms_flush_note();                                             // iigs_symbols.hpp: a pending one-line note
// ============================================================================
// memvu_storevis.hpp — memory access-visibility accounting
//   MEMVU_STOREVIS : of the stores a workload performs, how many land where an
//                    agent other than the CPU can see them?
//   MEMVU_LOADVIS  : of the loads, how many MUST be served by the machine
//                    rather than from any local copy?
//   MEMVU_BLAME    : WHICH CODE generates that traffic?
//
// MEMVU_BLAME turns the other two around. They say how much a workload costs;
// this says where the cost comes from, attributed to the program counter of the
// instruction responsible. Point it at a system with source available and the
// output stops being a performance measurement and becomes a work list: these
// are the routines whose memory behaviour is expensive, ranked.
//
// Attribution is to the address of the OPCODE, captured at dispatch, not to the
// program counter at the moment of the access -- by then the fetch has already
// walked past the operand bytes and every multi-byte instruction would blame a
// point somewhere inside itself. Counts are kept per 256-byte granule across the
// 24-bit space: fine enough to land on a routine, coarse enough to stay a fixed
// 512 KB regardless of how much code runs.
//
// The question is not academic. Every memory-side design decision in this
// machine — shadowing, the fast/slow split, what a card can snoop, what any
// accelerator would have to route to the machine — turns on the ratio of
// private traffic to observable traffic, and that ratio is a property of the
// SOFTWARE, not of any hardware model. It is therefore measurable here, exactly,
// without hardware, which is the whole point of these rails.
//
// ---------------------------------------------------------------------------
// STORES (MEMVU_STOREVIS) — each store lands in exactly one class
//
//   DEVICE     $Cxxx in banks $00/$01/$E0/$E1 — soft switches and slot I/O.
//              Access itself is the side effect, so it is externally visible by
//              definition.
//   SLOWSIDE   A direct store into the Mega II banks $E0/$E1 (outside $Cxxx).
//              The Mega II side is read by agents that are not the CPU.
//   SHADOWED   A store the machine ITSELF mirrored to the Mega II side, per its
//              own $C035 shadow configuration. Counted at the machine's own
//              decision point (mmu_iigs.cpp), never re-derived here — a second
//              implementation of the shadow rules would be a second thing to be
//              wrong.
//   PRIVATE    Everything else: ordinary RAM with no non-CPU reader implied by
//              the machine's current configuration.
//
//   VISIBLE = DEVICE + SLOWSIDE + SHADOWED.  PRIVATE = TOTAL - VISIBLE.
//
// ---------------------------------------------------------------------------
// LOADS (MEMVU_LOADVIS) — a DIFFERENT question, deliberately not symmetric
//
// A store's visibility asks "who else can see this?". A load's asks "who has to
// serve this?", and those are not mirror images:
//
//   DEVICE     Reading a soft switch or slot register IS the side effect, so the
//              machine must serve it. **Unavoidable**, under any caching scheme.
//   SLOWSIDE   A read from the Mega II banks. The machine must serve it only if
//              the reader holds no coherent local copy. **Avoidable in
//              principle** — so it is reported separately and is NOT summed into
//              the unavoidable figure.
//   PRIVATE    Ordinary RAM.
//
// Shadowing has no load equivalent (it is a write-mirroring mechanism), so the
// load report carries no shadowed field rather than a zero.
//
// Loads are also split by the funnel that issued them, which corresponds to what
// the processor asserts on the bus for that cycle:
//
//   FETCH      program fetch — opcode and operand bytes (a valid-program-address
//              cycle). The instruction stream.
//   DATA       operand data read (a valid-data-address cycle).
//   VECTOR     a vector pull, via the MMU's own vector-read path.
//   PHANTOM    the internal/RMW cycle read.
//
// ---------------------------------------------------------------------------
// OBSERVATION MODEL — read this before quoting a number.
//
//   * The model is named in the output (`model=`), because "how much is visible"
//     is only meaningful relative to a stated definition of observer.
//   * PRIVATE means "no non-CPU reader is implied by the machine's configuration
//     as this emulator models it." It does NOT mean "provably unobservable."
//     A bus-mastering card can read any RAM; nothing here models that.
//   * On a platform without IIgs shadowing the SHADOWED class cannot exist. The
//     rail says so (`model=iie-basic-v1`, `shadowed=n/a`) rather than reporting a
//     confident zero, which would be a plausible-wrong meter.
//   * PHANTOM stores (the read-modify-write dummy write) are real bus cycles on
//     real silicon, so they are counted in TOTAL — and reported separately, so a
//     reader can subtract them if their question excludes them.
//
//     Expect near zero on a IIgs workload. That is CORRECT, not a dead tap. Only
//     an NMOS 6502 and a 65816 in EMULATION mode write during the modify cycle; a
//     65C02, and a 65816 in NATIVE mode, read instead — see the RMW modify-cycle
//     branch in base_6502.cpp. The count therefore tracks time spent in emulation
//     mode, and does so cleanly: ~83 for a IIgs sitting in ROM firmware, 7 across
//     a GS/OS 6.0.1 boot, 0 on the Finder desktop. A LARGE value on a native-mode
//     workload is the surprising result, not a small one.
//
// COVERAGE — taps every CPU access funnel in base_6502.cpp:
//   stores  bus_write (ordinary stores and stack pushes), phantom_write/_ign
//   loads   fetch_pc, bus_read, vp_read, phantom_read/_ign
// `read_byte`/`write_byte`/`write_word` are dead code (no call sites) and are
// deliberately untapped; **if any is revived it MUST be tapped here**, or these
// rails silently undercount. The debug/disassembly paths that call mmu->read
// directly are NOT taps and must never become them: they are the observer, not
// the machine.
//
// Header-only, env-gated, observe-don't-disturb: increments counters and touches
// no emulated state. Unarmed, the cost is one untaken branch per access.
//
// Generic naming only — this models a real Apple IIgs and carries no product- or
// board-specific terms.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <string>
#include "memvu_seam.hpp"   // MEMVU_SEAM feeds off these same taps rather than adding its own

// ---- arm bits + observation model -------------------------------------------
inline bool memvu_sv_on = false;          // MEMVU_STOREVIS
inline bool memvu_lv_on = false;          // MEMVU_LOADVIS
inline bool memvu_sv_have_shadow = false; // platform can shadow (IIgs MMU present)

// ---- shared visibility classifier -------------------------------------------
enum memvu_vis_t { MEMVU_PRIVATE = 0, MEMVU_DEVICE, MEMVU_SLOWSIDE };

// The one place the address rules live, so the store and load sides cannot drift
// apart. Shadowing is deliberately absent: it is a property of a WRITE and is
// counted where the machine decides it, not here.
inline memvu_vis_t memvu_classify(uint32_t addr) {
    const uint32_t bank   = (addr >> 16) & 0xFF;
    const uint32_t addr16 = addr & 0xFFFF;
    const bool io_bank = (bank == 0x00 || bank == 0x01 || bank == 0xE0 || bank == 0xE1);
    if (io_bank && addr16 >= 0xC000 && addr16 <= 0xCFFF) return MEMVU_DEVICE;
    if (bank == 0xE0 || bank == 0xE1)                    return MEMVU_SLOWSIDE;
    return MEMVU_PRIVATE;
}

// ---- BLAME state -------------------------------------------------------------
inline bool     memvu_blame_on = false;
inline uint32_t memvu_blame_pc = 0;            // opcode address of the current instruction
inline uint32_t *memvu_blame_w = nullptr;      // visible stores per 256-byte granule
inline uint32_t *memvu_blame_r = nullptr;      // routed reads per 256-byte granule
inline uint32_t  memvu_blame_w_top[5] = {}, memvu_blame_w_topg[5] = {};   // scratch for the STOREVIS top-writers line
// The toolbox call in progress at each visible store, per granule: the top three
// call words and the rest, so a writer that is a shared helper is named by the
// call that used it rather than by the entry point that happens to precede it.
struct MemvuBlameCall { uint16_t word[3]; uint32_t n[3]; uint32_t other; };
inline MemvuBlameCall *memvu_blame_call = nullptr;
// The call in progress, asked through a pointer main() sets when BLAME is armed:
// the tap is linked into binaries (iigsmmutest) that have no toolbox tracker.
inline uint16_t (*memvu_callctx_fn)() = nullptr;

static constexpr uint32_t MEMVU_BLAME_GRAN = 8;                 // log2(256)
static constexpr uint32_t MEMVU_BLAME_N    = 1u << (24 - MEMVU_BLAME_GRAN);   // 65536

inline bool memvu_blame_init() {
    if (!memvu_blame_w) memvu_blame_w = (uint32_t *)calloc(MEMVU_BLAME_N, sizeof(uint32_t));
    if (!memvu_blame_r) memvu_blame_r = (uint32_t *)calloc(MEMVU_BLAME_N, sizeof(uint32_t));
    if (!memvu_blame_call) memvu_blame_call = (MemvuBlameCall *)calloc(MEMVU_BLAME_N, sizeof(MemvuBlameCall));
    return memvu_blame_w && memvu_blame_r;
}
inline void memvu_blame_reset() {
    if (memvu_blame_w) memset(memvu_blame_w, 0, MEMVU_BLAME_N * sizeof(uint32_t));
    if (memvu_blame_r) memset(memvu_blame_r, 0, MEMVU_BLAME_N * sizeof(uint32_t));
    if (memvu_blame_call) memset(memvu_blame_call, 0, MEMVU_BLAME_N * sizeof(MemvuBlameCall));
}
// "; in CALL nn%" for a granule's dominant toolbox call, or "" when its stores
// happened outside any call (or nothing counted them).
inline const char *memvu_blame_call_suffix(uint32_t g, char *buf, size_t n) {
    buf[0] = 0;
    if (!memvu_blame_call) return buf;
    const MemvuBlameCall &c = memvu_blame_call[g];
    uint64_t total = c.other; int best = -1;
    for (int i = 0; i < 3; i++) { total += c.n[i]; if (c.n[i] && (best < 0 || c.n[i] > c.n[best])) best = i; }
    if (!total) return buf;
    const unsigned pct = best < 0 ? 0 : (unsigned)(100 * (uint64_t)c.n[best] / total);
    const unsigned outside = (unsigned)(100 * (uint64_t)c.other / total);
    // The dominant call when it is at least 1%; and when most of the traffic
    // happened outside any call -- an interrupt handler, a stray helper -- say
    // so, because that is the cause a reader would otherwise have to guess.
    size_t off = 0;
    if (pct >= 1) {
        int m = snprintf(buf, n, "; in %s %u%%", iigs_tool_func_name(c.word[best] & 0xFF, c.word[best] >> 8).c_str(), pct);
        if (m > 0) off = (size_t)m < n ? (size_t)m : n - 1;
    }
    if (outside >= 50 && off < n) snprintf(buf + off, n - off, "%s outside any call %u%%", off ? "," : ";", outside);
    return buf;
}

inline void memvu_blame_note(bool write) {
    uint32_t *t = write ? memvu_blame_w : memvu_blame_r;
    if (t) t[(memvu_blame_pc & 0xFFFFFF) >> MEMVU_BLAME_GRAN]++;
    if (write && memvu_blame_call) {
        MemvuBlameCall &c = memvu_blame_call[(memvu_blame_pc & 0xFFFFFF) >> MEMVU_BLAME_GRAN];
        const uint16_t w = memvu_callctx_fn ? memvu_callctx_fn() : 0;
        if (!w) { c.other++; return; }
        for (int i = 0; i < 3; i++) {
            if (c.word[i] == w) { c.n[i]++; return; }
            if (c.n[i] == 0) { c.word[i] = w; c.n[i] = 1; return; }
        }
        c.other++;
    }
}

// ---- store counters ----------------------------------------------------------
inline uint64_t memvu_sv_total    = 0;   // every CPU store, phantoms included
inline uint64_t memvu_sv_phantom  = 0;   // subset of total: RMW dummy writes
inline uint64_t memvu_sv_device   = 0;
inline uint64_t memvu_sv_slowside = 0;
inline uint64_t memvu_sv_shadowed = 0;   // machine mirrored it (its own decision)

inline uint64_t memvu_sv_bank_total[256]   = {};
inline uint64_t memvu_sv_bank_visible[256] = {};

// Per-register device stores: 4 I/O banks x 4096 addresses. "device=469605"
// told a reader a number and withheld the fact that would let them read it
// (it was one register, $C03D, the sound GLU data port, written by ROM). The
// summary now names its top registers, and the writers, beside the totals.
inline uint32_t memvu_sv_dev[4][4096] = {};
inline int memvu_io_bank_index(uint32_t bank) {
    return bank == 0x00 ? 0 : bank == 0x01 ? 1 : bank == 0xE0 ? 2 : 3;
}
inline const uint8_t memvu_io_bank_of[4] = { 0x00, 0x01, 0xE0, 0xE1 };

// ---- load counters -----------------------------------------------------------
enum memvu_kind_t { MEMVU_K_FETCH = 0, MEMVU_K_DATA, MEMVU_K_VECTOR, MEMVU_K_PHANTOM, MEMVU_K__N };
inline const char *memvu_kind_name[MEMVU_K__N] = { "fetch", "data", "vector", "phantom" };

inline uint64_t memvu_lv_kind_total[MEMVU_K__N]    = {};
inline uint64_t memvu_lv_kind_device[MEMVU_K__N]   = {};
inline uint64_t memvu_lv_kind_slowside[MEMVU_K__N] = {};

inline void memvu_reset() {
    memvu_sv_total = memvu_sv_phantom = 0;
    memvu_sv_device = memvu_sv_slowside = memvu_sv_shadowed = 0;
    for (int i = 0; i < 256; i++) { memvu_sv_bank_total[i] = 0; memvu_sv_bank_visible[i] = 0; }
    memset(memvu_sv_dev, 0, sizeof memvu_sv_dev);
    for (int k = 0; k < MEMVU_K__N; k++) {
        memvu_lv_kind_total[k] = memvu_lv_kind_device[k] = memvu_lv_kind_slowside[k] = 0;
    }
}
inline void memvu_sv_reset() { memvu_reset(); }   // name kept for the store-only caller

/* ---- live readback (CTRL `memvu` verb) --------------------------------------
 *
 * These counters were previously readable only as a summary line printed at
 * spike end, which forces a whole measurement to be one process lifetime: to
 * measure a phase you relaunched from a snapshot and took the run's total.
 *
 * Read live, plus `memvu reset`, they answer a different and better question --
 * run to a steady state, zero, run a bounded window, read. That is the
 * difference between measuring "a boot that happened to end in the Finder" and
 * measuring the Finder.
 *
 * Every formatter REFUSES when its rail is unarmed rather than reporting zeros.
 * An unarmed counter is not a measurement of nothing; it is the absence of a
 * measurement, and the two must not look alike (AGENTS.md rule 3).
 */
inline void memvu_format_status(char *buf, size_t bufsz) {
    /* memvu_sv_have_shadow is only set when a rail arms, so reporting it as
     * "none" while unarmed would tell a caller on a IIgs that the machine
     * cannot shadow -- false, and indistinguishable from the true answer on a
     * IIe. Unarmed, the honest value is "not yet determined". */
    const char *shadow = !(memvu_sv_on || memvu_lv_on)
                             ? "unknown(no rail armed; determined at arm time)"
                             : (memvu_sv_have_shadow
                                    ? "iigs-mmu"
                                    : "none(non-shadowing platform)");
    snprintf(buf, bufsz,
             "status=OK storevis=%s loadvis=%s blame=%s shadow_model=%s "
             "stores=%llu loads=%llu | memvu store|load|reset",
             memvu_sv_on ? "armed" : "off",
             memvu_lv_on ? "armed" : "off",
             memvu_blame_on ? "armed" : "off",
             shadow,
             (unsigned long long)memvu_sv_total,
             (unsigned long long)(memvu_lv_kind_total[MEMVU_K_FETCH] +
                                  memvu_lv_kind_total[MEMVU_K_DATA] +
                                  memvu_lv_kind_total[MEMVU_K_VECTOR] +
                                  memvu_lv_kind_total[MEMVU_K_PHANTOM]));
}

inline void memvu_format_store(char *buf, size_t bufsz) {
    if (!memvu_sv_on) {
        snprintf(buf, bufsz,
                 "status=FAIL memvu-storevis-not-armed -- set MEMVU_STOREVIS "
                 "before launch. Unarmed counters read zero, which is not a "
                 "measurement of zero visible stores");
        return;
    }
    const unsigned long long vis = (unsigned long long)
        (memvu_sv_device + memvu_sv_slowside + memvu_sv_shadowed);
    const unsigned long long tot = (unsigned long long)memvu_sv_total;
    snprintf(buf, bufsz,
             "status=OK total=%llu phantom=%llu device=%llu slowside=%llu "
             "shadowed=%llu visible=%llu visible_pct=%.2f shadow_model=%s",
             tot, (unsigned long long)memvu_sv_phantom,
             (unsigned long long)memvu_sv_device,
             (unsigned long long)memvu_sv_slowside,
             (unsigned long long)memvu_sv_shadowed, vis,
             tot ? (100.0 * (double)vis / (double)tot) : 0.0,
             memvu_sv_have_shadow ? "iigs-mmu" : "none");
}

inline void memvu_format_load(char *buf, size_t bufsz) {
    if (!memvu_lv_on) {
        snprintf(buf, bufsz,
                 "status=FAIL memvu-loadvis-not-armed -- set MEMVU_LOADVIS "
                 "before launch");
        return;
    }
    size_t off = 0;
    int m = snprintf(buf, bufsz, "status=OK");
    if (m > 0) off = (size_t)m;
    for (int k = 0; k < MEMVU_K__N && off + 32 < bufsz; k++) {
        m = snprintf(buf + off, bufsz - off, " %s=%llu/dev=%llu/slow=%llu",
                     memvu_kind_name[k],
                     (unsigned long long)memvu_lv_kind_total[k],
                     (unsigned long long)memvu_lv_kind_device[k],
                     (unsigned long long)memvu_lv_kind_slowside[k]);
        if (m < 0) break;
        off += (size_t)m;
    }
}

// ---- the CPU-side store tap --------------------------------------------------
inline void memvu_seam_store(uint32_t addr);   // defined below; same classifier

inline void memvu_sv_note_store(uint32_t addr, bool phantom) {
    if (memvu_seam_on) memvu_seam_store(addr);
    const uint32_t bank = (addr >> 16) & 0xFF;
    memvu_sv_total++;
    memvu_sv_bank_total[bank]++;
    if (phantom) memvu_sv_phantom++;

    switch (memvu_classify(addr)) {
        case MEMVU_DEVICE:
            memvu_sv_device++;   memvu_sv_bank_visible[bank]++;
            memvu_sv_dev[memvu_io_bank_index(bank)][addr & 0xFFF]++;
            if (memvu_blame_on) memvu_blame_note(true);
            break;
        case MEMVU_SLOWSIDE:
            memvu_sv_slowside++; memvu_sv_bank_visible[bank]++;
            if (memvu_blame_on) memvu_blame_note(true);
            break;
        default: break;   // PRIVATE unless the machine shadows it, counted below
    }
}

// The seam model consumes the same classification rather than re-deriving it, so
// the model and the counters can never disagree about what an address is.
inline void memvu_seam_store(uint32_t addr) {
    switch (memvu_classify(addr)) {
        case MEMVU_DEVICE:   memvu_seam_write(true);  break;   // I/O: slow tier
        case MEMVU_SLOWSIDE: memvu_seam_write(true);  break;   // Mega II: slow tier
        default:             memvu_seam_local_access(); break;
    }
}
inline void memvu_seam_load(uint32_t addr) {
    switch (memvu_classify(addr)) {
        case MEMVU_DEVICE:   memvu_seam_read(true); break;     // must be served
        case MEMVU_SLOWSIDE:
            if (memvu_seam_shadowreads_local) memvu_seam_local_access();
            else                              memvu_seam_read(true);
            break;
        default: memvu_seam_local_access(); break;
    }
}

// ---- the machine-side shadow tap ---------------------------------------------
inline void memvu_sv_note_shadowed(uint32_t addr) {
    if (!memvu_sv_on) return;
    if (memvu_blame_on) memvu_blame_note(true);
    memvu_sv_shadowed++;
    memvu_sv_bank_visible[(addr >> 16) & 0xFF]++;
}

// ---- the CPU-side load tap ---------------------------------------------------
inline void memvu_seam_load(uint32_t addr);    // defined below; same classifier

inline void memvu_lv_note_load(uint32_t addr, memvu_kind_t kind) {
    if (memvu_seam_on) memvu_seam_load(addr);
    memvu_lv_kind_total[kind]++;
    switch (memvu_classify(addr)) {
        case MEMVU_DEVICE:
            memvu_lv_kind_device[kind]++;
            if (memvu_blame_on) memvu_blame_note(false);
            break;
        case MEMVU_SLOWSIDE:
            memvu_lv_kind_slowside[kind]++;
            if (memvu_blame_on) memvu_blame_note(false);
            break;
        default: break;
    }
}

// ---- reports -----------------------------------------------------------------
inline void memvu_sv_report(FILE *f) {
    iigs_syms_flush_note();   // never inside this report's lines
    if (!memvu_sv_on) return;

    const uint64_t shadowed = memvu_sv_have_shadow ? memvu_sv_shadowed : 0;
    const uint64_t visible  = memvu_sv_device + memvu_sv_slowside + shadowed;
    const uint64_t total    = memvu_sv_total;

    if (total == 0) {
        fprintf(f, "MEMVU STOREVIS: stores=0 -- no stores observed; nothing to report\n");
        return;
    }
    if (visible > total) {
        fprintf(f, "MEMVU STOREVIS: ** INCONSISTENT ** visible=%llu > stores=%llu "
                   "(tap coverage bug -- do not use these numbers)\n",
                (unsigned long long)visible, (unsigned long long)total);
        return;
    }

    // The shadowed field is a count on a platform that can shadow and the literal
    // "n/a" on one that cannot. It is rendered separately because the alternative
    // — printing 0 — is the exact plausible-wrong meter this rail must not be.
    char shadow_field[24];
    if (memvu_sv_have_shadow)
        snprintf(shadow_field, sizeof shadow_field, "%llu", (unsigned long long)shadowed);
    else
        snprintf(shadow_field, sizeof shadow_field, "n/a");

    fprintf(f, "MEMVU STOREVIS: model=%s stores=%llu visible=%llu (%.2f%%) "
               "private=%llu device=%llu slowside=%llu shadowed=%s phantom=%llu\n",
            memvu_sv_have_shadow ? "iigs-shadow-v1" : "iie-basic-v1",
            (unsigned long long)total,
            (unsigned long long)visible,
            100.0 * (double)visible / (double)total,
            (unsigned long long)(total - visible),
            (unsigned long long)memvu_sv_device,
            (unsigned long long)memvu_sv_slowside,
            shadow_field,
            (unsigned long long)memvu_sv_phantom);

    // Attribution rides with the total. Top device registers, named; top writer
    // granules from the blame table, which STOREVIS arms for exactly this.
    if (memvu_sv_device) {
        fprintf(f, "MEMVU STOREVIS device top:");
        for (int rank = 0; rank < 5; rank++) {
            int bb = -1, ba = 0; uint32_t bc = 0;
            for (int b = 0; b < 4; b++)
                for (int a = 0; a < 4096; a++)
                    if (memvu_sv_dev[b][a] > bc) { bc = memvu_sv_dev[b][a]; bb = b; ba = a; }
            if (bb < 0) break;
            const uint16_t a16 = (uint16_t)(0xC000 | ba);
            fprintf(f, " $%02X:%04X=%u (%s)", memvu_io_bank_of[bb], a16, bc, iigs_io_reg_name_or(a16));
            memvu_sv_dev[bb][ba] = 0;   // consumed; the report is a one-shot at spike end
        }
        fprintf(f, "\n");
    }
    if (memvu_blame_w) {
        fprintf(f, "MEMVU STOREVIS writers top:");
        for (int rank = 0; rank < 5; rank++) {
            uint32_t bg = 0, bc = 0;
            for (uint32_t g = 0; g < MEMVU_BLAME_N; g++)
                if (memvu_blame_w[g] > bc) { bc = memvu_blame_w[g]; bg = g; }
            if (!bc) break;
            const uint32_t base = bg << MEMVU_BLAME_GRAN;
            char sym[96]; sym[0] = 0;
            const char *name = a2gspu_name_pc(base, sym, sizeof sym);
            char ic[160]; memvu_blame_call_suffix(bg, ic, sizeof ic);
            fprintf(f, " $%02X/%04X=%u (%s%s)", (base >> 16) & 0xFF, base & 0xFFFF, bc,
                    (name && name[0]) ? name : "unnamed", ic);
            memvu_blame_w_top[rank] = bc;   // keep BLAME's own report intact: restore below
            memvu_blame_w_topg[rank] = bg;
            memvu_blame_w[bg] = 0;
        }
        for (int rank = 0; rank < 5; rank++) if (memvu_blame_w_top[rank]) memvu_blame_w[memvu_blame_w_topg[rank]] = memvu_blame_w_top[rank];
        fprintf(f, "\n");
    }
}

inline void memvu_sv_report_banks(FILE *f) {
    if (!memvu_sv_on || memvu_sv_total == 0) return;
    for (int b = 0; b < 256; b++) {
        if (!memvu_sv_bank_total[b]) continue;
        fprintf(f, "MEMVU STOREVIS BANK %02X: stores=%llu visible=%llu\n",
                b, (unsigned long long)memvu_sv_bank_total[b],
                (unsigned long long)memvu_sv_bank_visible[b]);
    }
}

// Top offenders. Sorted by total cost; the split says what KIND of cost, because
// a routine that writes the display and one that polls a device need different
// fixes.
inline void memvu_blame_report(FILE *f, int topn) {
    if (!memvu_blame_on || !memvu_blame_w) return;
    struct Row { uint32_t g, w, r; };
    Row *rows = (Row *)malloc(sizeof(Row) * 4096);
    if (!rows) return;
    int nr = 0;
    for (uint32_t g = 0; g < MEMVU_BLAME_N && nr < 4096; g++) {
        if (memvu_blame_w[g] || memvu_blame_r[g])
            rows[nr++] = Row{g, memvu_blame_w[g], memvu_blame_r[g]};
    }
    // partial selection sort: topn is small, the array is not worth qsort here
    for (int i = 0; i < topn && i < nr; i++) {
        int best = i;
        for (int j = i + 1; j < nr; j++)
            if ((uint64_t)rows[j].w + rows[j].r > (uint64_t)rows[best].w + rows[best].r) best = j;
        Row t = rows[i]; rows[i] = rows[best]; rows[best] = t;
    }
    fprintf(f, "MEMVU BLAME: %d code granules generated externally-visible traffic; top %d\n",
            nr, topn < nr ? topn : nr);
    for (int i = 0; i < topn && i < nr; i++) {
        const uint32_t base = rows[i].g << MEMVU_BLAME_GRAN;
        char sym[96]; sym[0] = 0;
        const char *name = a2gspu_name_pc(base, sym, sizeof sym);
        char ic[160]; memvu_blame_call_suffix(rows[i].g, ic, sizeof ic);
        fprintf(f, "MEMVU BLAME: $%02X/%04X  visible_writes=%u  routed_reads=%u  (%s%s)\n",
                (base >> 16) & 0xFF, base & 0xFFFF, rows[i].w, rows[i].r,
                (name && name[0]) ? name : "unnamed", ic);
    }
    free(rows);
}

inline void memvu_lv_report(FILE *f) {
    if (!memvu_lv_on) return;

    uint64_t total = 0, dev = 0, slow = 0;
    for (int k = 0; k < MEMVU_K__N; k++) {
        total += memvu_lv_kind_total[k];
        dev   += memvu_lv_kind_device[k];
        slow  += memvu_lv_kind_slowside[k];
    }
    if (total == 0) {
        fprintf(f, "MEMVU LOADVIS: loads=0 -- no loads observed; nothing to report\n");
        return;
    }

    // device is the unavoidable figure; slowside is avoidable given a coherent
    // local copy. Summing them would assert a caching policy this rail does not
    // model, so the line reports them apart and says which is which.
    fprintf(f, "MEMVU LOADVIS: model=iigs-read-v1 loads=%llu mustserve=%llu (%.2f%%) "
               "cacheable_slow=%llu (%.2f%%) private=%llu\n",
            (unsigned long long)total,
            (unsigned long long)dev,  100.0 * (double)dev  / (double)total,
            (unsigned long long)slow, 100.0 * (double)slow / (double)total,
            (unsigned long long)(total - dev - slow));

    fprintf(f, "MEMVU LOADVIS KIND:");
    for (int k = 0; k < MEMVU_K__N; k++) {
        fprintf(f, " %s=%llu/%llu/%llu", memvu_kind_name[k],
                (unsigned long long)memvu_lv_kind_total[k],
                (unsigned long long)memvu_lv_kind_device[k],
                (unsigned long long)memvu_lv_kind_slowside[k]);
    }
    fprintf(f, "   (total/mustserve/cacheable_slow)\n");
}

// The accounting's readback, supplied with the rails (M8): `rail read MEMVU_STOREVIS`.
inline a2rails::Registrar g_reader_storevis{"MEMVU_STOREVIS", memvu_format_status};
inline a2rails::Registrar g_reader_loadvis {"MEMVU_LOADVIS",  memvu_format_status};
