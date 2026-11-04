#pragma once
// a2gspu_setreg.hpp -- the register table behind the CTRL `setreg` verb.
//
// `setreg <reg> <hex>` splices one CPU register. The bank registers were not
// reachable: the program bank only through a 24-bit `setreg pc`, the data bank
// not at all. After `run`, the ROM leaves DBR wherever its last code put it
// (on the IIgs typically $E1), so a spliced program's absolute loads and stores
// went to that bank, and the only workaround was a PEA/PLB prologue planted in
// front of the program -- extra bytes and cycles in every measurement.
//
// Since protocol minor 2 (`features setreg-banks`):
//   setreg dbr <hex>   data bank register, $00-$FF
//   setreg pbr <hex>   program bank register, $00-$FF (PC low 16 bits kept)
// Out-of-range values for these two are refused, not truncated.
//
// Kept apart from the dispatcher so apps/setregtest can check it without a
// running machine.

#include <cstdio>
#include <cstring>
#include "cpu.hpp"

namespace a2setreg {

enum class Result { Ok, Unknown, Range };

inline Result apply(cpu_state *c, const char *rn, unsigned long v) {
    if      (!strcmp(rn, "a"))   c->a  = (uint16_t)v;
    else if (!strcmp(rn, "x"))   c->x  = (uint16_t)v;
    else if (!strcmp(rn, "y"))   c->y  = (uint16_t)v;
    else if (!strcmp(rn, "sp"))  c->sp = (uint16_t)v;
    else if (!strcmp(rn, "p"))   c->p  = (uint8_t)v;
    else if (!strcmp(rn, "d"))   c->d  = (uint16_t)v;
    else if (!strcmp(rn, "pc"))  c->full_pc = (uint32_t)(v & 0xFFFFFF);
    else if (!strcmp(rn, "dbr")) { if (v > 0xFF) return Result::Range; c->db = (uint8_t)v; }
    else if (!strcmp(rn, "pbr")) { if (v > 0xFF) return Result::Range; c->pb = (uint8_t)v; }
    // THE EMULATION FLAG, because there was no way to set it and it is
    // needed constantly. `run <n>` advances n VIDEO FRAMES and leaves the
    // IIgs ROM wherever it happened to be -- which is NATIVE mode, a
    // state a 128K Apple II cannot be in. Every harness that splices 6502
    // code in after a `run` inherits it, and slot firmware called from
    // there does not survive. The workaround was to poke `38 FB 4C ..`
    // (SEC / XCE / JMP) and run it, which is three verbs and a scratch
    // address to express one bit.
    //
    // Entering emulation mode does MORE than set the bit: XCE forces M
    // and X to 1 and truncates the index and stack high bytes. Setting
    // E alone would leave 16-bit registers live in a mode that has none,
    // which is not a state the CPU can reach and not one worth being
    // able to fake. Mirrored from OP_XCE_IMP in base_6502.cpp.
    else if (!strcmp(rn, "e")) {
        c->E = (v & 1) ? 1 : 0;
        if (c->E) { c->x_hi = 0; c->y_hi = 0; c->sp_hi = 0x01;
                    c->_M = 1;  c->_X = 1; }
    }
    else return Result::Unknown;
    return Result::Ok;
}

// The ack text, unchanged for the registers that existed before minor 2.
inline void format_ack(Result r, const char *rn, unsigned long v, char *result, size_t rsz) {
    if (r == Result::Ok)
        snprintf(result, rsz, "status=OK setreg %s=$%lX (DELIBERATE splice)", rn, v);
    else if (r == Result::Range)
        snprintf(result, rsz, "status=FAIL setreg-range %s=$%lX max=$FF", rn, v);
    else
        snprintf(result, rsz, "status=FAIL setreg-unknown-%s", rn);
}

} // namespace a2setreg
