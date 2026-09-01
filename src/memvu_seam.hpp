#pragma once
// ============================================================================
// memvu_seam.hpp — MEMVU_SEAM: what would a workload cost a processor that has to
// route some of its memory traffic to this machine?
//
// The other memvu rails COUNT things. This one MODELS one, and that difference is
// the whole reason for the warning block it prints. A counter can be wrong by
// being miscoded; a model can be wrong by being believed.
//
// ---------------------------------------------------------------------------
// WHAT IT MODELS
//
// A processor running at some multiple of the host's rate, whose memory accesses
// are each either satisfied locally or routed to the machine:
//
//   local access      1 internal cycle
//   routed read       stalls for the host cycle -- a read cannot be deferred
//   routed write      enters a posted buffer and does NOT stall, unless the
//                     buffer is full, in which case it stalls until a slot frees
//
// The buffer drains one entry per host-cycle period, concurrently with local
// execution. Reference (machine) cycles are taken from the emulator's own clock,
// so the speedup figure is against what this machine actually does, not against
// an idealized one.
//
// ---------------------------------------------------------------------------
// WHAT IT DOES NOT MODEL -- every omission is OPTIMISTIC, so the result is an
// UPPER BOUND on speedup and must never be quoted as a prediction.
//
//   * No pipeline. Every local access costs exactly one internal cycle; a real
//     core has hazards, mispredictions and multi-cycle operations.
//   * No local memory hierarchy. Local means free; there are no local cache
//     misses, no fill latency, no bandwidth limit behind the core.
//   * No coherence traffic. Invalidations, snoop stalls and shadow maintenance
//     are all absent.
//   * No interrupts, and no timing-debt settlement. A design that must pace
//     itself to the machine at any point will be slower than this says.
//   * No read/write ordering constraint. A routed read is not made to wait for
//     the write buffer to drain, which a conservative implementation would.
//   * Instruction fetch is charged like any other access, which approximates a
//     decoded-instruction cache that is doing its job perfectly.
//
// ---------------------------------------------------------------------------
// PARAMETERS -- `MEMVU_SEAM=<ratio>,<slowmul>,<bufdepth>,<shadowreads>`
//
//   ratio        internal cycles per fast-tier host cycle (e.g. 36 for a core
//                running ~36x the host's fast rate)
//   slowmul      how many fast-tier periods a slow-tier cycle occupies
//   bufdepth     posted-write buffer entries
//   shadowreads  1 = reads of the machine's slow side are served from a coherent
//                local copy; 0 = they are routed. This is the single assumption
//                the design cannot yet make, so it is a parameter and not a
//                default -- run it both ways.
//
// There is no default. A model whose parameters are implicit invites its output
// to be quoted as a property of the software, and it is not: it is a property of
// the software AND the parameters, jointly.
//
// Header-only, env-gated, observe-don't-disturb. Generic naming only.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>

inline bool memvu_seam_on = false;

// ---- parameters (no defaults; set only by a successful parse) ---------------
inline uint32_t memvu_seam_ratio    = 0;
inline uint32_t memvu_seam_slowmul  = 0;
inline uint32_t memvu_seam_bufdepth = 0;
inline bool     memvu_seam_shadowreads_local = false;

// ---- modeled state, all in internal cycles ---------------------------------
inline uint64_t memvu_seam_time       = 0;   // modeled internal time
inline uint64_t memvu_seam_stall_read = 0;
inline uint64_t memvu_seam_stall_buf  = 0;
inline uint64_t memvu_seam_local      = 0;
inline uint64_t memvu_seam_host_r     = 0;
inline uint64_t memvu_seam_host_w     = 0;
inline uint64_t memvu_seam_vcycles    = 0;   // reference cycles, from the emulator clock
inline uint64_t memvu_seam_bufhigh    = 0;   // high-water mark of the write buffer

// posted-write buffer: entries pending, and when the oldest completes draining
inline uint64_t memvu_seam_pending    = 0;
inline uint64_t memvu_seam_drain_at   = 0;

inline bool memvu_seam_init(const char *spec) {
    unsigned r = 0, sm = 0, bd = 0, sr = 2;
    if (!spec || sscanf(spec, "%u,%u,%u,%u", &r, &sm, &bd, &sr) != 4) return false;
    if (r == 0 || sm == 0 || sr > 1) return false;
    memvu_seam_ratio = r; memvu_seam_slowmul = sm; memvu_seam_bufdepth = bd;
    memvu_seam_shadowreads_local = (sr == 1);
    return true;
}

