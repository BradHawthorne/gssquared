#pragma once
// ============================================================================
// memvu_istream.hpp — instruction-stream shape
//   MEMVU_OPMIX  : what instructions does this workload actually execute, and
//                  under which CPU mode?
//   MEMVU_IREUSE : how much does the instruction stream repeat, and how often
//                  does the CPU mode change underneath code that is repeating?
//   MEMVU_WORKSET: how much of the data traffic goes through the direct page,
//                  how many direct pages are live, and how deep does the stack
//                  actually get?
//
// MEMVU_WORKSET exists because "this architecture's programs keep their working
// set in the direct page and on the stack" is the kind of claim everyone repeats
// and nobody has counted. Direct-page accesses are counted EXACTLY, at the two
// funnels that form a direct-page data address, not inferred by testing whether
// an address happens to fall inside the current D window -- an ordinary absolute
// access to the same address would be indistinguishable that way. D and S are
// sampled once per instruction at the dispatch tap, so the count of distinct
// direct pages and the stack's depth are both exact rather than sampled at
// access time and biased toward busy pages.
//
// The store/load rails (memvu_storevis.hpp) answer where a workload's memory
// traffic goes. These answer what the workload *is*. Between them they describe
// the shape of a program without anyone having to read its source, which is the
// point: an instruction mix is otherwise a thing people assert from memory of
// how 65xx code "usually" looks.
//
// ---------------------------------------------------------------------------
// MODE CONTEXT
//
// This emulator selects a CPU implementation per (E, M-width, X-width) combination
// at compile time, so the mode is a constant at the dispatch site and costs
// nothing to record. It is packed here as a 3-bit context:
//
//     ctx = (e_mode << 2) | (m_16 << 1) | x_16       0..7, named in the report
//
// ---------------------------------------------------------------------------
// MEMVU_IREUSE — the model, stated plainly because it IS a model
//
// A direct-mapped cache of instruction *lines*, each line carrying the mode
// context it was last seen under. Per executed instruction:
//
//     tag match + mode match   -> hit        the line is usable as-is
//     tag match + mode differs -> modemiss   same bytes, different meaning
//     otherwise                -> miss       the line is not resident
//
// `modemiss` is the interesting one and does not exist in a conventional
// instruction cache: on this ISA an instruction's LENGTH depends on M and X, so
// a resident line decoded under one mode cannot be reused under another. The
// rate at which that happens bounds the value of caching anything decoded, and
// it is not a quantity anyone should estimate by intuition.
//
// Geometry is configurable (`MEMVU_IREUSE=<lines>,<linesize>`) because the
// answer depends on it and a single hard-coded geometry would invite quoting one
// number as if it were the property of the software rather than of the model.
// Both must be powers of two. Default 512 x 64 bytes.
//
// A separate bitmap records every distinct line ever executed, giving the
// instruction footprint of the workload independent of any cache geometry.
//
// ---------------------------------------------------------------------------
// THE OPCODE GROUP TABLE IS HAND-DERIVED.
//
// The named aggregates below (branches, RMW, stack, block move, mode change) come
// from a table written by hand against the 65816 opcode map. It is good enough to
// steer attention and is NOT good enough to quote as a normative figure without
// checking it against the data sheet. The raw per-opcode histogram is emitted
// alongside precisely so that any aggregate can be recomputed from primary data
// rather than trusted.
//
// Header-only, env-gated, observe-don't-disturb. Unarmed: one untaken branch per
// instruction. Generic naming only — this models a real Apple IIgs.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ---- arm bits ---------------------------------------------------------------
inline bool memvu_op_on  = false;   // MEMVU_OPMIX
inline bool memvu_ir_on  = false;   // MEMVU_IREUSE
inline bool memvu_ws_on  = false;   // MEMVU_WORKSET

// ---- WORKSET state ----------------------------------------------------------
inline uint64_t memvu_ws_dp_read = 0, memvu_ws_dp_write = 0;
inline uint8_t *memvu_ws_dseen = nullptr;   // bitmap of every D value observed (8 KB)
inline uint64_t memvu_ws_ddistinct = 0, memvu_ws_dchanges = 0;
inline uint32_t memvu_ws_dlast = 0xFFFFFFFF;
inline uint32_t memvu_ws_smin = 0xFFFFFFFF, memvu_ws_smax = 0;
inline uint64_t memvu_ws_spage[256] = {};   // histogram of S >> 8

