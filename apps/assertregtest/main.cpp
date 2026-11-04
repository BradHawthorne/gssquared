// assertregtest -- `reg:` fields of the assert evaluator, on both surfaces.
//
// The CTRL `assert` verb (a2assert::eval) and the A2GSPU_ASSERT env rail
// (iigs_eval_asserts) share iigs_assert_value, so a register field must give
// the same verdict on both. Each case below states its expected verdict by
// hand and is run through both evaluators against one CPU state:
//   * every register, by value (pass and fail);
//   * masked flags (carry, m, x) with an explicit-base mask;
//   * the refusals: unknown register, empty or base-less mask, no CPU --
//     each must come back UNEVALUATED, never as a pass.
#include <SDL3/SDL.h>

#include "gs2.hpp"
#include "cpu.hpp"
#include "a2gspu_assert.hpp"

#include <cstdio>
#include <string>

gs2_app_t gs2_app_inues;
uint64_t debug_level = 0;

enum Want { PASS, FAIL, UNEVAL };

static int failures = 0;

static void one(cpu_state *cpu, const char *spec, Want want) {
    char detail[600] = {0};
    a2assert::Tally t = a2assert::eval(spec, cpu, nullptr, detail, sizeof detail);
    Want got = t.unevaluated ? UNEVAL : t.fail ? FAIL : PASS;
    if (t.pass + t.fail + t.unevaluated == 0) got = UNEVAL;
    // The env rail has two outcomes (0 pass / 1 not a pass); UNEVAL and FAIL
    // both map to 1 there, and that is the contract to hold it to.
    int env = iigs_eval_asserts(spec, cpu, nullptr, 0, 0);
    bool env_ok = (want == PASS) ? env == 0 : env == 1;
    if (got != want || !env_ok) {
        static const char *nm[] = {"PASS", "FAIL", "UNEVALUATED"};
        std::fprintf(stderr, "FAIL: '%s' ctrl=%s env=%d want=%s%s\n", spec, nm[got], env, nm[want], detail);
        ++failures;
    }
}

int main() {
    cpu_state cpu(PROCESSOR_65816);
    cpu.E = 0;
    cpu.a = 0x1234; cpu.x = 0x0056; cpu.y = 0xBEEF;
    cpu.sp = 0x01F3; cpu.d = 0x0300;
    cpu.p = 0x21;          // m=1 (bit 5), x=0, C=1
    cpu.full_db = 0; cpu.db = 0x7E;
    cpu.full_pc = 0x021000;

    one(&cpu, "reg:a==$1234", PASS);
    one(&cpu, "reg:a==$1235", FAIL);
    one(&cpu, "reg:a!=$1235", PASS);
    one(&cpu, "reg:x==$0056", PASS);
    one(&cpu, "reg:y==$BEEF", PASS);
    one(&cpu, "reg:s==$01F3", PASS);
    one(&cpu, "reg:d==$0300", PASS);
    one(&cpu, "reg:p==$21", PASS);
    one(&cpu, "reg:dbr==$7E", PASS);
    one(&cpu, "reg:dbr==$7F", FAIL);
    one(&cpu, "reg:pbr==$02", PASS);
    one(&cpu, "reg:pc==$1000", PASS);
    one(&cpu, "reg:e==0", PASS);
    // masked flags
    one(&cpu, "reg:p&$01==$01", PASS);   // carry set
    one(&cpu, "reg:p&$20==$20", PASS);   // m=1
    one(&cpu, "reg:p&$10==0", PASS);     // x=0
    one(&cpu, "reg:p&$10==$10", FAIL);
    one(&cpu, "reg:p&0x02==0", PASS);    // Z clear, 0x spelling
    one(&cpu, "reg:a&0d255==0d52", PASS);// decimal mask, $34 = 52
    // several clauses, one false: the whole spec is not a pass
    one(&cpu, "reg:a==$1234;reg:p&$01==$01;reg:dbr==$7E", PASS);
    one(&cpu, "reg:a==$1234;reg:p&$01==0", FAIL);
    // refusals
    one(&cpu, "reg:q==1", UNEVAL);
    one(&cpu, "reg:==1", UNEVAL);
    one(&cpu, "reg:p&==1", UNEVAL);
    one(&cpu, "reg:p&10==0", UNEVAL);    // mask with no base
    one(&cpu, "reg:p&$0G==0", UNEVAL);   // mask not a number
    one(nullptr, "reg:a==$1234", UNEVAL);
    // emulation flag
    cpu.E = 1;
    one(&cpu, "reg:e==1", PASS);

    if (failures) { std::fprintf(stderr, "%d case(s) failed\n", failures); return 1; }
    std::printf("assertregtest: OK\n");
    return 0;
}
