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
// control-plane-only (the spike and CTRL units, not the CPU core). The CPU-register dump itself lives in
// iigs_toolbox.hpp (iigs_cpu_state_dump_regs), shared with the BRK path.
// ============================================================================
#include "a2gspu_observe.hpp"   // a2obs_peek (M11)
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cpu.hpp"
#include "iigs_toolbox.hpp"
#include "iigs_shr_layout.hpp"

// ---- the warning counter, defined before anything that needs to warn -------
// Every "skipped" / "could not" a run prints goes through here, so the GSDIAG
// line can carry warnings=N and A2GSPU_STRICT=1 can refuse to call such a run
// OK. A rail that skips silently -- or loudly, on a line nobody parses -- is
// how a missing blit binary measured the idle desktop as a healthy row.
//
// THIS PAIR USED TO LIVE AT THE BOTTOM OF THIS HEADER, below the assert
// evaluator, and that position was the bug: the two places that most need to
// warn -- an unknown assert field, an address that is not probe-readable --
// could reach printf and nothing else. Their diagnostics never reached
// warnings=, so `A2GSPU_ASSERT="totally_bogus_field!=5" A2GSPU_STRICT=1`
// printed "unknown field", then "-> PASS", then "warnings=0", and exited 0.
// Declaration order is what made a strict run unable to see its own complaint.
inline int g_a2gspu_warnings = 0;
inline void a2gspu_warn(const char *rail, const char *what) {
    g_a2gspu_warnings++;
    printf("%s: WARNING %s\n", rail, what);
}

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
                else   // observer read, never the CPU path: DUMP is an observe rail (M11)
                    b = a2obs_peek(cpu->mmu, (bank << 16) | ((a + i) & 0xFFFF));
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
//   idxN  (N=0..15: mode+fill-correct displayed pixel count of palette index N;
//          320 fill-mode zeros count as the previous color on that line)
// Grammar: "name OP value [; name OP value ...]", OP in == != <= >= < >.
// Values are DECIMAL unless prefixed $ or 0x (0d forces decimal, a bare leading
// zero makes strtol read octal); a peek: comparand must carry an explicit base.
//
// *ok is cleared when the field could not be EVALUATED AT ALL -- an unknown
// name, a handler-backed address, a toolbox call that never fired. Each of
// those returns a sentinel (-1 / -999999), and a sentinel that keeps comparing
// as an ordinary number is how `totally_bogus_field != 5` and `qd_carry != 0`
// on a run where QDStartUp never fired both reported PASS. The caller must
// fail the clause, not compare the sentinel.
inline long iigs_assert_value(const char *name, cpu_state *cpu, const uint8_t *e1,
                              int nonzero, int distinct, bool *ok) {
    auto tf = [&](uint16_t tw, int which) -> long {
        auto it = g_iigs_last_result.find(tw);
        if (it == g_iigs_last_result.end()) {
            char m[192];
            snprintf(m, sizeof m, "'%s' names toolbox call $%04X, which was never "
                     "recorded on this run -- the clause cannot be evaluated",
                     name, (unsigned)tw);
            a2gspu_warn("IIGS ASSERT", m);
            if (ok) *ok = false;
            return -1;
        }
        return which == 0 ? (it->second.first ? 1 : 0) : it->second.second;
    };
    // peek:HHHHHH -> observation-free byte at the 24-bit address (bank<<16|addr),
    // e.g. peek:E11E00 (a command-ring byte) or peek:0003FC (a bank-0 work-area
    // pointer byte). Via MMU::probe_peek (no soft-switch/slot/clock side effect) —
    // the introspection floor's structural-assert primitive.
    if (!strncmp(name, "peek:", 5)) {
        uint32_t a = (uint32_t)strtoul(name + 5, nullptr, 16) & 0xFFFFFF;
        if (!cpu || !cpu->mmu) {
            a2gspu_warn("IIGS ASSERT", "no MMU in hand -- a peek: clause cannot be evaluated");
            if (ok) *ok = false;
            return -1;
        }
        // peek: is the ONLY machine-independent assert field, so a silent wrong
        // answer here poisons every portable gate. probe_peek returns floating-bus
        // noise for handler-backed pages ($C0xx always), which would make an assert
        // pass or fail on bus garbage. Refuse instead of guessing.
        if (!cpu->mmu->probe_readable(a)) {
            // Through a2gspu_warn, not printf: this IS a "could not", and the
            // contract above says every one of them must reach warnings= so
            // A2GSPU_STRICT=1 can refuse to call the run OK.
            char m[224];
            snprintf(m, sizeof m, "peek:%06X is NOT probe-readable (handler-backed "
                     "page; probe_peek would return floating bus, not memory) -- "
                     "the clause cannot be evaluated", a);
            a2gspu_warn("IIGS ASSERT", m);
            if (ok) *ok = false;
            return -999999;
        }
        return cpu->mmu->probe_peek(a);
    }
    if (!strcmp(name, "scb_mode")) return (e1[0x9D00] & iigs_shr::SCB_640) ? 640 : 320;
    if (!strcmp(name, "qd_carry")) return tf(0x0204, 0);
    if (!strcmp(name, "qd_err"))   return tf(0x0204, 1);
    if (!strcmp(name, "lt_carry")) return tf(0x0E01, 0);
    if (!strcmp(name, "cs_carry")) return tf(0x1504, 0);
    if (!strcmp(name, "pr_carry")) return tf(0x5404, 0);
    if (!strcmp(name, "gsos_err")) return g_iigs_last_gsos_err;
    if (!strcmp(name, "nonzero"))  return nonzero;
    if (!strcmp(name, "distinct")) return distinct;
    // idxN -> displayed count of palette-index N (320/640 + fill expansion).
    if (name[0]=='i' && name[1]=='d' && name[2]=='x' && name[3]>='0' && name[3]<='9') {
        int idx = atoi(name + 3);
        if (idx >= 0 && idx < 16) { long h[16]; iigs_shr::histogram(e1, h); return h[idx]; }
    }
    {
        char m[160];
        snprintf(m, sizeof m, "unknown field '%s' -- no such assert name; the "
                 "clause cannot be evaluated", name);
        a2gspu_warn("IIGS ASSERT", m);
    }
    if (ok) *ok = false;
    return -999999;
}

