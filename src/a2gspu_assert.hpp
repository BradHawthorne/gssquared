#pragma once
// ============================================================================
// a2gspu_assert.hpp — the CTRL `assert` verb, evaluated by the SAME primitives
// as the A2GSPU_ASSERT env rail.
//
// WHAT THIS DELETES. The rail had two assertion implementations for one rail
// name. `A2GSPU_ASSERT` (env, spike) and `assert` (CTRL, live) are advertised
// by the same registry row and the same doc paragraph, and against the SAME
// BINARY they disagreed five ways. Measured, both surfaces, one session:
//
//   1. Grammar.  env took six operators (== != <= >= < >); CTRL took `==` only.
//   2. Fields.   env took ten field kinds (scb_mode, qd_carry/qd_err, lt_carry,
//                cs_carry/pr_carry, gsos_err, nonzero/distinct, idxN, peek:);
//                CTRL took peek: and nothing else -- `assert scb_mode==0` came
//                back "parse-fail" on the surface whose own doc row lists it.
//   3. Honesty.  env refuses to evaluate a peek: whose page is handler-backed
//                ("probe_peek would return floating bus, not memory"); CTRL
//                called probe_peek anyway, so `assert peek:00C029==EE` reported
//                status=PASS against bus noise.
//   4. Value.    CTRL's sscanf("%x") silently truncated `==169` to `$69`.
//   5. Base.     CTRL printed `got=A9`, env printed `(actual=169)`. Anyone
//                diffing the two transcripts compares A9 against 169 for the
//                same byte and sees a difference that is not there.
//
// So CTRL no longer decides any of that. Every question with a machine answer
// -- what a field name means, whether an address may be probed at all, how a
// literal's base is read, how a number prints -- is answered by the env rail's
// own functions, called from here:
//
//     iigs_assert_value()        the ten field kinds + the probe_readable refusal
//     iigs_assert_parse_value()  $HH / 0xHH / 0dNN, and the ambiguity flag
//     iigs_assert_fmt_num()      "$A9 (169)" -- both bases, one spelling
//
// WHAT IS STILL DUPLICATED, AND WHY. The clause LOOP (split on ;/, find the
// operator, apply the ambiguity rule, compare) exists here and in
// iigs_eval_asserts. Folding the loop together too would mean restructuring
// that function, and the honest reason it is not folded is timing, not design:
// it is being reworked in the same change set. The two loops are written to the
// same rules on purpose, and tools/a2rail/gs816/assertparity.ps1 is the thing
// that refuses to let them drift -- it feeds ONE expression list to both
// surfaces and fails if the verdicts differ. A shared primitive nobody
// differential-tests is just a slower divergence.
//
// THREE OUTCOMES, NOT TWO. "the check ran and was false" and "the check could
// not be evaluated at all" are different facts, and the old CTRL ack merged
// them: an unparsable token and a false assertion both emitted
// `status=FAIL ... fail=1`. A peek: at a handler-backed address, an unknown
// field, a value that will not parse, an SHR field on a surface with no Mega II
// image -- none of those are a false assertion. They are a question the
// instrument cannot answer, and they now say so in their own ack word,
// `assert-unevaluated`.
//
// Unevaluated is still NOT a pass. Turning "I could not check that" into green
// is how a gate measures nothing and reports success.
// ============================================================================
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "cpu.hpp"
#include "iigs_diag.hpp"   // iigs_assert_value / _parse_value / _fmt_num (env rail)