inline bool memvu_ws_init() {
    if (!memvu_ws_dseen) memvu_ws_dseen = (uint8_t *)calloc(8192, 1);   // 65536 bits
    return memvu_ws_dseen != nullptr;
}

// Exact: called from the two funnels that form a direct-page data address.
inline void memvu_ws_note_dp(bool write) {
    if (write) memvu_ws_dp_write++; else memvu_ws_dp_read++;
}

// Sampled once per instruction, so D churn and stack depth are unbiased by how
// busy any particular page or frame happens to be.
inline void memvu_ws_note_state(uint32_t d, uint32_t sp) {
    if (d != memvu_ws_dlast) {
        if (memvu_ws_dlast != 0xFFFFFFFF) memvu_ws_dchanges++;
        memvu_ws_dlast = d;
    }
    const uint32_t dv = d & 0xFFFF;
    uint8_t &cell = memvu_ws_dseen[dv >> 3];
    const uint8_t bit = (uint8_t)(1u << (dv & 7));
    if (!(cell & bit)) { cell |= bit; memvu_ws_ddistinct++; }

    const uint32_t s = sp & 0xFFFF;
    if (s < memvu_ws_smin) memvu_ws_smin = s;
    if (s > memvu_ws_smax) memvu_ws_smax = s;
    memvu_ws_spage[(s >> 8) & 0xFF]++;
}

// ---- mode context -----------------------------------------------------------
inline const char *memvu_ctx_name[8] = {
    "N/m8/x8", "N/m8/x16", "N/m16/x8", "N/m16/x16",
    "E/m8/x8", "E/m8/x16", "E/m16/x8", "E/m16/x16",   // E forces 8/8; the rest are unreachable
};

// ---- OPMIX state ------------------------------------------------------------
inline uint64_t memvu_op_count[8][256] = {};
inline uint64_t memvu_op_total = 0;

// ---- IREUSE state -----------------------------------------------------------
inline uint32_t memvu_ir_lines    = 512;
inline uint32_t memvu_ir_linesize = 64;
inline uint32_t memvu_ir_shift    = 6;      // log2(linesize)
inline uint32_t memvu_ir_mask     = 511;    // lines - 1

inline uint32_t *memvu_ir_tag   = nullptr;  // per set: line address, or 0xFFFFFFFF
inline uint8_t  *memvu_ir_ctx   = nullptr;  // per set: mode context last seen
inline uint8_t  *memvu_ir_seen  = nullptr;  // bitmap of every line ever executed

inline uint64_t memvu_ir_insns = 0, memvu_ir_hit = 0, memvu_ir_miss = 0, memvu_ir_modemiss = 0;
inline uint64_t memvu_ir_distinct = 0;

// 24-bit space at 16-byte granularity is the finest the bitmap need be; lines are
// never smaller than that in practice, and it keeps the map at 128 KB.
static constexpr uint32_t MEMVU_SEEN_SHIFT = 4;
static constexpr uint32_t MEMVU_SEEN_BITS  = 1u << (24 - MEMVU_SEEN_SHIFT);   // 1,048,576
static constexpr uint32_t MEMVU_SEEN_BYTES = MEMVU_SEEN_BITS / 8;             // 131,072

inline bool memvu_ir_init(const char *spec) {
    uint32_t lines = 512, lsz = 64;
    if (spec && *spec && strcmp(spec, "1") != 0) {
        unsigned a = 0, b = 0;
        if (sscanf(spec, "%u,%u", &a, &b) == 2 && a && b) { lines = a; lsz = b; }
        else return false;
    }
    // powers of two, or the index arithmetic below is wrong and would report a
    // confident number produced by a broken model
    if ((lines & (lines - 1)) || (lsz & (lsz - 1)) || lsz < 4) return false;

    memvu_ir_lines = lines; memvu_ir_linesize = lsz;
    memvu_ir_mask = lines - 1;
    memvu_ir_shift = 0; while ((1u << memvu_ir_shift) < lsz) memvu_ir_shift++;

    memvu_ir_tag  = (uint32_t *)malloc(sizeof(uint32_t) * lines);
    memvu_ir_ctx  = (uint8_t  *)malloc(lines);
    memvu_ir_seen = (uint8_t  *)calloc(MEMVU_SEEN_BYTES, 1);
    if (!memvu_ir_tag || !memvu_ir_ctx || !memvu_ir_seen) return false;
    memset(memvu_ir_tag, 0xFF, sizeof(uint32_t) * lines);
    memset(memvu_ir_ctx, 0, lines);
    return true;
}

