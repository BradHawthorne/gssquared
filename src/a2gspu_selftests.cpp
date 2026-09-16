/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// CPU/MMU micro-tests -- moved out of gs2.cpp; see gs2_app.hpp.
#include "gs2_app.hpp"


// ===========================================================================
// Env-gated headless CPU micro-test suite (A2GSPU_CPUTEST=<name>).
//
// Each case runs the REAL instruction stepper (cpu->cpun->execute_next, the
// 65816 mode-dispatcher) against a hand-built scenario in bank-0 RAM, then
// exits with a pass/fail code. No GUI, no test ROM, no device dependency — the
// same headless exit-code contract the boot/assert gate uses, applied to a
// single CPU corner case. Scenarios are driven with REAL opcodes injected in
// RAM (XCE/REP/SEP/etc.) so the dispatcher selects the correct width core just
// as it does in normal execution; this is what gives the tests their teeth.
//
// This suite is the regression net for the CPU-accuracy contracts the project's
// behavioral oracle (and every golden derived from it) silently depends on. A
// future edit that breaks one of these corners flips the named test to FAIL
// instead of corrupting goldens undetected. Each case below is teeth-proven:
// it PASSES with the accuracy fix in place and FAILS if the fix is reverted.
//
// Cases (each a separate A2GSPU_CPUTEST value):
//   wai_wake        WAI wakes on a MASKED pending IRQ (I gates servicing,
//                   not the wake); the masked IRQ is not serviced.
//   plp_native_x    PLP in native mode with the pulled X(index-8) bit set
//                   forces the index high bytes to $00.
//   rti_native_x    same contract via RTI's pull path.
//   dp_wrap_dl0     emulation-mode (d) indirect with DL==0 wraps the pointer
//                   high-byte fetch inside the zero page ($00FF -> $0000).
//   dec_z_816       65816 decimal-mode ADC sets Z from the BCD result (A==0).
//   dec_z_nmos      NMOS 6502 decimal-mode ADC sets Z from the BINARY sum
//                   A+M+C (the M4 +C fix); contrasts the 65816 contract.
//   branch_cyc      native-mode taken branch across a page = flat 3 cycles
//                   (no NMOS page-cross +1 penalty).
//   jmp_ind_816     65816 JMP ($xxFF) reads the vector high byte from the NEXT
//                   page (no page-boundary bug).
//   jmp_ind_nmos    NMOS 6502 JMP ($xxFF) DOES wrap inside the page (the bug).
// ===========================================================================

// Drive the dispatched 65816 from cold emulation state into native mode with
// 16-bit index registers (E=0, X=0). Returns with the machine parked so the
// caller can inject and run its own opcodes from PROG.
//
// Program at PROG (emulation cold state):  18 FB        CLC ; XCE  -> native
//                                          C2 10        REP #$10  -> X=0 (16b idx)
static void microtest_go_native_x16(cpu_state *cpu, MMU *mmu, uint32_t PROG) {
    mmu->write(PROG + 0, 0x18);   // CLC
    mmu->write(PROG + 1, 0xFB);   // XCE  -> E=C=0 : native mode
    mmu->write(PROG + 2, 0xC2);   // REP #imm
    mmu->write(PROG + 3, 0x10);   //  #$10 -> clear X flag (16-bit index)
    // Full cold-state reset so a prior case cannot contaminate this one when the
    // suite is run back-to-back via A2GSPU_CPUTEST=all (each case is independent).
    cpu->p = 0x00; cpu->d = 0x0000; cpu->a = 0x0000; cpu->sp = 0x01FF;
    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;     // cold emulation defaults (after p=0)
    cpu->pb = 0x00; cpu->db = 0x00; cpu->full_db = 0;
    cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;
    // Exactly three instructions: CLC, XCE, REP #$10. (Running a 4th step here
    // would prematurely execute the caller's first real opcode at PROG+4.)
    for (int i = 0; i < 3; i++) (cpu->cpun->execute_next)(cpu);  // CLC, XCE, REP
}

