#pragma once
// ============================================================================
// memvu_storevis.hpp — memory access-visibility accounting
//   MEMVU_STOREVIS : of the stores a workload performs, how many land where an
//                    agent other than the CPU can see them?
//   MEMVU_LOADVIS  : of the loads, how many MUST be served by the machine
//                    rather than from any local copy?
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

// ---- store counters ----------------------------------------------------------
inline uint64_t memvu_sv_total    = 0;   // every CPU store, phantoms included
inline uint64_t memvu_sv_phantom  = 0;   // subset of total: RMW dummy writes
inline uint64_t memvu_sv_device   = 0;
inline uint64_t memvu_sv_slowside = 0;
inline uint64_t memvu_sv_shadowed = 0;   // machine mirrored it (its own decision)

inline uint64_t memvu_sv_bank_total[256]   = {};
inline uint64_t memvu_sv_bank_visible[256] = {};

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
    for (int k = 0; k < MEMVU_K__N; k++) {
        memvu_lv_kind_total[k] = memvu_lv_kind_device[k] = memvu_lv_kind_slowside[k] = 0;
    }
}
inline void memvu_sv_reset() { memvu_reset(); }   // name kept for the store-only caller

// ---- the CPU-side store tap --------------------------------------------------
inline void memvu_sv_note_store(uint32_t addr, bool phantom) {
    const uint32_t bank = (addr >> 16) & 0xFF;
    memvu_sv_total++;
    memvu_sv_bank_total[bank]++;
    if (phantom) memvu_sv_phantom++;

    switch (memvu_classify(addr)) {
        case MEMVU_DEVICE:   memvu_sv_device++;   memvu_sv_bank_visible[bank]++; break;
        case MEMVU_SLOWSIDE: memvu_sv_slowside++; memvu_sv_bank_visible[bank]++; break;
        default: break;   // PRIVATE unless the machine shadows it, counted below
    }
}

// ---- the machine-side shadow tap ---------------------------------------------
inline void memvu_sv_note_shadowed(uint32_t addr) {
    if (!memvu_sv_on) return;
    memvu_sv_shadowed++;
    memvu_sv_bank_visible[(addr >> 16) & 0xFF]++;
}

// ---- the CPU-side load tap ---------------------------------------------------
inline void memvu_lv_note_load(uint32_t addr, memvu_kind_t kind) {
    memvu_lv_kind_total[kind]++;
    switch (memvu_classify(addr)) {
        case MEMVU_DEVICE:   memvu_lv_kind_device[kind]++;   break;
        case MEMVU_SLOWSIDE: memvu_lv_kind_slowside[kind]++; break;
        default: break;
    }
}

// ---- reports -----------------------------------------------------------------
inline void memvu_sv_report(FILE *f) {
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