inline void memvu_istream_reset() {
    memset(memvu_op_count, 0, sizeof memvu_op_count);
    memvu_op_total = 0;
    memvu_ir_insns = memvu_ir_hit = memvu_ir_miss = memvu_ir_modemiss = memvu_ir_distinct = 0;
    if (memvu_ir_tag)  memset(memvu_ir_tag, 0xFF, sizeof(uint32_t) * memvu_ir_lines);
    if (memvu_ir_ctx)  memset(memvu_ir_ctx, 0, memvu_ir_lines);
    if (memvu_ir_seen) memset(memvu_ir_seen, 0, MEMVU_SEEN_BYTES);
    memvu_ws_dp_read = memvu_ws_dp_write = 0;
    memvu_ws_ddistinct = memvu_ws_dchanges = 0;
    memvu_ws_dlast = 0xFFFFFFFF;
    memvu_ws_smin = 0xFFFFFFFF; memvu_ws_smax = 0;
    memset(memvu_ws_spage, 0, sizeof memvu_ws_spage);
    if (memvu_ws_dseen) memset(memvu_ws_dseen, 0, 8192);
}

// ---- the dispatch tap -------------------------------------------------------
// Called once per executed instruction, with the address of the opcode byte (the
// PC *before* the fetch advances it) and the compile-time mode context.
inline void memvu_istream_note(uint32_t pc24, uint8_t opcode, uint8_t ctx) {
    if (memvu_op_on) {
        memvu_op_count[ctx & 7][opcode]++;
        memvu_op_total++;
    }
    if (memvu_ir_on && memvu_ir_tag) {
        memvu_ir_insns++;
        const uint32_t line = (pc24 & 0xFFFFFF) >> memvu_ir_shift;
        const uint32_t set  = line & memvu_ir_mask;
        if (memvu_ir_tag[set] == line) {
            if (memvu_ir_ctx[set] == ctx) memvu_ir_hit++;
            else { memvu_ir_modemiss++; memvu_ir_ctx[set] = ctx; }
        } else {
            memvu_ir_miss++;
            memvu_ir_tag[set] = line;
            memvu_ir_ctx[set] = ctx;
        }
        const uint32_t sb = (pc24 & 0xFFFFFF) >> MEMVU_SEEN_SHIFT;
        uint8_t &cell = memvu_ir_seen[sb >> 3];
        const uint8_t bit = (uint8_t)(1u << (sb & 7));
        if (!(cell & bit)) { cell |= bit; memvu_ir_distinct++; }
    }
}

// ---- hand-derived opcode groups (see the caveat in the header) --------------
inline bool memvu_op_in(uint8_t op, const uint8_t *set, int n) {
    for (int i = 0; i < n; i++) if (set[i] == op) return true;
    return false;
}
inline uint64_t memvu_op_sum(const uint8_t *set, int n) {
    uint64_t t = 0;
    for (int c = 0; c < 8; c++)
        for (int i = 0; i < n; i++) t += memvu_op_count[c][set[i]];
    return t;
}