// --- PLP / RTI native-mode index-high clear (contract 1) -------------------
// In native mode with 16-bit index (X=0) and non-zero index high bytes, pulling
// a P with the X(index-8) bit SET must force x_hi/y_hi to $00 (the real 65816
// mirrors the SEP/XCE width-narrowing). Reverting the fix leaves x_hi/y_hi at
// their pre-set values -> FAIL. via_rti selects RTI's pull path vs PLP's.
static int microtest_native_x_pull(cpu_state *cpu, MMU *mmu, bool via_rti) {
    const uint32_t PROG = 0x001000;
    microtest_go_native_x16(cpu, mmu, PROG);    // native, M=8bit (A_lo), X=16bit
    bool native16 = (cpu->E == 0 && cpu->_X == 0);

    // The bytes to be pulled are pushed with REAL stack instructions (PHA) so the
    // push and the later pull use the identical SP-relative mapping regardless of
    // the alt-ZP / language-card soft-switch state a prior case may have left set.
    // P value to be pulled: the X index-width bit (bit 4, $10) set => narrow the
    // index registers to 8-bit. (In native P, bit 4 is the X flag.)
    const uint8_t pulled_p = 0x10;       // X=1, everything else clear

    const char *opname;
    uint32_t at = PROG + 4;
    if (!via_rti) {
        // LDA #pulled_p ; PHA ; (dirty x/y hi) ; PLP
        mmu->write(at++, OP_LDA_IMM); mmu->write(at++, pulled_p);
        mmu->write(at++, OP_PHA_IMP);
        mmu->write(at++, OP_PLP_IMP);
        cpu->pc = (uint16_t)(PROG + 4);
        (cpu->cpun->execute_next)(cpu);  // LDA
        (cpu->cpun->execute_next)(cpu);  // PHA  (push P onto the real stack)
        cpu->x_hi = 0xAA; cpu->y_hi = 0xBB;   // dirty AFTER the push, before the pull
        (cpu->cpun->execute_next)(cpu);  // PLP  (pulls the byte we just pushed)
        opname = "plp_native_x";
    } else {
        // Push the RTI frame with PHA in reverse pull order: PB, PCH, PCL, P.
        // RTI pulls P (top), then PCL, PCH, then PB.
        const uint8_t frame[4] = { 0x00, 0x20, 0x00, pulled_p }; // PB, PCH, PCL, P
        for (int i = 0; i < 4; i++) {
            mmu->write(at++, OP_LDA_IMM); mmu->write(at++, frame[i]);
            mmu->write(at++, OP_PHA_IMP);
        }
        uint32_t rti_at = at;
        mmu->write(at++, OP_RTI_IMP);
        cpu->pc = (uint16_t)(PROG + 4);
        for (int i = 0; i < 8; i++) (cpu->cpun->execute_next)(cpu);  // 4x (LDA;PHA)
        cpu->x_hi = 0xAA; cpu->y_hi = 0xBB;   // dirty before the RTI pull
        cpu->pc = (uint16_t)rti_at;
        (cpu->cpun->execute_next)(cpu);  // RTI
        opname = "rti_native_x";
    }

    bool x_narrowed = (cpu->_X == 1);     // the pulled P actually set X
    int ok = native16 && x_narrowed && (cpu->x_hi == 0x00) && (cpu->y_hi == 0x00);
    printf("CPUTEST %s: native16=%d x_set=%d x_hi=%02X y_hi=%02X -- %s\n",
           opname, native16, x_narrowed, cpu->x_hi, cpu->y_hi, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- emulation-mode (d) indirect DL=0 page wrap (contract 2) ----------------
// In emulation mode with the Direct-page low byte == 0, the (d) indirect
// pointer's high-byte fetch wraps INSIDE the zero page ($00FF -> $0000), not
// across into $0100. We build a pointer whose low byte is at $00FF: wrap-correct
// reads the high byte from $0000, the (reverted) bug reads it from $0100. The
// two candidate pointers address two different bytes; A distinguishes them.
static int microtest_dp_wrap_dl0(cpu_state *cpu, MMU *mmu) {
    const uint32_t PROG = 0x001000;
    // pointer low at $00FF; wrap-correct high at $0000 -> ptr=$1234
    //                       buggy        high at $0100 -> ptr=$9934
    mmu->write(0x0000FF, 0x34);          // pointer low byte
    mmu->write(0x000000, 0x12);          // wrap-correct high byte
    mmu->write(0x000100, 0x99);          // buggy (cross-page) high byte
    mmu->write(0x001234, 0x42);          // value at the wrap-correct target
    mmu->write(0x009934, 0x77);          // value at the buggy target
    // LDA (d) with d-operand $FF, in emulation mode, D(irect)=$0000.
    mmu->write(PROG + 0, OP_LDA_IND);    // B2  LDA (d)
    mmu->write(PROG + 1, 0xFF);          //  (d)=$FF
    mmu->write(PROG + 2, OP_STP_IMP);    // DB  STP

    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;   // emulation mode (cold)
    cpu->d = 0x0000;                        // DL == 0 (the quirk condition)
    cpu->pb = 0x00; cpu->db = 0x00; cpu->full_db = 0;
    cpu->a = 0x0000; cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;

    (cpu->cpun->execute_next)(cpu);      // LDA (d)
    uint8_t a = cpu->a_lo;
    int ok = (a == 0x42);                // wrap-correct value loaded
    printf("CPUTEST dp_wrap_dl0: a=%02X (want 42 wrap / 77 buggy) -- %s\n",
           a, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- decimal-mode Z flag (contract 3) ---------------------------------------
// The 65816 sets the decimal-mode Z from the BCD result (A==0). The NMOS 6502
// sets it from the raw BINARY sum A+M+C (carry included — the M4 fix added the
// +C). We pick operands where the two disagree: A=$99, M=$01, C=0, decimal.
//   BCD:    $99 + $01 = $00 with carry  -> 65816 Z=1 (result A is $00)
//   binary: $99 + $01 + 0 = $9A != 0    -> NMOS   Z=0
// Without the M4 +C the NMOS binary sum can still differ; we additionally drive
// a carry-in case below to pin the +C specifically.
//
// 65816 path runs on the live dispatched core. NMOS path runs on a transient
// 6502 core (the project's oracle never uses NMOS, but the fix's +C lives on
// that path, so the teeth require it). The transient core shares this CPU's
// mmu/clock and is destroyed on return; it does not perturb the machine state
// that boot/golden depend on (this test is env-gated off the boot path).
static int microtest_dec_z_816(cpu_state *cpu, MMU *mmu) {
    const uint32_t PROG = 0x001000;
    microtest_go_native_x16(cpu, mmu, PROG);    // native; M still 8-bit (REP #$10 only)
    // SED ; LDA #$99 ; ADC #$01   (8-bit A, decimal)
    mmu->write(PROG + 4, OP_SED_IMP);   // F8 SED
    mmu->write(PROG + 5, OP_LDA_IMM);   // A9
    mmu->write(PROG + 6, 0x99);
    mmu->write(PROG + 7, OP_ADC_IMM);   // 69
    mmu->write(PROG + 8, 0x01);
    mmu->write(PROG + 9, OP_STP_IMP);   // DB
    cpu->C = 0; cpu->pc = (uint16_t)(PROG + 4);
    for (int i = 0; i < 4 && !cpu->clock_stopped; i++) (cpu->cpun->execute_next)(cpu);
    // BCD: $99 + $01 = $00, carry out. 65816: A==$00 -> Z=1.
    int ok = (cpu->a_lo == 0x00) && (cpu->Z == 1) && (cpu->C == 1);
    printf("CPUTEST dec_z_816: a=%02X Z=%d C=%d (want a=00 Z=1 C=1) -- %s\n",
           cpu->a_lo, cpu->Z, cpu->C, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int microtest_dec_z_nmos(cpu_state *cpu, MMU *mmu, NClock *clk) {
    const uint32_t PROG = 0x001000;
    // Build a transient NMOS 6502 core that shares this machine's clock.
    std::unique_ptr<BaseCPU> nmos = createCPU(PROCESSOR_6502, clk);
    if (!nmos) { printf("CPUTEST dec_z_nmos: no NMOS core -- FAIL\n"); return 1; }
    BaseCPU *saved = cpu->cpun.release();   // park the live dispatcher
    cpu->cpun.reset(nmos.release());
    BaseCPU *saved_core = cpu->core;
    cpu->core = cpu->cpun.get();

    // SED ; SEC ; LDA #$98 ; ADC #$01   (binary A+M+C = $98+$01+1 = $9A != 0)
    //   The M4 fix counts the carry-in: without +C the binary sum is $99 ->
    //   still != 0 here, so we also assert the carry-included branch via a
    //   second operand set that is zero ONLY with the +C: A=$98 M=$01 C=0 ...
    // We instead use the cleanest discriminator of the +C term:
    //   A=$99 M=$00 C=1, decimal:
    //     binary A+M+C = $99+$00+1 = $9A  -> Z=0  (correct, carry counted)
    //     WITHOUT +C   = $99+$00   = $99  -> Z=0  (same here)
    // That does not discriminate. Use A=$FF M=$00 C=1:
    //     with +C: $FF+$00+1 = $100 -> low byte $00 -> Z=1
    //     no  +C : $FF+$00   = $FF  -> Z=0
    // BCD result of $FF+$00+1 in decimal is implementation noise; we assert
    // ONLY Z, which the fix derives from the binary low byte being $00.
    mmu->write(PROG + 0, OP_SED_IMP);   // F8 SED
    mmu->write(PROG + 1, OP_SEC_IMP);   // 38 SEC (C=1, the carry-in)
    mmu->write(PROG + 2, OP_LDA_IMM);   // A9
    mmu->write(PROG + 3, 0xFF);
    mmu->write(PROG + 4, OP_ADC_IMM);   // 69
    mmu->write(PROG + 5, 0x00);
    mmu->write(PROG + 6, OP_NOP_IMP);   // EA (NMOS has no STP; park on NOP)

    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;
    cpu->pb = 0; cpu->db = 0; cpu->full_db = 0;
    cpu->p = 0; cpu->a = 0; cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;
    for (int i = 0; i < 5; i++) (cpu->cpun->execute_next)(cpu);  // SED SEC LDA ADC NOP

    // binary A+M+C = $FF+$00+1 = $100 -> low byte $00 -> NMOS decimal Z = 1.
    int ok = (cpu->Z == 1);
    printf("CPUTEST dec_z_nmos: Z=%d (want Z=1 from binary A+M+C carry) -- %s\n",
           cpu->Z, ok ? "PASS" : "FAIL");

    // restore the live dispatched core; drop the transient.
    cpu->cpun.reset(saved);
    cpu->core = saved_core;
    return ok ? 0 : 1;
}

// --- native-mode taken-branch cycle count (contract 4) ----------------------
// On the 65816 in native mode (E=0) a taken branch is a flat 3 cycles with NO
// page-cross penalty (the NMOS/emulation +1 for crossing a page does not apply).
// We place a BNE that is taken and crosses a page boundary, then measure the
// clock delta across exactly that one instruction. Native = 3; the reverted
// (penalty-applied) path would charge 4.
static int microtest_branch_cyc(cpu_state *cpu, MMU *mmu, NClock *clk) {
    const uint32_t PROG = 0x001000;
    microtest_go_native_x16(cpu, mmu, PROG);
    // Put the BNE so its NEXT instruction byte sits near a page end and the
    // target is on a different page. BNE at $10FE: PC after operand = $1100;
    // rel = +$10 (forward) -> target $1110, crossing from page $11 ... we need
    // the cross to be from the post-fetch PC's page. Place BNE at $10FC:
    //   opcode $10FC, operand $10FD, PC after fetch = $10FE; target = $10FE +
    //   (int8)$7F = $117D -> page $10 -> $11 cross. Z must be 0 (taken).
    const uint32_t BR = 0x0010FC;
    mmu->write(BR + 0, 0xD0);            // BNE
    mmu->write(BR + 1, 0x7F);            //  rel +$7F (forward, page-crossing)
    cpu->_Z = 0;                         // not-equal => BNE taken
    cpu->pc = (uint16_t)BR;
    uint64_t c0 = clk->get_cycles();
    (cpu->cpun->execute_next)(cpu);      // the BNE
    uint64_t c1 = clk->get_cycles();
    uint64_t cyc = c1 - c0;
    uint16_t pc = cpu->pc;
    bool crossed = ((BR + 2) & 0xFF00) != (pc & 0xFF00);
    int ok = (cyc == 3) && crossed && (pc == 0x117D);
    printf("CPUTEST branch_cyc: cycles=%llu pc=%04X crossed=%d (want 3, no +1) -- %s\n",
           (unsigned long long)cyc, pc, crossed, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- JMP (indirect) page-boundary behavior (contract 5) ---------------------
// 65816: JMP ($xxFF) fetches the vector high byte from the NEXT page (correct).
// NMOS 6502: it WRAPS inside the page (the classic bug). We lay a vector whose
// low byte is at $20FF; correct high comes from $2100, the bug reads $2000.
static int microtest_jmp_ind(cpu_state *cpu, MMU *mmu, bool nmos, NClock *clk) {
    const uint32_t PROG = 0x001000;
    const uint32_t VECLO = 0x0020FF;     // pointer low byte
    mmu->write(VECLO, 0xCD);             // vector low = $CD
    mmu->write(0x002100, 0xAB);          // NEXT-page high  -> $ABCD (816 correct)
    mmu->write(0x002000, 0x12);          // SAME-page high  -> $12CD (NMOS bug)
    mmu->write(PROG + 0, OP_JMP_IND);    // 6C  JMP ($20FF)
    mmu->write(PROG + 1, 0xFF);
    mmu->write(PROG + 2, 0x20);

    BaseCPU *saved = nullptr, *saved_core = nullptr;
    std::unique_ptr<BaseCPU> nmoscore;
    if (nmos) {
        nmoscore = createCPU(PROCESSOR_6502, clk);
        if (!nmoscore) { printf("CPUTEST jmp_ind_nmos: no NMOS core -- FAIL\n"); return 1; }
        saved = cpu->cpun.release();
        cpu->cpun.reset(nmoscore.release());
        saved_core = cpu->core; cpu->core = cpu->cpun.get();
    }
    cpu->E = 1; cpu->_M = 1; cpu->_X = 1;
    cpu->pb = 0; cpu->db = 0; cpu->full_db = 0;
    cpu->pc = (uint16_t)PROG;
    cpu->I = 1; cpu->irq_asserted = false; cpu->irq_pipe = 0;
    cpu->clock_stopped = false; cpu->halt = 0; cpu->reset_asserted = 0; cpu->rdy = false;

    (cpu->cpun->execute_next)(cpu);      // the JMP (Indirect)
    uint16_t pc = cpu->pc;
    int ok;
    if (nmos) {
        ok = (pc == 0x12CD);             // page-wrapped (the bug present)
        printf("CPUTEST jmp_ind_nmos: pc=%04X (want 12CD wrapped/bug) -- %s\n",
               pc, ok ? "PASS" : "FAIL");
        cpu->cpun.reset(saved);          // restore live dispatcher
        cpu->core = saved_core;
    } else {
        ok = (pc == 0xABCD);             // next-page high (no bug)
        printf("CPUTEST jmp_ind_816: pc=%04X (want ABCD next-page/no-bug) -- %s\n",
               pc, ok ? "PASS" : "FAIL");
    }
    return ok ? 0 : 1;
}

// Program injected at bank-0 $1000 (8-bit emulation mode, the cold-boot state):
//     1000  CB        WAI
//     1001  EE 10 10  INC $1010      ; "resumed past WAI" marker -> $1010 = 1
//     1004  DB        STP            ; halt the part cleanly so the loop ends
//   $1010 is pre-zeroed; the IRQ vector $FFFE/$FFFF is pointed at a TRAP page so
//   that, if the masked IRQ were wrongly serviced, the PC would land in $FF00..
//   and the marker would never be written (caught as a FAIL).
static int microtest_wai_wake(cpu_state *cpu, MMU *mmu) {

    // --- build the scenario in RAM (write_raw bypasses IO/shadow side effects) ---
    // $1000 is bank-0 main RAM outside every IIgs shadow window ($0400-$07FF text,
    // $2000-$5FFF hires) so the injected bytes are not mirrored into $E1.
    const uint32_t PROG = 0x001000;
    const uint32_t MARK = 0x001010;
    // Use the same bus write()/read() path the stepper's fetch uses, so the
    // injected bytes are guaranteed visible at the program counter (write_raw and
    // the fetch can resolve to different page buffers depending on bank state).
    mmu->write(PROG + 0, 0xCB);              // WAI
    mmu->write(PROG + 1, 0xEE);              // INC abs
    mmu->write(PROG + 2, 0x10);
    mmu->write(PROG + 3, 0x10);              // -> $1010 (the marker)
    mmu->write(PROG + 4, 0xDB);              // STP
    mmu->write(MARK, 0x00);                  // marker starts at 0
    // Sanity: confirm the injected WAI opcode is visible to the CPU at PROG.
    uint8_t fetched = mmu->read(PROG);
    // Point the emulation-mode IRQ vector ($FFFE/$FFFF) at $FF00 and lay a STP
    // there: if the masked IRQ were (wrongly) serviced, the PC diverts into the
    // $FFxx trap instead of running INC $2010 -> the marker stays 0 -> FAIL.
    mmu->write(0x00FFFE, 0x00);
    mmu->write(0x00FFFF, 0xFF);
    mmu->write(0x00FF00, 0xDB);              // STP at the trap

    // --- set the CPU into the exact corner state ---
    cpu->halt = 0;
    cpu->clock_stopped = false;
    cpu->reset_asserted = 0;
    cpu->rdy = false;
    cpu->E = 1;                 // emulation mode (the cold-boot default)
    cpu->pb = 0x00;
    cpu->db = 0x00;
    cpu->pc = (uint16_t)PROG;
    cpu->I = 1;                 // interrupt SERVICING masked
    cpu->irq_asserted = true;   // ... but an interrupt IS pending (the wake source)
    cpu->irq_pipe = 0;          // with I=1 the pipe never latches -> no servicing

    bool saw_rdy = false;       // WAI must set RDY at least once
    bool resumed = false;       // ... then RDY must clear and execution continue
    const int STEP_CAP = 64;    // bounded: a never-waking part can't hang the test
    int steps = 0;
    for (; steps < STEP_CAP; steps++) {
        (cpu->cpun->execute_next)(cpu);
        if (cpu->rdy) saw_rdy = true;
        else if (saw_rdy) resumed = true;   // first clear after a set = the wake
        if (cpu->clock_stopped) break;      // STP reached -> program finished
    }

    uint8_t  marker = mmu->read(MARK);
    uint32_t pc24   = ((uint32_t)cpu->pb << 16) | cpu->pc;
    bool took_vector = (cpu->pc >= 0xFF00 && cpu->pc <= 0xFFFF && cpu->pb == 0);

    // PASS criteria (all must hold):
    //  - WAI raised RDY (the part actually entered the wait)         saw_rdy
    //  - RDY then cleared (it WOKE on the pending, masked interrupt)  resumed
    //  - the instruction after WAI ran (INC $2010)                    marker==1
    //  - the masked IRQ was NOT serviced (no divert to the vector)    !took_vector
    //  - the part halted within the step cap (no RDY spin)            clock_stopped
    int ok = saw_rdy && resumed && (marker == 0x01) && !took_vector && cpu->clock_stopped;

    printf("CPUTEST wai_wake: fetched=%02X saw_rdy=%d resumed=%d marker=%02X "
           "took_vector=%d stopped=%d steps=%d pc=%06X -- %s\n",
           fetched, saw_rdy, resumed, marker, took_vector, cpu->clock_stopped,
           steps, (unsigned)pc24, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// Dispatcher: A2GSPU_CPUTEST=<name> selects one case. "all" runs every case and
// fails if any one fails (the CI entry point). Unknown name -> FAIL.
int run_cpu_microtest(GS2AppState *state, const char *which) {
    computer_t *computer = state->computer;
    cpu_state  *cpu = computer->cpu;
    if (!cpu || !cpu->mmu || !cpu->cpun || !computer->clock) {
        printf("CPUTEST: machine not initialized -- FAIL\n");
        return 1;
    }
    MMU    *mmu = cpu->mmu;
    NClock *clk = (NClock *)computer->clock;   // NClockII is-a NClock

    if (strcmp(which, "wai_wake")     == 0) return microtest_wai_wake(cpu, mmu);
    if (strcmp(which, "dp_wrap_dl0")  == 0) return microtest_dp_wrap_dl0(cpu, mmu);
    if (strcmp(which, "dec_z_816")    == 0) return microtest_dec_z_816(cpu, mmu);
    if (strcmp(which, "dec_z_nmos")   == 0) return microtest_dec_z_nmos(cpu, mmu, clk);
    if (strcmp(which, "branch_cyc")   == 0) return microtest_branch_cyc(cpu, mmu, clk);
    if (strcmp(which, "plp_native_x") == 0) return microtest_native_x_pull(cpu, mmu, false);
    if (strcmp(which, "rti_native_x") == 0) return microtest_native_x_pull(cpu, mmu, true);
    if (strcmp(which, "jmp_ind_816")  == 0) return microtest_jmp_ind(cpu, mmu, false, clk);
    if (strcmp(which, "jmp_ind_nmos") == 0) return microtest_jmp_ind(cpu, mmu, true,  clk);

    if (strcmp(which, "all") == 0) {
        int fails = 0;
        fails += (microtest_wai_wake(cpu, mmu) != 0);
        fails += (microtest_native_x_pull(cpu, mmu, false) != 0);
        fails += (microtest_native_x_pull(cpu, mmu, true)  != 0);
        fails += (microtest_dp_wrap_dl0(cpu, mmu) != 0);
        fails += (microtest_dec_z_816(cpu, mmu) != 0);
        fails += (microtest_dec_z_nmos(cpu, mmu, clk) != 0);
        fails += (microtest_branch_cyc(cpu, mmu, clk) != 0);
        fails += (microtest_jmp_ind(cpu, mmu, false, clk) != 0);
        fails += (microtest_jmp_ind(cpu, mmu, true,  clk) != 0);
        printf("CPUTEST all: %d failure(s)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    printf("CPUTEST: unknown case '%s' -- FAIL\n", which);
    return 1;
}

// ===========================================================================
// Env-gated headless MMU + VIDEO micro-test suite (A2GSPU_MMUTEST=<name>).
//
// Mirrors the CPU micro-test rail (A2GSPU_CPUTEST) for the two subsystems that
// directly PRODUCE the SHR boot golden every render golden trusts: the IIgs FPI
// memory controller (main/aux steering, the SHADOW register, the $E1 bank latch,
// the language-card bank/read switches) and the Super Hi-Res pixel decode (per-
// scanline SCB 320/640 mode, the 640 dot->palette-offset mapping, and the $0RGB
// palette expansion). A silent regression in either subsystem would corrupt the
// golden undetected; these cases flip the named test to FAIL instead. Each is
// TEETH-PROVEN: it PASSES with the real mapping/decode and FAILS if the relevant
// logic is reverted. Cases are env-gated off the boot path, so the boot/CPU/
// GS-OS golden is unchanged.
//
// Cases (each a separate A2GSPU_MMUTEST value):
//   mmu_aux_steer  main/aux read+write steering: RAMRD/RAMWRT are independent;
//                  80STORE+PAGE2 steers the text page to aux regardless of RAMRD;
//                  ALTZP steers the zero page to aux. (calc_aux_read/write.)
//   mmu_shadow     the $C035 SHADOW register decode: each inhibit bit gates the
//                  matching window (text1/hgr1/SHR) for shadow-to-$E1.
//   mmu_e1_latch   the $E1 bank latch: with the latch set a bank-$E1 write lands
//                  in the Mega-II aux image (the SHR window) and reads back; the
//                  golden's SHR bytes flow through exactly this path.
//   mmu_lc_bank    language-card $C08x bank-select + read-enable: the classic
//                  double-read write-enable, bank1/bank2 select, ROM-read default.
//   vid_scb_mode   per-scanline SCB mode decode: bit7 picks 640 vs 320, changing
//                  how many palette-index pixels a line contributes to the hist.
//   vid_640_offset the 640-mode dot->palette-offset map {dot0:+8,dot1:+12,
//                  dot2:+0,dot3:+4} (the exact renderer contract).
//   all            runs every case; nonzero exit if any one fails (the CI entry).
// (The $0RGB->RGB888 palette expansion is intentionally NOT a case here — see the
//  honesty note above run_mmu_microtest: it has no teeth as a standalone test.)
// ===========================================================================

// --- main/aux read+write steering (calc_aux_read / calc_aux_write) ----------
// Drives the REAL $C00x soft-switches through the MMU's bus write path (the
// faithful 74LS259 decode) and asserts the resolved aux offset for representative
// pages. The contract has teeth on three independent axes: RAMRD vs RAMWRT must
// steer reads and writes SEPARATELY; 80STORE+PAGE2 must override RAMRD for the
// text page; ALTZP must steer the zero page. A revert that collapses any axis
// (e.g. read/write sharing one flag, or dropping the 80STORE override) flips this.
static int microtest_mmu_aux_steer(MMU_IIgs *m) {
    const uint32_t AUX = 0x1'0000, MAIN = 0x0'0000;
    auto sw = [&](uint16_t a){ m->write(a, 0x00); };   // touch a soft-switch ($C0xx)
    // Cold baseline: all main. (RAMRD/RAMWRT/80STORE/PAGE2/ALTZP/HIRES off.)
    sw(0xC000); sw(0xC002); sw(0xC004); sw(0xC008); sw(0xC054); sw(0xC056);
    int ok = 1;
    // Axis 1: RAMRD on, RAMWRT off -> a $40-page READ steers to aux, WRITE stays main.
    sw(0xC003);                                   // RAMRD on
    int a1r = (m->calc_aux_read (0x004000) == AUX);
    int a1w = (m->calc_aux_write(0x004000) == MAIN);
    sw(0xC005);                                   // RAMWRT on -> now WRITE steers too
    int a1w2 = (m->calc_aux_write(0x004000) == AUX);
    sw(0xC002); sw(0xC004);                        // both back off
    ok &= a1r && a1w && a1w2;
    // Axis 2: 80STORE + PAGE2 steers the TEXT page ($04xx) to aux even with RAMRD off.
    sw(0xC001);                                   // 80STORE on
    sw(0xC055);                                   // PAGE2 on
    int a2on = (m->calc_aux_read(0x000400) == AUX) && (m->calc_aux_write(0x000400) == AUX);
    sw(0xC054);                                   // PAGE2 off -> text page back to main
    int a2off = (m->calc_aux_read(0x000400) == MAIN) && (m->calc_aux_write(0x000400) == MAIN);
    sw(0xC000);                                   // 80STORE off
    ok &= a2on && a2off;
    // Axis 3: ALTZP steers the zero page ($00xx) both read and write.
    sw(0xC009);                                   // ALTZP on
    int a3on = (m->calc_aux_read(0x000000) == AUX) && (m->calc_aux_write(0x000000) == AUX);
    sw(0xC008);                                   // ALTZP off
    int a3off = (m->calc_aux_read(0x000000) == MAIN);
    ok &= a3on && a3off;
    printf("MMUTEST mmu_aux_steer: rd=%d wr=%d wr2=%d 80s2=%d 80soff=%d zp=%d zpoff=%d -- %s\n",
           a1r, a1w, a1w2, a2on, a2off, a3on, a3off, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- $C035 SHADOW register window decode (shadow_is_enabled) -----------------
// Each SHADOW inhibit bit gates shadow-to-$E1 for one display window. We set the
// register via the real $C035 write and assert the per-window enable for the
// text1 ($0400), hgr1 ($2000) and SHR (bank-1 $12000) windows. Two register
// values pin the polarity: $00 (nothing inhibited) shadows ALL three; $0B
// (TEXT1+HGR1+SHR inhibit bits set) shadows NONE of them. A revert that inverts a
// bit, drops a window, or mis-ranges the SHR aux window flips this.
static int microtest_mmu_shadow(MMU_IIgs *m) {
    int ok = 1;
    // SHR-only witness: $16000 is inside the SHR window ($12000-$19FFF) but ABOVE the
    // overlapping AUXHGR window ($12000-$15FFF), so only the SHR inhibit bit gates it
    // (a bank-1 $2000 address sits in both windows and would need both bits to inhibit).
    const uint32_t SHR_ONLY = 0x016000;
    m->write(0xC035, 0x00);                       // nothing inhibited
    int all_on = m->shadow_is_enabled(0x000400)   // text1
              && m->shadow_is_enabled(0x002000)   // hgr1
              && m->shadow_is_enabled(SHR_ONLY);  // SHR
    // bits: TEXT1=$01, HGR1=$02, SHR=$08  -> $0B inhibits all three windows.
    m->write(0xC035, 0x01 | 0x02 | 0x08);
    int all_off = !m->shadow_is_enabled(0x000400)
               && !m->shadow_is_enabled(0x002000)
               && !m->shadow_is_enabled(SHR_ONLY);
    // selective: inhibit ONLY SHR ($08) -> text1 still shadowed, SHR not.
    m->write(0xC035, 0x08);
    int sel = m->shadow_is_enabled(0x000400) && !m->shadow_is_enabled(SHR_ONLY);
    m->write(0xC035, 0x08);                        // restore the reset default
    ok &= all_on && all_off && sel;
    printf("MMUTEST mmu_shadow: all_on=%d all_off=%d sel=%d -- %s\n",
           all_on, all_off, sel, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- $E1 bank-latch SHR visibility (bank_e1_write / bank_e1_read) ------------
// The SHR pixels the golden hashes live in the Mega-II AUX image (bank $E1). With
// the bank latch SET (reg_new_video bit0, the reset default), a write to a bank-
// $E1 address lands at megaii_base[addr & 0x1FFFF] -- i.e. $E1:$2000 -> the SHR
// window at Mega-II linear $12000 -- and reads back through the same latch. With
// the latch CLEAR, the same $E1 access falls back to the Mega-II's own read/write
// (bank-0 of the image), so the byte does NOT appear at the aux-image $12000.
// This is the exact aux/main fork the golden's SHR bytes traverse.
static int microtest_mmu_e1_latch(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST mmu_e1_latch: no Mega-II image -- FAIL\n"); return 1; }
    const uint32_t E1_PIX = 0xE12000;                 // $E1:$2000 (top of the SHR window)
    const uint32_t AUXIDX = 0x12000;                  // its Mega-II aux linear index
    // Latch SET (reset default = reg_new_video $01). Write+read through bank $E1.
    m->write(0xC029, 0x01);                            // bank_latch = 1
    m->write(E1_PIX, 0x5A);
    int set_land = (m2[AUXIDX] == 0x5A);               // landed in the aux image
    int set_read = (m->read(E1_PIX) == 0x5A);          // reads back through the latch
    // Latch CLEAR: the SAME $E1 write must NOT update the aux-image $12000 slot.
    m2[AUXIDX] = 0x00;                                 // clear the witness
    m->write(0xC029, 0x00);                            // bank_latch = 0
    m->write(E1_PIX, 0xA5);
    int clr_miss = (m2[AUXIDX] != 0xA5);               // did NOT take the aux path
    m->write(0xC029, 0x01);                            // restore the reset default
    int ok = set_land && set_read && clr_miss;
    printf("MMUTEST mmu_e1_latch: set_land=%d set_read=%d clr_miss=%d -- %s\n",
           set_land, set_read, clr_miss, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- language-card $C08x bank-select + read/write-enable ---------------------
// Drives the real LC soft-switch decode (the same LanguageCardLogic the boot ROM
// hits) and asserts the bank-select, read-enable and the classic double-read
// write-enable. Contract: a $C08x access with A3 set selects bank1; with A3 clear,
// bank2. Read-enable follows the A0/A1 pattern (00/11 -> RAM read, 01/10 -> ROM).
// Write-enable requires TWO consecutive odd reads (PRE_WRITE then WRITE); an even
// read in between clears it. The write-enable is the load-bearing teeth surface:
// we first CLEAR it with an even read, confirm a SINGLE odd read does NOT yet
// enable (only arms PRE_WRITE), then confirm the SECOND odd read enables. A revert
// that drops the double-read latch (enables on one read, or never) flips this.
static int microtest_mmu_lc_bank(MMU_IIgs *m) {
    int ok = 1;
    // --- bank select + read-enable polarity ---
    // $C08B = ...1011: A3=1 (bank1), A1A0=11 (read-enable RAM).
    m->read(0xC08B);
    int b1 = m->is_lc_bank1();                    // A3 set -> bank1
    int re = m->is_lc_read_enable();              // 11 -> read-enable (RAM)
    // $C083 = ...0011: A3=0 (bank2), A1A0=11 (read-enable).
    m->read(0xC083);
    int b2 = !m->is_lc_bank1();                   // A3 clear -> bank2
    // $C089 = ...1001: A1A0=01 -> read-enable CLEARED (ROM read).
    m->read(0xC089);
    int ro = !m->is_lc_read_enable();             // 01 -> ROM read (read-enable off)
    // --- the double-read write-enable latch (the teeth) ---
    m->read(0xC088);                              // EVEN read -> write-enable CLEARED
    int we_off = !m->is_lc_write_enable();        // now NOT write-enabled
    m->read(0xC08B);                              // 1st ODD read -> arms PRE_WRITE only
    int we_mid = !m->is_lc_write_enable();        // still NOT enabled after one odd read
    m->read(0xC08B);                              // 2nd ODD read -> write-enable SET
    int we_on = m->is_lc_write_enable();          // NOW write-enabled (the latch fired)
    ok &= b1 && re && b2 && ro && we_off && we_mid && we_on;
    // Leave the LC back in the reset-ish read-enabled bank2 state the boot path expects.
    m->set_state_register(0x0C); m->bsr_map_memory();
    printf("MMUTEST mmu_lc_bank: b1=%d re=%d b2=%d ro=%d we_off=%d we_mid=%d we_on=%d -- %s\n",
           b1, re, b2, ro, we_off, we_mid, we_on, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- video: SCB per-scanline 320/640 mode decode (iigs_shr::histogram) -------
// The shared SHR decode (the one the video summary AND any SHR render path use)
// reads the per-line SCB to pick 320 vs 640. A 320-mode line decodes each byte as
// two 4-bit indices (160 bytes -> 320 px); a 640-mode line decodes each byte as
// four 2-bit dots (160 bytes -> 640 px). We build a buffer whose lines are split
// half 320 / half 640 and assert the TOTAL pixel count the histogram accounts for
// matches the mode-correct sum. A revert that ignores SCB bit7 (always-320 or
// always-640) changes that total and flips this.
static int microtest_vid_scb_mode(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST vid_scb_mode: no Mega-II image -- FAIL\n"); return 1; }
    uint8_t *e1 = m2 + iigs_shr::MEGAII_E1;          // bank $E1 base
    // Zero the SCB + pixel window, then lay a deterministic pattern.
    for (int i = 0; i < iigs_shr::LINES; i++) e1[iigs_shr::SCB + i] = 0;
    for (int i = 0; i < iigs_shr::LINES * iigs_shr::BYTES_PER_LINE; i++)
        e1[iigs_shr::PIX + i] = 0x11;                 // every nibble/dot non-zero & uniform
    const int N640 = 100;                            // first 100 lines 640, rest 320
    for (int vc = 0; vc < iigs_shr::LINES; vc++)
        e1[iigs_shr::SCB + vc] = (vc < N640) ? 0x80 : 0x00;
    long hist[16];
    iigs_shr::histogram(e1, hist);
    long total = 0; for (int i = 0; i < 16; i++) total += hist[i];
    // 640-mode line = 160 bytes * 4 dots = 640 px; 320-mode line = 160*2 = 320 px.
    long want = (long)N640 * 640 + (long)(iigs_shr::LINES - N640) * 320;
    int ok = (total == want);
    // teeth witness: an always-320 decode would total LINES*320 (= 64000), distinct.
    printf("MMUTEST vid_scb_mode: total=%ld want=%ld (always320=%d) -- %s\n",
           total, want, iigs_shr::LINES * 320, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- video: 640-mode dot->palette-offset map {8,12,0,4} ----------------------
// In 640 mode each byte is 4 two-bit dots; the EFFECTIVE palette index is the
// 2-bit value plus the per-dot color sub-bank offset dot0:+8 dot1:+12 dot2:+0
// dot3:+4 (the exact renderer/HW contract). EVERY line is the same 640 byte $1B =
// 00 01 10 11 -> dot0=0 dot1=1 dot2=2 dot3=3, so each dot carries a DISTINCT 2-bit
// value and the per-dot OFFSET PAIRING is load-bearing (not just the offset SET):
//   dot0:0+8=8  dot1:1+12=13  dot2:2+0=2  dot3:3+4=7  -> bins {2,7,8,13}.
// Each = 160 bytes * 200 lines = 32000; all other bins 0. A revert that permutes
// the offsets (e.g. {0,4,8,12}) lands a DIFFERENT bin set ({0,5,10,15}) and flips
// this; the distinct dot values defeat the all-equal aliasing.
static int microtest_vid_640_offset(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST vid_640_offset: no Mega-II image -- FAIL\n"); return 1; }
    uint8_t *e1 = m2 + iigs_shr::MEGAII_E1;
    for (int i = 0; i < iigs_shr::LINES; i++) e1[iigs_shr::SCB + i] = 0x80;  // ALL 640 mode
    // $1B = 00 01 10 11 -> dot0=0 dot1=1 dot2=2 dot3=3, on every pixel byte.
    for (int i = 0; i < iigs_shr::LINES * iigs_shr::BYTES_PER_LINE; i++)
        e1[iigs_shr::PIX + i] = 0x1B;
    long hist[16];
    iigs_shr::histogram(e1, hist);
    // Expected occupied bins from the {8,12,0,4} pairing: {2,7,8,13}.
    const long PER = (long)iigs_shr::BYTES_PER_LINE * iigs_shr::LINES;   // 160*200 = 32000
    int idx_ok = (hist[8]==PER && hist[13]==PER && hist[2]==PER && hist[7]==PER);
    long other = 0;
    for (int i = 0; i < 16; i++) if (i!=2 && i!=7 && i!=8 && i!=13) other += hist[i];
    int ok = idx_ok && (other == 0);
    printf("MMUTEST vid_640_offset: h8=%ld h13=%ld h2=%ld h7=%ld other=%ld (per=%ld) -- %s\n",
           hist[8], hist[13], hist[2], hist[7], other, PER, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// --- video: 320 fill-mode nibble repeat (SCB bit 5 = $20) ---------------------
// Hardware: a 0 nibble repeats the previous color on that line. SCB $20 is FILL;
// SCB $40 is scanline IRQ and must NOT expand. Byte0=$11 (two colour-1 pixels),
// rest 0, all 200 lines. Fill on -> 64000 idx1; fill off ($40) -> 400 idx1 + 63600 idx0.
static int microtest_vid_fill_mode(MMU_IIgs *m) {
    uint8_t *m2 = m->get_megaii_memory_base();
    if (!m2) { printf("MMUTEST vid_fill_mode: no Mega-II image -- FAIL\n"); return 1; }
    uint8_t *e1 = m2 + iigs_shr::MEGAII_E1;
    for (int i = 0; i < iigs_shr::LINES * iigs_shr::BYTES_PER_LINE; i++)
        e1[iigs_shr::PIX + i] = 0;
    for (int vc = 0; vc < iigs_shr::LINES; vc++)
        e1[iigs_shr::PIX + vc * iigs_shr::BYTES_PER_LINE] = 0x11;
    long hist[16];
    long h1_fill = 0, h0_fill = 0;
    for (int i = 0; i < iigs_shr::LINES; i++) e1[iigs_shr::SCB + i] = iigs_shr::SCB_FILL;
    iigs_shr::histogram(e1, hist);
    h1_fill = hist[1]; h0_fill = hist[0];
    long want_fill = (long)iigs_shr::LINES * 320;
    int ok_fill = (h1_fill == want_fill && h0_fill == 0);
    for (int i = 0; i < iigs_shr::LINES; i++) e1[iigs_shr::SCB + i] = iigs_shr::SCB_IRQ;
    iigs_shr::histogram(e1, hist);
    long want_stripe = (long)iigs_shr::LINES * 2;
    long want_zero = (long)iigs_shr::LINES * 318;
    int ok_irq = (hist[1] == want_stripe && hist[0] == want_zero);
    int ok = ok_fill && ok_irq;
    printf("MMUTEST vid_fill_mode: fill h1=%ld h0=%ld irq-stripe h1=%ld h0=%ld -- %s\n",
           h1_fill, h0_fill, hist[1], hist[0], ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

// NOTE on a deliberately-OMITTED video case (honesty): the $0RGB-palette-word ->
// RGB888 (*0x11 nibble-replication) expansion is golden-relevant, but it lives
// INLINE inside the printf-only iigs_video_summary() (no returnable function), so a
// standalone test would have to re-derive the formula rather than call production
// code — giving it NO teeth (a revert of the real *0x11 would not flip it). Rather
// than ship a brittle self-referential check or refactor the summary just to test
// it, that case is skipped here. The video cases that ARE present (scb_mode,
// 640_offset, vid_fill_mode) drive the SHARED production decode iigs_shr::histogram().

// Dispatcher: A2GSPU_MMUTEST=<name>. "all" runs every case (the CI entry point).
int run_mmu_microtest(GS2AppState *state, const char *which) {
    MMU_IIgs *m = state->mmu_iigs;
    if (!m) { printf("MMUTEST: IIgs MMU not initialized (need -p 5) -- FAIL\n"); return 1; }

    if (strcmp(which, "mmu_aux_steer")  == 0) return microtest_mmu_aux_steer(m);
    if (strcmp(which, "mmu_shadow")     == 0) return microtest_mmu_shadow(m);
    if (strcmp(which, "mmu_e1_latch")   == 0) return microtest_mmu_e1_latch(m);
    if (strcmp(which, "mmu_lc_bank")    == 0) return microtest_mmu_lc_bank(m);
    if (strcmp(which, "vid_scb_mode")   == 0) return microtest_vid_scb_mode(m);
    if (strcmp(which, "vid_640_offset") == 0) return microtest_vid_640_offset(m);
    if (strcmp(which, "vid_fill_mode")  == 0) return microtest_vid_fill_mode(m);

    if (strcmp(which, "all") == 0) {
        int fails = 0;
        fails += (microtest_mmu_aux_steer(m)  != 0);
        fails += (microtest_mmu_shadow(m)     != 0);
        fails += (microtest_mmu_e1_latch(m)   != 0);
        fails += (microtest_mmu_lc_bank(m)    != 0);
        fails += (microtest_vid_scb_mode(m)   != 0);
        fails += (microtest_vid_640_offset(m) != 0);
        fails += (microtest_vid_fill_mode(m)  != 0);
        printf("MMUTEST all: %d failure(s)\n", fails);
        return fails == 0 ? 0 : 1;
    }

    printf("MMUTEST: unknown case '%s' -- FAIL\n", which);
    return 1;
}
