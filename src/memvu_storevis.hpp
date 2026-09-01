#pragma once
// ============================================================================
// memvu_storevis.hpp — store-visibility accounting (MEMVU_STOREVIS)
//
// Answers ONE question about a workload: of the stores a program performs, how
// many land somewhere an agent OTHER than the CPU can observe?
//
// The question is not academic. Every memory-side design decision in this
// machine — shadowing, the fast/slow split, what a card can snoop, what an
// accelerator would have to make externally visible — turns on the ratio of
// private stores to observable ones, and that ratio is a property of the
// SOFTWARE, not of any hardware model. It is therefore measurable here, exactly,
// without hardware, which is the whole point of this rail.
//
// CLASSES (each store lands in exactly one)
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
// COVERAGE: taps every CPU store funnel in base_6502.cpp — `bus_write` (which
// carries ordinary stores and stack pushes) and `phantom_write` /
// `phantom_write_ign` (RMW dummy writes). `write_word` is dead code (no call
// sites) and is deliberately not tapped; if it is ever revived it MUST be tapped
// here, or this rail silently undercounts 16-bit stores.
//
// Header-only, env-gated, observe-don't-disturb: it increments counters and
// touches no emulated state. With MEMVU_STOREVIS unset the cost is one untaken
// branch per store.
//
// Generic naming only — this models a real Apple IIgs and carries no product- or
// board-specific terms.
// ============================================================================
#include <cstdint>
#include <cstdio>

// ---- arm bit + observation model -------------------------------------------
inline bool memvu_sv_on = false;          // MEMVU_STOREVIS
inline bool memvu_sv_have_shadow = false; // platform can shadow (IIgs MMU present)

// ---- counters ---------------------------------------------------------------
inline uint64_t memvu_sv_total    = 0;   // every CPU store, phantoms included
inline uint64_t memvu_sv_phantom  = 0;   // subset of total: RMW dummy writes
inline uint64_t memvu_sv_device   = 0;   // $Cxxx in banks 00/01/E0/E1
inline uint64_t memvu_sv_slowside = 0;   // direct Mega II bank E0/E1 store
inline uint64_t memvu_sv_shadowed = 0;   // machine mirrored it (its own decision)

// Per-bank totals, so a reader can see WHERE the traffic is rather than only how
// much of it is visible. 256 banks x 8 bytes = 2 KB, allocated whether armed or
// not; that is cheaper than the branch to allocate it lazily.
inline uint64_t memvu_sv_bank_total[256]   = {};
inline uint64_t memvu_sv_bank_visible[256] = {};

inline void memvu_sv_reset() {
    memvu_sv_total = memvu_sv_phantom = 0;
    memvu_sv_device = memvu_sv_slowside = memvu_sv_shadowed = 0;
    for (int i = 0; i < 256; i++) { memvu_sv_bank_total[i] = 0; memvu_sv_bank_visible[i] = 0; }
}

// ---- the CPU-side tap -------------------------------------------------------
// Called from every store funnel. `phantom` marks an RMW dummy write.
inline void memvu_sv_note_store(uint32_t addr, bool phantom) {
    uint32_t bank   = (addr >> 16) & 0xFF;
    uint32_t addr16 = addr & 0xFFFF;

    memvu_sv_total++;
    memvu_sv_bank_total[bank]++;
    if (phantom) memvu_sv_phantom++;

    // $Cxxx is I/O in the four banks that carry the Apple II memory image.
    const bool io_bank = (bank == 0x00 || bank == 0x01 || bank == 0xE0 || bank == 0xE1);
    if (io_bank && addr16 >= 0xC000 && addr16 <= 0xCFFF) {
        memvu_sv_device++;
        memvu_sv_bank_visible[bank]++;
        return;
    }
    if (bank == 0xE0 || bank == 0xE1) {
        memvu_sv_slowside++;
        memvu_sv_bank_visible[bank]++;
        return;
    }
    // Otherwise PRIVATE unless the machine shadows it — which is counted at the
    // machine's own decision point, not guessed at here.
}

// ---- the machine-side tap ---------------------------------------------------
// Called from the MMU where the machine decides to mirror a store to the Mega II
// side. Taking the count from the decision itself is what keeps this rail from
// owning a second copy of the shadow rules.
inline void memvu_sv_note_shadowed(uint32_t addr) {
    if (!memvu_sv_on) return;
    memvu_sv_shadowed++;
    memvu_sv_bank_visible[(addr >> 16) & 0xFF]++;
}

// ---- report -----------------------------------------------------------------
// One machine-readable line, plus the per-bank rows for banks that saw traffic.
inline void memvu_sv_report(FILE *f) {
    if (!memvu_sv_on) return;

    const uint64_t shadowed = memvu_sv_have_shadow ? memvu_sv_shadowed : 0;
    const uint64_t visible  = memvu_sv_device + memvu_sv_slowside + shadowed;
    const uint64_t total    = memvu_sv_total;

    // Guard the ratio rather than printing a NaN or a confident 0.00% on no data.
    if (total == 0) {
        fprintf(f, "MEMVU STOREVIS: stores=0 -- no stores observed; nothing to report\n");
        return;
    }
    // Shadowed writes are a subset of stores already counted in total (they are
    // bank 00/01 stores the machine additionally mirrored), so visible can never
    // exceed total. If it does, the taps disagree and the meter is lying: say so.
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

    const double pct = 100.0 * (double)visible / (double)total;
    fprintf(f, "MEMVU STOREVIS: model=%s stores=%llu visible=%llu (%.2f%%) "
               "private=%llu device=%llu slowside=%llu shadowed=%s phantom=%llu\n",
            memvu_sv_have_shadow ? "iigs-shadow-v1" : "iie-basic-v1",
            (unsigned long long)total,
            (unsigned long long)visible,
            pct,
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