// ---- report -----------------------------------------------------------------
inline void memvu_istream_report(FILE *f) {
    if (memvu_op_on) {
        if (memvu_op_total == 0) {
            fprintf(f, "MEMVU OPMIX: insns=0 -- no instructions observed; nothing to report\n");
        } else {
            static const uint8_t BRANCH[] = {0x10,0x30,0x50,0x70,0x90,0xB0,0xD0,0xF0,0x80,0x82};
            static const uint8_t RMW[]    = {0x06,0x0E,0x16,0x1E, 0x46,0x4E,0x56,0x5E,
                                             0x26,0x2E,0x36,0x3E, 0x66,0x6E,0x76,0x7E,
                                             0xE6,0xEE,0xF6,0xFE, 0xC6,0xCE,0xD6,0xDE,
                                             0x04,0x0C,0x14,0x1C};
            static const uint8_t STACK[]  = {0x48,0x68,0x08,0x28,0xDA,0xFA,0x5A,0x7A,
                                             0x8B,0xAB,0x0B,0x2B,0x4B,0xF4,0xD4,0x62};
            static const uint8_t CALLRET[]= {0x20,0x22,0x60,0x6B,0x40};
            static const uint8_t BLOCK[]  = {0x54,0x44};
            static const uint8_t MODESW[] = {0xC2,0xE2,0xFB};

            fprintf(f, "MEMVU OPMIX: insns=%llu branch=%llu rmw=%llu stack=%llu "
                       "callret=%llu blockmove=%llu modeswitch=%llu  (groups hand-derived)\n",
                    (unsigned long long)memvu_op_total,
                    (unsigned long long)memvu_op_sum(BRANCH,  sizeof BRANCH),
                    (unsigned long long)memvu_op_sum(RMW,     sizeof RMW),
                    (unsigned long long)memvu_op_sum(STACK,   sizeof STACK),
                    (unsigned long long)memvu_op_sum(CALLRET, sizeof CALLRET),
                    (unsigned long long)memvu_op_sum(BLOCK,   sizeof BLOCK),
                    (unsigned long long)memvu_op_sum(MODESW,  sizeof MODESW));

            fprintf(f, "MEMVU OPMIX CTX:");
            for (int c = 0; c < 8; c++) {
                uint64_t t = 0;
                for (int o = 0; o < 256; o++) t += memvu_op_count[c][o];
                if (t) fprintf(f, " %s=%llu", memvu_ctx_name[c], (unsigned long long)t);
            }
            fprintf(f, "\n");

            // raw primary data, so any aggregate above can be recomputed
            fprintf(f, "MEMVU OPMIX RAW:");
            for (int o = 0; o < 256; o++) {
                uint64_t t = 0;
                for (int c = 0; c < 8; c++) t += memvu_op_count[c][o];
                if (t) fprintf(f, " %02X=%llu", o, (unsigned long long)t);
            }
            fprintf(f, "\n");
        }
    }

    if (memvu_ir_on) {
        if (!memvu_ir_tag) {
            fprintf(f, "MEMVU IREUSE: ** NOT ARMED ** geometry invalid or allocation failed\n");
        } else if (memvu_ir_insns == 0) {
            fprintf(f, "MEMVU IREUSE: insns=0 -- no instructions observed; nothing to report\n");
        } else {
            const double n = (double)memvu_ir_insns;
            fprintf(f, "MEMVU IREUSE: geometry=%ux%uB insns=%llu hit=%llu (%.2f%%) "
                       "miss=%llu (%.2f%%) modemiss=%llu (%.2f%%)\n",
                    memvu_ir_lines, memvu_ir_linesize,
                    (unsigned long long)memvu_ir_insns,
                    (unsigned long long)memvu_ir_hit,      100.0 * memvu_ir_hit / n,
                    (unsigned long long)memvu_ir_miss,     100.0 * memvu_ir_miss / n,
                    (unsigned long long)memvu_ir_modemiss, 100.0 * memvu_ir_modemiss / n);
            fprintf(f, "MEMVU IREUSE FOOTPRINT: distinct %uB-granules=%llu (~%llu KB of executed code)\n",
                    1u << MEMVU_SEEN_SHIFT,
                    (unsigned long long)memvu_ir_distinct,
                    (unsigned long long)((memvu_ir_distinct << MEMVU_SEEN_SHIFT) / 1024));
        }
    }

    if (memvu_ws_on) {
        const uint64_t dp = memvu_ws_dp_read + memvu_ws_dp_write;
        if (memvu_ws_smin > memvu_ws_smax) {
            fprintf(f, "MEMVU WORKSET: no instructions observed; nothing to report\n");
        } else {
            fprintf(f, "MEMVU WORKSET: dp_accesses=%llu (read=%llu write=%llu) "
                       "distinct_D=%llu D_changes=%llu\n",
                    (unsigned long long)dp,
                    (unsigned long long)memvu_ws_dp_read,
                    (unsigned long long)memvu_ws_dp_write,
                    (unsigned long long)memvu_ws_ddistinct,
                    (unsigned long long)memvu_ws_dchanges);
            fprintf(f, "MEMVU WORKSET STACK: S range=[$%04X..$%04X] span=%u bytes  pages:",
                    memvu_ws_smin, memvu_ws_smax, memvu_ws_smax - memvu_ws_smin + 1);
            for (int p = 0; p < 256; p++)
                if (memvu_ws_spage[p]) fprintf(f, " %02X=%llu", p, (unsigned long long)memvu_ws_spage[p]);
            fprintf(f, "\n");
        }
    }
}