inline void memvu_seam_reset() {
    memvu_seam_time = memvu_seam_stall_read = memvu_seam_stall_buf = 0;
    memvu_seam_local = memvu_seam_host_r = memvu_seam_host_w = 0;
    memvu_seam_vcycles = memvu_seam_bufhigh = 0;
    memvu_seam_pending = memvu_seam_drain_at = 0;
}

// Drain whatever the passage of time has allowed.
inline void memvu_seam_drain(uint64_t period) {
    while (memvu_seam_pending && memvu_seam_drain_at <= memvu_seam_time) {
        memvu_seam_pending--;
        if (memvu_seam_pending) memvu_seam_drain_at += period;
    }
}

// A routed read: the core cannot proceed without the data.
inline void memvu_seam_read(bool slow_tier) {
    const uint64_t period = (uint64_t)memvu_seam_ratio * (slow_tier ? memvu_seam_slowmul : 1);
    memvu_seam_drain(period);
    memvu_seam_time += period;
    memvu_seam_stall_read += period;
    memvu_seam_host_r++;
}

// A routed write: posted, and free unless the buffer is full.
inline void memvu_seam_write(bool slow_tier) {
    const uint64_t period = (uint64_t)memvu_seam_ratio * (slow_tier ? memvu_seam_slowmul : 1);
    memvu_seam_drain(period);
    if (memvu_seam_bufdepth == 0 || memvu_seam_pending >= memvu_seam_bufdepth) {
        // stall until a slot frees; with depth 0 this degenerates to write-through
        const uint64_t until = memvu_seam_pending ? memvu_seam_drain_at : memvu_seam_time + period;
        if (until > memvu_seam_time) {
            memvu_seam_stall_buf += until - memvu_seam_time;
            memvu_seam_time = until;
        }
        memvu_seam_drain(period);
    }
    if (!memvu_seam_pending) memvu_seam_drain_at = memvu_seam_time + period;
    memvu_seam_pending++;
    if (memvu_seam_pending > memvu_seam_bufhigh) memvu_seam_bufhigh = memvu_seam_pending;
    memvu_seam_host_w++;
}

inline void memvu_seam_local_access() {
    memvu_seam_time += 1;
    memvu_seam_local++;
}

// Reference cycles, sampled from the emulator's own clock at each instruction.
inline void memvu_seam_note_vcycles(uint64_t delta) { memvu_seam_vcycles += delta; }

inline void memvu_seam_report(FILE *f) {
    if (!memvu_seam_on) return;
    if (memvu_seam_time == 0) {
        fprintf(f, "MEMVU SEAM: no accesses modeled; nothing to report\n");
        return;
    }
    const double t   = (double)memvu_seam_time;
    const double hostequiv = t / (double)memvu_seam_ratio;   // in fast-tier host periods
    const double sp  = memvu_seam_vcycles ? (double)memvu_seam_vcycles / hostequiv : 0.0;

    fprintf(f, "MEMVU SEAM: model=v1 ratio=%u slowmul=%u bufdepth=%u shadowreads=%s\n",
            memvu_seam_ratio, memvu_seam_slowmul, memvu_seam_bufdepth,
            memvu_seam_shadowreads_local ? "local" : "routed");
    fprintf(f, "MEMVU SEAM: local=%llu routed_r=%llu routed_w=%llu bufhigh=%llu\n",
            (unsigned long long)memvu_seam_local,
            (unsigned long long)memvu_seam_host_r,
            (unsigned long long)memvu_seam_host_w,
            (unsigned long long)memvu_seam_bufhigh);
    fprintf(f, "MEMVU SEAM: internal=%llu stall_read=%llu (%.1f%%) stall_wbuf=%llu (%.1f%%)\n",
            (unsigned long long)memvu_seam_time,
            (unsigned long long)memvu_seam_stall_read, 100.0 * memvu_seam_stall_read / t,
            (unsigned long long)memvu_seam_stall_buf,  100.0 * memvu_seam_stall_buf  / t);
    fprintf(f, "MEMVU SEAM: refcycles=%llu  ** UPPER-BOUND speedup=%.2fx **  "
               "(no pipeline, no local misses, no coherence, no interrupts, no pacing)\n",
            (unsigned long long)memvu_seam_vcycles, sp);
}
