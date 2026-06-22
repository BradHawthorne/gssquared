#pragma once
// ============================================================================
// iigs_diag.hpp — headless memory-range hexdump for GS/OS app bring-up.
//
// Parses an A2GSPU_DUMP="BANK/LO-HI[,BANK/LO-HI...]" spec (hex) and hexdumps
// each range to stdout at the spike. $E0/$E1 read directly from the Mega II
// image (side-effect-free); other banks go through the CPU MMU read. Useful
// for confirming what a program wrote (e.g. a command ring at $E1:$1E00, or a
// table in the app's load bank) without minting/decoding a trace by hand.
//
// gs2.cpp-only (not the CPU core). The CPU-register dump itself lives in
// iigs_toolbox.hpp (iigs_cpu_state_dump_regs), shared with the BRK path.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cpu.hpp"
#include "iigs_toolbox.hpp"
#include "iigs_shr_layout.hpp"

inline void iigs_mem_range_dump(cpu_state *cpu, const uint8_t *megaii_base,
                                const char *spec) {
    const char *p = spec;
    while (p && *p) {
        char *endp = nullptr;
        unsigned bank = (unsigned)strtoul(p, &endp, 16) & 0xFF;
        if (*endp != '/') break;
        unsigned lo = (unsigned)strtoul(endp + 1, &endp, 16) & 0xFFFF;
        if (*endp != '-') break;
        unsigned hi = (unsigned)strtoul(endp + 1, &endp, 16) & 0xFFFF;
        printf("IIGS MEM: $%02X/%04X-%04X\n", bank, lo, hi);
        for (unsigned a = lo; a <= hi; a += 16) {
            printf("  %02X/%04X:", bank, a);
            for (unsigned i = 0; i < 16 && (a + i) <= hi; i++) {
                uint8_t b;
                if ((bank | 1) == 0xE1 && megaii_base)
                    b = megaii_base[((bank & 1) << 16) | ((a + i) & 0xFFFF)];
                else
                    b = cpu->mmu->read((bank << 16) | ((a + i) & 0xFFFF));
                printf(" %02X", b);
            }
            printf("\n");
        }
        if (*endp == ',') p = endp + 1; else break;
    }
}

// ---- Assertion gate (#2): evaluate A2GSPU_ASSERT and return 0=pass / 1=fail --
// Named values an assertion can test:
//   scb_mode  -> 320 or 640 (from line-0 SCB bit7)   gsos_err -> last GS/OS error
//   qd_carry/qd_err  (QDStartUp $0204)   lt_carry (LoadTools $0E01)
//   cs_carry (ClearScreen $1504)   pr_carry (PaintRect $5404)
//   nonzero / distinct  (SHR-window byte stats)
//   idxN  (N=0..15: mode-correct pixel count of palette index N;
//          e.g. idx6==28800 = a 240x120 colour-6 PaintRect rectangle)
// Grammar: "name OP value [; name OP value ...]", OP in == != <= >= < >.
inline long iigs_assert_value(const char *name, cpu_state *cpu, const uint8_t *e1,
                              int nonzero, int distinct) {
    auto tf = [](uint16_t tw, int which) -> long {
        auto it = g_iigs_last_result.find(tw);
        if (it == g_iigs_last_result.end()) return -1;
        return which == 0 ? (it->second.first ? 1 : 0) : it->second.second;
    };
    // peek:HHHHHH -> observation-free byte at the 24-bit address (bank<<16|addr),
    // e.g. peek:E11E00 (a command-ring byte) or peek:0003FC (a bank-0 work-area
    // pointer byte). Via MMU::probe_peek (no soft-switch/slot/clock side effect) —
    // the introspection floor's structural-assert primitive.
    if (!strncmp(name, "peek:", 5)) {
        uint32_t a = (uint32_t)strtoul(name + 5, nullptr, 16) & 0xFFFFFF;
        return (cpu && cpu->mmu) ? cpu->mmu->probe_peek(a) : -1;
    }
    if (!strcmp(name, "scb_mode")) return (e1[0x9D00] & 0x80) ? 640 : 320;
    if (!strcmp(name, "qd_carry")) return tf(0x0204, 0);
    if (!strcmp(name, "qd_err"))   return tf(0x0204, 1);
    if (!strcmp(name, "lt_carry")) return tf(0x0E01, 0);
    if (!strcmp(name, "cs_carry")) return tf(0x1504, 0);
    if (!strcmp(name, "pr_carry")) return tf(0x5404, 0);
    if (!strcmp(name, "gsos_err")) return g_iigs_last_gsos_err;
    if (!strcmp(name, "nonzero"))  return nonzero;
    if (!strcmp(name, "distinct")) return distinct;
    // idxN -> the mode-correct count of palette-index N across the SHR window
    // (e.g. idx6==28800 proves a 240x120 colour-6 PaintRect rectangle is on screen).
    if (name[0]=='i' && name[1]=='d' && name[2]=='x' && name[3]>='0' && name[3]<='9') {
        int idx = atoi(name + 3);
        if (idx >= 0 && idx < 16) { long h[16]; iigs_shr::histogram(e1, h); return h[idx]; }
    }
    printf("IIGS ASSERT: unknown field '%s'\n", name);
    return -999999;
}

