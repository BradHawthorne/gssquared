// setregtest -- the register table behind the CTRL `setreg` verb
// (src/a2gspu_setreg.hpp), checked without a running machine.
//
// What it pins down:
//   * `dbr` and `pbr` set the bank registers and nothing else (PC low 16 bits,
//     the other bank, A/X/Y untouched);
//   * values above $FF for the two bank registers are refused, not truncated,
//     and leave the register as it was;
//   * the registers that existed before keep their behaviour, including the
//     emulation-flag rule (E=1 forces M=X=1, XH=YH=0, SH=$01);
//   * the exact ack text of the three outcomes.
#include <SDL3/SDL.h>

#include "gs2.hpp"
#include "cpu.hpp"
#include "a2gspu_setreg.hpp"

#include <cstdio>
#include <string>

gs2_app_t gs2_app_inues;
uint64_t debug_level = 0;

static int failures = 0;
static void check(bool ok, const std::string &what) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", what.c_str()); ++failures; }
}

int main() {
    cpu_state cpu(PROCESSOR_65816);
    cpu.E = 0;
    cpu.full_pc = 0x001234;
    cpu.full_db = 0;
    cpu.a = 0x1111; cpu.x = 0x2222; cpu.y = 0x3333;

    using a2setreg::Result;
    check(a2setreg::apply(&cpu, "dbr", 0x7F) == Result::Ok, "setreg dbr 7F accepted");
    check(cpu.db == 0x7F, "dbr is $7F");
    check(cpu.pb == 0x00 && cpu.pc == 0x1234, "dbr leaves PBR:PC alone");

    check(a2setreg::apply(&cpu, "pbr", 0x02) == Result::Ok, "setreg pbr 02 accepted");
    check(cpu.pb == 0x02, "pbr is $02");
    check(cpu.pc == 0x1234, "pbr keeps the PC low 16 bits");
    check((cpu.full_pc & 0xFFFFFF) == 0x021234, "full PC is 02:1234");
    check(cpu.db == 0x7F, "pbr leaves DBR alone");
    check(cpu.a == 0x1111 && cpu.x == 0x2222 && cpu.y == 0x3333, "bank setters leave A/X/Y alone");

    check(a2setreg::apply(&cpu, "dbr", 0x100) == Result::Range, "setreg dbr 100 refused");
    check(cpu.db == 0x7F, "refused dbr leaves the register as it was");
    check(a2setreg::apply(&cpu, "pbr", 0x1FF) == Result::Range, "setreg pbr 1FF refused");
    check(cpu.pb == 0x02, "refused pbr leaves the register as it was");

    check(a2setreg::apply(&cpu, "k", 0x01) == Result::Unknown, "unknown register name refused");

    // Registers that existed before keep their behaviour.
    check(a2setreg::apply(&cpu, "pc", 0x03ABCD) == Result::Ok && cpu.pb == 0x03 && cpu.pc == 0xABCD,
          "setreg pc still takes a 24-bit value");
    check(a2setreg::apply(&cpu, "e", 1) == Result::Ok && cpu.E == 1 && cpu.x_hi == 0 && cpu.y_hi == 0 &&
          cpu.sp_hi == 0x01 && cpu._M == 1 && cpu._X == 1, "setreg e 1 applies the emulation invariant");

    // Exact replies (hand-written).
    char buf[160];
    a2setreg::format_ack(Result::Ok, "dbr", 0x7F, buf, sizeof buf);
    check(std::string(buf) == "status=OK setreg dbr=$7F (DELIBERATE splice)", std::string("ok ack: ") + buf);
    a2setreg::format_ack(Result::Range, "dbr", 0x100, buf, sizeof buf);
    check(std::string(buf) == "status=FAIL setreg-range dbr=$100 max=$FF", std::string("range ack: ") + buf);
    a2setreg::format_ack(Result::Unknown, "k", 1, buf, sizeof buf);
    check(std::string(buf) == "status=FAIL setreg-unknown-k", std::string("unknown ack: ") + buf);

    if (failures) { std::fprintf(stderr, "%d check(s) failed\n", failures); return 1; }
    std::printf("setregtest: OK\n");
    return 0;
}
