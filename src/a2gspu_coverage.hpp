// a2gspu_coverage.hpp -- execution-coverage bitmap (A2GSPU_COVERAGE)
//
// WHY THIS EXISTS
// ---------------
// The hardest problem in disassembling a 40-year-old binary is the code/data
// split.  A static disassembler must GUESS: it follows flow, applies heuristics,
// and where it cannot decide it either invents an instruction inside a data table
// or gives up and calls real code "data".  Both failures are silent, and both
// poison everything downstream -- a wrong split produces a confidently wrong
// architecture document.
//
// The emulator already knows the answer.  Whatever the CPU executed is code, by
// definition.  This records exactly that: one bit per byte, set when the byte was
// part of an executed instruction.  Fed back into deasmiigs (--coverage), it
// turns the central heuristic into a MEASUREMENT.
//
// Coverage is a lower bound, never an upper one: a byte that never executed in a
// given run is "not proven code", not "proven data".  Consumers must treat it as
// positive evidence only.  Union several runs (different game paths) to widen it.
//
// Design notes:
//  * Marks the FULL instruction extent, not just the opcode byte.  The length is
//    recovered from the next instruction's PC: a forward delta of 1..4 bytes is
//    the instruction that just retired, so [prev_pc, pc) is marked.  A branch or
//    jump breaks contiguity, so those mark the opcode byte alone and the extent
//    is picked up when that instruction is reached in sequential flow.
//  * Hooked OUTSIDE `if constexpr (CPUTraits::has_65816_ops)` in base_6502.cpp,
//    so it works on the 6502/65C02 (Apple II/IIe) path.  Most of the existing
//    per-instruction rails sit INSIDE that block and are therefore 65816-only.
//  * Off by default: one predictable untaken branch per instruction when unset.

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ---- state -------------------------------------------------------------------
// C++17 inline variables (the idiom already used by iigs_toolbox.hpp's g_* rail
// state).  These MUST NOT be `extern` with the definition in gs2.cpp: the CPU
// core (libgs2_cpu_new) references them, so every target that links the CPU
// library without gs2.cpp -- cycletest, cycletest816, cputest, mmutest -- would
// fail to link with "undefined reference to g_cov_*".
inline bool      g_cov_on      = false;
inline uint32_t  g_cov_lo      = 0;
inline uint32_t  g_cov_hi      = 0;
inline uint8_t  *g_cov_bits    = nullptr;
inline size_t    g_cov_nbytes  = 0;
inline uint32_t  g_cov_prev_pc = 0xFFFFFFFFu;
inline uint64_t  g_cov_marked  = 0;   // distinct bytes marked (stats only)

static inline void a2gspu_cov_mark(uint32_t a) {
    if (a < g_cov_lo || a > g_cov_hi) return;
    uint32_t i = a - g_cov_lo;
    uint8_t m = (uint8_t)(1u << (i & 7));
    uint8_t *p = &g_cov_bits[i >> 3];
    if (!(*p & m)) { *p |= m; g_cov_marked++; }
}

// Called once per instruction, with the PC of the instruction ABOUT to execute.
static inline void a2gspu_cov_step(uint32_t pc) {
    if (g_cov_prev_pc != 0xFFFFFFFFu) {
        uint32_t d = pc - g_cov_prev_pc;          // unsigned: backward jump wraps huge
        if (d >= 1 && d <= 4) {
            for (uint32_t k = 0; k < d; k++) a2gspu_cov_mark(g_cov_prev_pc + k);
        } else {
            a2gspu_cov_mark(g_cov_prev_pc);       // control transfer: opcode byte only
        }
    }
    g_cov_prev_pc = pc;
}