inline int iigs_eval_asserts(const char *spec, cpu_state *cpu, const uint8_t *e1,
                             int nonzero, int distinct) {
    int allpass = 1, nchk = 0;
    char buf[512];
    strncpy(buf, spec, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (char *tok = strtok(buf, ";,"); tok; tok = strtok(nullptr, ";,")) {
        while (*tok == ' ') tok++;
        char *op = nullptr; int oplen = 0;
        for (char *q = tok; *q; q++) {
            if ((q[0]=='='||q[0]=='!'||q[0]=='<'||q[0]=='>') && q[1]=='=') { op=q; oplen=2; break; }
            if (q[0]=='<' || q[0]=='>') { op=q; oplen=1; break; }
        }
        if (!op) continue;
        char name[64]; int nl = (int)(op - tok); if (nl > 63) nl = 63;
        memcpy(name, tok, nl); name[nl] = 0;
        while (nl > 0 && name[nl-1] == ' ') name[--nl] = 0;
        char ops[3] = {0}; memcpy(ops, op, oplen);
        char *vs = op + oplen; while (*vs == ' ') vs++;
        long val = (vs[0] == '$') ? strtol(vs+1, nullptr, 16) : strtol(vs, nullptr, 0);
        long actual = iigs_assert_value(name, cpu, e1, nonzero, distinct);
        int pass = 0;
        if (!strcmp(ops,"==")) pass = (actual==val);
        else if (!strcmp(ops,"!=")) pass = (actual!=val);
        else if (!strcmp(ops,"<=")) pass = (actual<=val);
        else if (!strcmp(ops,">=")) pass = (actual>=val);
        else if (!strcmp(ops,"<"))  pass = (actual<val);
        else if (!strcmp(ops,">"))  pass = (actual>val);
        nchk++; if (!pass) allpass = 0;
        printf("IIGS ASSERT: %s %s %ld  (actual=%ld) -> %s\n",
               name, ops, val, actual, pass ? "PASS" : "FAIL");
    }
    printf("IIGS ASSERT: %d checks -> %s\n", nchk, allpass ? "ALL PASS" : "FAILED");
    return allpass ? 0 : 1;
}

// One machine-readable status line for the corpus harness (#79) to parse. The exit
// CODE stays gate-driven (0/1) for harness compat; this adds the richer category
// (OK / GATE_FAIL / CRASH_BRK / GSOS_ERROR / SNAP_FAIL / HALTED_EARLY).
inline void iigs_emit_status(const char *status, int rc, const char *gate,
                             long gsos_err, int brk, long scb, uint64_t hash) {
    printf("GSDIAG: status=%s rc=%d gate=%s gsos_err=$%04lX brk=%d scb=%ld hash=%016llX\n",
           status, rc, gate, (unsigned long)(gsos_err & 0xFFFF), brk, scb,
           (unsigned long long)hash);
}