namespace a2assert {

enum class Verdict { Pass, Fail, Unevaluated };

struct Tally { int pass = 0, fail = 0, unevaluated = 0; };

// Fields that read the SHR window or the spike's byte statistics. A live CTRL
// session on a non-IIgs has no Mega II bank-$E1 image, and iigs_assert_value
// would dereference the null e1 for these -- so they are refused BEFORE the
// call rather than crashing inside it. nonzero/distinct are computed by the
// spike over its SHR window and simply do not exist on this surface; answering
// them with a made-up 0 would be the floating-bus mistake in a second place.
inline bool field_needs_shr(const char *name) {
    return !strcmp(name, "scb_mode") ||
           (name[0]=='i' && name[1]=='d' && name[2]=='x' &&
            name[3]>='0' && name[3]<='9');
}
inline bool field_is_spike_stat(const char *name) {
    return !strcmp(name, "nonzero") || !strcmp(name, "distinct");
}

// A peek: ADDRESS THAT DOES NOT FULLY PARSE IS REFUSED, NOT ROUNDED DOWN.
// iigs_assert_value reads the address with strtoul(name+5, nullptr, 16) and
// keeps whatever prefix parsed: `peek:FF:FA62` silently becomes $0000FF and
// `peek:zzz` silently becomes $000000. Both then assert confidently against
// the wrong byte, which is the same class of defect as asserting against the
// floating bus -- an answer where there should have been a refusal.
//
// The old CTRL verb did accept a `peek:BANK:ADDR` spelling via sscanf. It is
// NOT reinstated here: the env rail cannot read it, and quietly making one
// surface understand a syntax the other mis-parses would rebuild the exact
// divergence this file exists to delete. One 24-bit address, both surfaces.
// (The env rail's own silent mis-parse of these forms is still there and wants
// the same guard; it could not be fixed from here without colliding with work
// in flight on that file.)
inline bool peek_addr_is_whole(const char *name) {
    const char *a = name + 5;
    if (!*a) return false;
    int n = 0;
    for (const char *q = a; *q; q++, n++)
        if (!isxdigit((unsigned char)*q)) return false;
    return n <= 6;
}

// One clause. `why` receives the human line for anything that is not a plain
// PASS; a PASS writes nothing, because an ack that recites what went right
// buries what went wrong.
inline Verdict eval_clause(const char *tok, cpu_state *cpu, const uint8_t *e1,
                           char *why, size_t whysz) {
    const char *op = nullptr;
    int oplen = 0;
    for (const char *q = tok; *q; q++) {
        if ((q[0]=='='||q[0]=='!'||q[0]=='<'||q[0]=='>') && q[1]=='=') { op = q; oplen = 2; break; }
        if (q[0]=='<' || q[0]=='>') { op = q; oplen = 1; break; }
    }
    if (!op) {
        // Dropped in silence by both surfaces once. The single-`=` typo
        // `qd_carry=0` is exactly this clause, and a spec made only of those
        // used to report zero checks and call it a pass.
        snprintf(why, whysz, " '%s' has no comparison operator (== != <= >= < >)", tok);
        return Verdict::Unevaluated;
    }
    char name[64];
    int nl = (int)(op - tok);
    if (nl > 63) nl = 63;
    memcpy(name, tok, (size_t)nl);
    name[nl] = 0;
    while (nl > 0 && name[nl-1] == ' ') name[--nl] = 0;
    char ops[3] = {0};
    memcpy(ops, op, (size_t)oplen);
    const char *vs = op + oplen;
    while (*vs == ' ') vs++;
    if (!*vs) {
        snprintf(why, whysz, " '%s %s' has no value", name, ops);
        return Verdict::Unevaluated;
    }

    bool expl = false, ambig = false;
    long val = iigs_assert_parse_value(vs, &expl, &ambig);
    // SAME RULE AS THE ENV RAIL, deliberately: a peek: comparand is one byte and
    // a byte's written form is hex, so `peek:FFFA62==22` could mean $22 or 34 and
    // nothing in the text says which. Refuse rather than pick a base for the
    // author. Scoped to peek: because scb_mode==640 and idx6==28800 are decimal
    // by nature and a blanket refusal would break every documented example.
    if (!strncmp(name, "peek:", 5) && !expl && ambig) {
        snprintf(why, whysz, " '%s %s %s' ambiguous base: write $%s or 0d%s",
                 name, ops, vs, vs, vs);
        return Verdict::Unevaluated;
    }
    if (!strncmp(name, "peek:", 5) && !peek_addr_is_whole(name)) {
        snprintf(why, whysz, " '%s' is not a whole 24-bit hex address "
                             "(write peek:HHHHHH, max 6 hex digits, no BANK:ADDR)", name);
        return Verdict::Unevaluated;
    }
    if (field_needs_shr(name) && !e1) {
        snprintf(why, whysz, " '%s' needs the Mega II bank-$E1 image, which this "
                             "machine does not have", name);
        return Verdict::Unevaluated;
    }
    if (field_is_spike_stat(name)) {
        snprintf(why, whysz, " '%s' is an SHR-window statistic the spike computes; "
                             "the live rail does not", name);
        return Verdict::Unevaluated;
    }

    bool ok = true;
    long actual = iigs_assert_value(name, cpu, e1, -1, -1, &ok);
    char vb[48], ab[48];
    iigs_assert_fmt_num(vb, sizeof vb, val);
    iigs_assert_fmt_num(ab, sizeof ab, actual);
    if (!ok) {
        // A sentinel came back, not a measurement. It must never satisfy a
        // comparison: that is how `bogus_field != 5` used to report PASS. The
        // detail of WHY reached stdout from iigs_assert_value (and the session
        // log with it); the ack says which clause, so the two can be joined.
        snprintf(why, whysz, " '%s %s %s' UNEVALUATED (see log)", name, ops, vb);
        return Verdict::Unevaluated;
    }

    int pass = 0;
    if      (!strcmp(ops,"==")) pass = (actual == val);
    else if (!strcmp(ops,"!=")) pass = (actual != val);
    else if (!strcmp(ops,"<=")) pass = (actual <= val);
    else if (!strcmp(ops,">=")) pass = (actual >= val);
    else if (!strcmp(ops,"<"))  pass = (actual <  val);
    else if (!strcmp(ops,">"))  pass = (actual >  val);
    if (pass) { why[0] = 0; return Verdict::Pass; }
    snprintf(why, whysz, " FAIL %s %s %s (actual=%s)", name, ops, vb, ab);
    return Verdict::Fail;
}

// Whole spec. `detail` accumulates only the clauses that were not a plain PASS.
inline Tally eval(const char *spec, cpu_state *cpu, const uint8_t *e1,
                  char *detail, size_t detailsz) {
    Tally t;
    size_t dlen = 0;
    if (detail && detailsz) detail[0] = 0;
    char buf[512];
    strncpy(buf, spec ? spec : "", sizeof buf - 1);
    buf[sizeof buf - 1] = 0;
    for (char *tok = strtok(buf, ";,"); tok; tok = strtok(nullptr, ";,")) {
        while (*tok == ' ') tok++;
        size_t tl = strlen(tok);
        while (tl > 0 && tok[tl-1] == ' ') tok[--tl] = 0;
        if (!*tok) continue;
        char why[256] = {0};
        Verdict v = eval_clause(tok, cpu, e1, why, sizeof why);
        if      (v == Verdict::Pass)        t.pass++;
        else if (v == Verdict::Fail)        t.fail++;
        else                                t.unevaluated++;
        if (why[0] && detail && dlen + strlen(why) + 1 < detailsz) {
            memcpy(detail + dlen, why, strlen(why) + 1);
            dlen += strlen(why);
        }
    }
    return t;
}

// The ack. THE TWO VERDICTS THE OLD ONE MERGED ARE NOW SEPARATE WORDS, so a
// harness can tell "your expectation was wrong" from "I could not check that".
inline void format_ack(const Tally &t, const char *detail, char *result, size_t rsz) {
    const char *d = detail ? detail : "";
    if (t.pass + t.fail + t.unevaluated == 0) {
        // Same rule the env rail applies: a spec that produced no checks is not
        // a pass. `assert` with nothing after it used to answer PASS pass=0.
        snprintf(result, rsz, "status=FAIL assert-unevaluated n=0 pass=0 fail=0 "
                              "-- spec produced 0 checks; an assert that evaluates "
                              "nothing is not a pass");
    } else if (t.unevaluated) {
        snprintf(result, rsz, "status=FAIL assert-unevaluated n=%d pass=%d fail=%d "
                              "path=probe_peek%s",
                 t.unevaluated, t.pass, t.fail, d);
    } else if (t.fail) {
        snprintf(result, rsz, "status=FAIL assert pass=%d fail=%d path=probe_peek%s",
                 t.pass, t.fail, d);
    } else {
        snprintf(result, rsz, "status=PASS assert pass=%d fail=0 path=probe_peek", t.pass);
    }
}

} // namespace a2assert