// The comparand's value, and whether the literal SAID which base it is in.
//   $HH / 0xHH -> hex, explicit        0dNN -> decimal, explicit
//   anything else -> strtol base 0 (decimal, octal on a bare leading zero),
//                    and NOT explicit.
// *ambiguous_hex is set when an inexplicit literal is more than one digit and
// every digit is a hex digit -- i.e. exactly when reading it as decimal and
// reading it as hex give different numbers and nothing in the text says which
// the author meant.
inline long iigs_assert_parse_value(const char *vs, bool *explicit_base,
                                    bool *ambiguous_hex) {
    *explicit_base = false; *ambiguous_hex = false;
    if (vs[0] == '$') { *explicit_base = true; return strtol(vs + 1, nullptr, 16); }
    if (vs[0] == '0' && (vs[1] == 'x' || vs[1] == 'X')) { *explicit_base = true; return strtol(vs + 2, nullptr, 16); }
    if (vs[0] == '0' && (vs[1] == 'd' || vs[1] == 'D')) { *explicit_base = true; return strtol(vs + 2, nullptr, 10); }
    int nd = 0; bool allhex = true;
    for (const char *q = vs; *q && *q != ' '; q++) {
        if (!isxdigit((unsigned char)*q)) { allhex = false; break; }
        nd++;
    }
    *ambiguous_hex = (allhex && nd > 1);
    return strtol(vs, nullptr, 0);
}

// Both bases on every printed line. The result line used to read "== 34"
// whether the spec wrote 34, 0x22 or $22, so a right-base spec and a
// wrong-base one were indistinguishable in the log AND in a capture of it.
inline void iigs_assert_fmt_num(char *b, size_t n, long v) {
    if (v < 0) snprintf(b, n, "%ld", v);
    else       snprintf(b, n, "$%lX (%ld)", v, v);
}

