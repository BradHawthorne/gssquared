// LOOPA-07: a clause that begins past byte 511 of the spec must be evaluated.
// Both rails used to copy the spec into a 512-byte buffer and drop the rest,
// so a failing clause past that point never ran and the summary was ALL PASS.
#include "a2gspu_assert.hpp"

#include <cstdio>
#include <string>

static std::string long_spec() {
    // 511 is prime, so a fixed clause cannot end on the old buffer boundary.
    // Fill with passing clauses, then spaces, so byte 511 starts the failure
    // and the truncated buffer holds only passing clauses.
    std::string spec;
    const std::string pass = "gsos_err==0;";
    while (spec.size() + pass.size() <= 511)
        spec += pass;
    while (spec.size() < 511)
        spec += ' ';
    spec += "gsos_err==1";
    return spec;
}

int main() {
    const std::string spec = long_spec();
    if (spec.size() <= 511 || spec.find("nonzero==1") < 511) {
        std::fprintf(stderr, "fixture does not place the failing clause past byte 511\n");
        return 2;
    }
    const int env = iigs_eval_asserts(spec.c_str(), nullptr, nullptr, 0, 0);
    char detail[256] = {};
    const a2assert::Tally ctrl = a2assert::eval(spec.c_str(), nullptr, nullptr,
                                                detail, sizeof detail);
    const int short_fail = iigs_eval_asserts("gsos_err==1", nullptr, nullptr, 0, 0);
    if (env == 0 || ctrl.fail < 1 || short_fail == 0) {
        std::fprintf(stderr,
                     "FAIL clause past byte 511 was not a failure "
                     "(env=%d ctrl_pass=%d ctrl_fail=%d short=%d len=%zu)\n",
                     env, ctrl.pass, ctrl.fail, short_fail, spec.size());
        return 1;
    }
    std::printf("PASS assert length %zu env=%d ctrl_fail=%d\n",
                spec.size(), env, ctrl.fail);
    return 0;
}