// A2GSPU_COVERAGE="LO-HI" or "BANK:LO-HI" (hex).  Returns true if enabled.
static inline bool a2gspu_cov_init(const char *spec) {
    if (!spec || !spec[0]) return false;
    const char *p = strchr(spec, ':');
    uint32_t bank = 0;
    bool banked = false;
    if (p) { bank = (uint32_t)strtoul(spec, nullptr, 16) & 0xFF; p++; banked = true; }
    else p = spec;
    const char *dash = strchr(p, '-');
    if (!dash) {
        fprintf(stderr, "A2GSPU_COVERAGE: bad range '%s' (want LO-HI or BANK:LO-HI)\n", spec);
        return false;
    }
    uint32_t lo = (uint32_t)strtoul(p, nullptr, 16);
    uint32_t hi = (uint32_t)strtoul(dash + 1, nullptr, 16);
    // Validate what we are actually going to USE, not the raw text.  Silently
    // masking the offsets to 16 bits is how "E0:E000-E0FFF" armed $E0E000-$E00FFF:
    // raw hi > lo so the old check passed, the mask then inverted the pair, and
    // the (hi - lo) underflow asked calloc for half a gigabyte -- and answered
    // status=OK, having written a 512 MB file of nothing.
    if (banked && (lo > 0xFFFF || hi > 0xFFFF)) {
        fprintf(stderr, "A2GSPU_COVERAGE: offset out of range in '%s' -- with a "
                        "BANK: prefix both ends must be $0000-$FFFF\n", spec);
        return false;
    }
    uint32_t flo = banked ? ((bank << 16) | lo) : (lo & 0xFFFFFF);
    uint32_t fhi = banked ? ((bank << 16) | hi) : (hi & 0xFFFFFF);
    if (fhi < flo) {
        fprintf(stderr, "A2GSPU_COVERAGE: HI < LO ($%06X-$%06X) in '%s'\n", flo, fhi, spec);
        return false;
    }
    // Re-arming must not leak the previous bitmap, and a failed re-arm must not
    // leave the old range live and collecting: disarm first, then commit.
    free(g_cov_bits);
    g_cov_bits = nullptr;
    g_cov_on = false;
    g_cov_lo = flo;
    g_cov_hi = fhi;
    g_cov_nbytes = ((size_t)(g_cov_hi - g_cov_lo) >> 3) + 1;
    g_cov_bits = (uint8_t *)calloc(g_cov_nbytes, 1);
    if (!g_cov_bits) { fprintf(stderr, "A2GSPU_COVERAGE: out of memory\n"); return false; }
    g_cov_prev_pc = 0xFFFFFFFFu;
    g_cov_marked = 0;
    g_cov_on = true;
    fprintf(stderr, "A2GSPU COVERAGE: enabled $%06X-$%06X (%zu bytes of bitmap)\n",
            g_cov_lo, g_cov_hi, g_cov_nbytes);
    return true;
}

// Clear the accumulated bits without disturbing the range.  This is what makes
// coverage *scopable*: reset at a decision point, perform one activity, dump, and
// the bitmap describes THAT activity alone (e.g. "the code a shop conversation
// touches") instead of everything since boot.
inline void a2gspu_cov_reset() {
    if (!g_cov_on || !g_cov_bits) return;
    memset(g_cov_bits, 0, g_cov_nbytes);
    g_cov_marked = 0;
    g_cov_prev_pc = 0xFFFFFFFFu;
}

// Disarm and release the bitmap.  Symmetric with cov_init so that arm/disarm
// cycles across a long session don't leak one bitmap per arm.
inline void a2gspu_cov_off() {
    free(g_cov_bits);
    g_cov_bits = nullptr;
    g_cov_nbytes = 0;
    g_cov_marked = 0;
    g_cov_prev_pc = 0xFFFFFFFFu;
    g_cov_on = false;
}

// On-disk format (little-endian), deliberately trivial so any consumer can read it:
//   0  : "A2COV1\0"   8 bytes (7 chars + NUL)
//   8  : lo           uint32
//   12 : hi           uint32
//   16 : bitmap       ceil((hi-lo+1)/8) bytes, bit i = byte (lo+i) executed
// Returns true only if a file actually landed on disk.  Callers MUST propagate
// this: a caller that answers "wrote" unconditionally turns a silent no-op into
// a reported success, which is the worst failure mode instrumentation has.
static inline bool a2gspu_cov_write(const char *path) {
    if (!g_cov_on || !g_cov_bits || !path || !path[0]) return false;
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "A2GSPU COVERAGE: cannot write '%s'\n", path); return false; }
    fwrite("A2COV1\0", 1, 8, f);
    uint32_t lo = g_cov_lo, hi = g_cov_hi;
    fwrite(&lo, 4, 1, f); fwrite(&hi, 4, 1, f);
    fwrite(g_cov_bits, 1, g_cov_nbytes, f);
    fclose(f);
    uint32_t span = g_cov_hi - g_cov_lo + 1;
    fprintf(stderr, "A2GSPU COVERAGE: wrote %s -- %llu/%u bytes executed (%.1f%%)\n",
            path, (unsigned long long)g_cov_marked, span,
            span ? (100.0 * (double)g_cov_marked / (double)span) : 0.0);
    return true;
}