// Is this spec a gate at all? A2GSPU_ASSERT set to "" or "   " asks for
// nothing, and arming a gate on the variable's mere presence made it report
// gate=PASS -- a verdict on a question nobody asked. The caller uses this to
// leave the gate unarmed (gate=none) instead; a NON-empty spec that parses to
// zero checks is a different thing and iigs_eval_asserts fails it.
inline bool a2gspu_assert_armed(const char *spec) {
    if (!spec) return false;
    for (const char *q = spec; *q; q++)
        if (!isspace((unsigned char)*q)) return true;
    return false;
}

inline int iigs_eval_asserts(const char *spec, cpu_state *cpu, const uint8_t *e1,
                             int nonzero, int distinct) {
    int allpass = 1, nchk = 0, ndrop = 0;
    // Computed BEFORE strtok chews buf. "Was anything asked of me at all?" and
    // "did anything parse?" are different questions and need different
    // verdicts: an unset-but-exported spec is no gate, a spec whose every
    // clause was unparseable is a failed one.
    const bool spec_nonempty = a2gspu_assert_armed(spec);
    char buf[512];
    strncpy(buf, spec, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
    for (char *tok = strtok(buf, ";,"); tok; tok = strtok(nullptr, ";,")) {
        while (*tok == ' ') tok++;
        char *op = nullptr; int oplen = 0;
        for (char *q = tok; *q; q++) {
            if ((q[0]=='='||q[0]=='!'||q[0]=='<'||q[0]=='>') && q[1]=='=') { op=q; oplen=2; break; }
            if (q[0]=='<' || q[0]=='>') { op=q; oplen=1; break; }
        }
        if (!op) {
            // A term with no operator used to be dropped in silence, and a spec
            // whose every term was dropped still printed "0 checks -> ALL PASS"
            // with gate=PASS and exit 0. The single-= typo `qd_carry=0` is
            // exactly that spec, and so is "totally bogus spec".
            if (*tok) {
                ndrop++;
                char m[224];
                snprintf(m, sizeof m, "clause '%s' has no comparison operator "
                         "(== != <= >= < >) -- IGNORED, NOT checked", tok);
                a2gspu_warn("A2GSPU_ASSERT", m);
                printf("IIGS ASSERT: unparseable term '%s' -> NOT CHECKED\n", tok);
                allpass = 0;
            }
            continue;
        }
        char name[64]; int nl = (int)(op - tok); if (nl > 63) nl = 63;
        memcpy(name, tok, nl); name[nl] = 0;
        while (nl > 0 && name[nl-1] == ' ') name[--nl] = 0;
        char ops[3] = {0}; memcpy(ops, op, oplen);
        char *vs = op + oplen; while (*vs == ' ') vs++;
        bool expl = false, ambig = false;
        long val = iigs_assert_parse_value(vs, &expl, &ambig);
        // peek: compares ONE BYTE, and a byte's natural written form is hex.
        // `peek:7E0100 == 34`, `== 0x22` and `== $22` all printed the IDENTICAL
        // line, so a spec that meant $22 and wrote 22 passed against the wrong
        // number with nothing in the log to say so. Refuse the ambiguity rather
        // than pick a base on the caller's behalf. SCOPED TO peek: on purpose:
        // scb_mode==640, idx6==28800 and gsos_err==0 are decimal by nature --
        // the fork's own README and rail-catalogue examples write them
        // bare -- and a blanket refusal would break every one of them.
        if (!strncmp(name, "peek:", 5) && !expl && ambig) {
            char m[256];
            snprintf(m, sizeof m, "%s value '%s' is ambiguous: write $%s (hex) "
                     "or 0d%s (decimal)", name, vs, vs, vs);
            a2gspu_warn("IIGS ASSERT", m);
            printf("IIGS ASSERT: %s %s '%s' -> FAIL (ambiguous base; write $%s or 0d%s)\n",
                   name, ops, vs, vs, vs);
            nchk++; allpass = 0;
            continue;
        }
        bool ok = true;
        long actual = iigs_assert_value(name, cpu, e1, nonzero, distinct, &ok);
        char vb[48], ab[48];
        iigs_assert_fmt_num(vb, sizeof vb, val);
        iigs_assert_fmt_num(ab, sizeof ab, actual);
        if (!ok) {
            // The field could not be evaluated. What came back is a sentinel,
            // not a measurement, and it must never satisfy a comparison -- the
            // whole point of the ok flag.
            nchk++; allpass = 0;
            printf("IIGS ASSERT: %s %s %s  (actual=UNEVALUATABLE) -> FAIL (unevaluatable)\n",
                   name, ops, vb);
            continue;
        }
        int pass = 0;
        if (!strcmp(ops,"==")) pass = (actual==val);
        else if (!strcmp(ops,"!=")) pass = (actual!=val);
        else if (!strcmp(ops,"<=")) pass = (actual<=val);
        else if (!strcmp(ops,">=")) pass = (actual>=val);
        else if (!strcmp(ops,"<"))  pass = (actual<val);
        else if (!strcmp(ops,">"))  pass = (actual>val);
        nchk++; if (!pass) allpass = 0;
        printf("IIGS ASSERT: %s %s %s  (actual=%s) -> %s\n",
               name, ops, vb, ab, pass ? "PASS" : "FAIL");
    }
    // AN ASSERT THAT EVALUATED NOTHING IS NOT A PASS. Four specs -- "qd_carry=0"
    // (the natural single-= typo), "", "   " and "totally bogus spec" -- all
    // printed "0 checks -> ALL PASS", gate=PASS, warnings=0, exit 0. gs2.cpp
    // now refuses to arm the gate at all on an all-whitespace spec, so a
    // harness that exports A2GSPU_ASSERT="" unconditionally still reports
    // gate=none; anything that reaches here was asked for and produced nothing.
    if (nchk == 0) {
        a2gspu_warn("A2GSPU_ASSERT", spec_nonempty
            ? "spec produced 0 checks -- every clause was unparseable"
            : "spec was empty -- 0 checks");
        printf("IIGS ASSERT: spec '%s' produced 0 checks (%d unparseable) -> FAILED "
               "(an assert that evaluates nothing is not a pass)\n", spec, ndrop);
        return 1;
    }
    /* The summary keeps the exact "N checks -> ALL PASS" spelling consumers match
     * whenever nothing was dropped. An unparseable clause fails the gate (it is
     * counted in allpass above), so "(0 unparseable)" on a pass carried no
     * information -- and adding it broke a harness that matched the line
     * verbatim. The count is printed exactly when it is non-zero, which is
     * exactly when it matters. */
    if (ndrop)
        printf("IIGS ASSERT: %d checks (%d unparseable) -> %s\n",
               nchk, ndrop, allpass ? "ALL PASS" : "FAILED");
    else
        printf("IIGS ASSERT: %d checks -> %s\n", nchk, allpass ? "ALL PASS" : "FAILED");
    return allpass ? 0 : 1;
}

// One machine-readable status line for the corpus harness (#79) to parse. The exit
// CODE stays gate-driven (0/1) for harness compat; this adds the richer category
// (OK / GATE_FAIL / CRASH_BRK / GSOS_ERROR / SNAP_FAIL / HALTED_EARLY).
// g_a2gspu_warnings / a2gspu_warn are defined at the TOP of this header, above
// the assert evaluator that needs them -- see the comment there.
// A rail that has something to say on the GSDIAG line registers a function
// here; the text it returns (leading space, key=value) is appended. Absent when
// no rail registered, so the field's presence says the rail ran.
inline const char *(*g_gsdiag_extra)(void) = nullptr;

// cycles= is the run's other determinism witness beside hash=: two runs whose
// A2GSPU PINS lines agree must agree on both, and a harness that differences
// two runs should refuse to subtract when they do not.
inline void iigs_emit_status(const char *status, int rc, const char *gate,
                             long gsos_err, int brk, long scb, uint64_t hash, uint64_t cycles) {
    printf("GSDIAG: status=%s rc=%d gate=%s gsos_err=$%04lX brk=%d scb=%ld hash=%016llX warnings=%d cycles=%llu%s\n",
           status, rc, gate, (unsigned long)(gsos_err & 0xFFFF), brk, scb,
           (unsigned long long)hash, g_a2gspu_warnings, (unsigned long long)cycles,
           g_gsdiag_extra ? g_gsdiag_extra() : "");
}
