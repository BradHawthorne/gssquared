
/* ==========================================================================
   THIS FILE IS NOT BUILT, AND WOULD NOT COMPILE IF IT WERE.

   It is in no CMakeLists target, produces no object, and `ctest -N` reports 0
   tests. It is also written against an older architecture: run_test() calls
   run_cpus(), which no longer exists (gs2.cpp has run_cpus_init() and a
   different execution model), and drives CPUs[0] directly rather than a
   computer_t.

   Stated here because the file LOOKS like the emulator's test suite -- it has
   run_test() and assert_cycles() and a table of programs -- and finding it is a
   reasonable way to conclude that CPU timing is covered by unit tests. It is
   not. Nothing here has run in a long time.

   Where the coverage actually lives: tools/cpucheck.ps1 in the a2engine repo,
   driven over the A2GSPU CTRL rail -- 599 opcode timing checks against each of
   the 6502, 65C02 and 65816 cores, plus base/page-cross/branch penalties. That
   harness tests the SHIPPING BINARY through the same interface a guest uses,
   rather than a separate link of the CPU core, which is the stronger position
   of the two and the reason reviving this file has not been worth it.
   ========================================================================== */

#include <functional>
#include <cassert>
#include <iostream>
#include <unistd.h>

#include "gs2.hpp"
#include "cpu.hpp"
#include "opcodes.hpp"
#include "test.hpp"

void run_test(Test& test) {
    cpu_state *cpu = CPUs[0];
    uint32_t addr = test.program_address;

    // Load the program into memory
    for (uint32_t i = 0; i < test.program_size; i++) {
        raw_memory_write(cpu, addr++, test.program[i]);
    }

    // Set up vectors
    raw_memory_write_word(cpu, RESET_VECTOR, test.program_address);
    raw_memory_write_word(cpu, BRK_VECTOR, test.program_address);

    // Run setup function
    if (test.setup) {
        test.setup(cpu);
    }

    // Run the CPU
    run_cpus();

    // Run assertions
    if (test.assertions) {
        test.assertions(cpu);
    }
}

void assert_cycles(cpu_state *cpu, uint64_t expected_cycles) {
    if (cpu->cycles == expected_cycles) return;
    std::cout << expected_cycles << " expected, got " << cpu->cycles << std::endl;
    assert(cpu->cycles == expected_cycles);
}

void demo_ram() {
    Test test = {
         .program = {
            OP_LDA_IMM, 0x40,
            OP_JSR_ABS, 0x13, 0x03,
            OP_LDY_IMM, 0xc0,
            OP_LDA_IMM, 0x0C,
            OP_JSR_ABS, 0x13, 0x03,
            OP_LDA_ABS, 0x30, 0xC0,
            OP_DEY_IMP,
            OP_BNE_REL, 0xF5,
            OP_HLT_IMP,

            OP_SEC_IMP,    /* 0x0314 */
            OP_PHA_IMP,
            OP_SBC_IMM, 0x01,
            OP_BNE_REL, 0xFC,
            OP_PLA_IMP,
            OP_SBC_IMM, 0x01,
            OP_BNE_REL, 0xF6,
            OP_RTS_IMP,

            /*
            OP_LDX_IMM, 0xFF, // init the stack pointer
            OP_TXS_IMP,

            OP_JSR_ABS, 0x09, 0x03, // JSR to next instruction (ldx imm hehe)
            OP_JMP_ABS, 0x00, 0x03, // do the whole thing again

            OP_LDX_IMM, 0xD0,       // 2 cycles  should be 0x0307
            OP_LDA_ZP_X, 0x00,      // 3 cycles
            OP_LDA_ZP_X, 0x40,      // 3 cycles

            OP_STA_ZP_X, 0x15,      // 3 cycles
            OP_STA_ABS_X, 0x12, 0x34, // 4 cycles

            OP_LDA_ABS, 0x12, 0x34, // 4 cycles
            OP_LDA_ABS_X, 0x12, 0x34, // 4 cycles?

            OP_LDY_IMM, 0x67,           
            OP_LDX_ABS_Y, 0x12, 0x34, // 4 cycles
            OP_LDX_IMM, 0x56, // 2 cycles
            OP_LDY_ABS_X, 0x12, 0x34, // 
            
            OP_LDA_IND_X, 0x40, // 5 cycles
            OP_LDY_IMM, 0x89, // 2 cycles
            OP_LDA_IND_Y, 0x40, // 5 cycles

            OP_LDX_ZP_Y, 0x50, // 3 cycles
            OP_LDY_ZP_X, 0x50, // 3 cycles
           
            OP_LDA_IMM, 0xAA,
            OP_ORA_IMM, 0x55,
            OP_STA_ZP, 0xFF, 

            OP_LDA_IMM, 0xAA,
            OP_EOR_IMM, 0xFF,
            OP_STA_ZP, 0x60,

            OP_DEX_IMP,
            OP_DEY_IMP,

    // overflow 0x50 + 0x10 = 0x60, v = 0

            OP_CLC_IMP,         
            OP_LDA_IMM, 0x50,
            OP_ADC_IMM, 0x10,

    // overflow 0x50 + 0x50 = 0xA0, v = 1 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0x50,
            OP_ADC_IMM, 0x50,

    // overflow 0x50 + 0x90 = 0xE0, v = 0 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0x50,
            OP_ADC_IMM, 0x90,

    // overflow 0x50 + 0xD0 = 0x120, v = 0 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0x50,
            OP_ADC_IMM, 0xD0,

    // overflow 0xD0 + 0x10 = 0xE0, v = 0 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0xD0,
            OP_ADC_IMM, 0x10,

    // overflow 0xD0 + 0x50 = 0x120, v = 0 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0xD0,
            OP_ADC_IMM, 0x50,

    // overflow 0xD0 + 0x90 = 0x160, v = 1 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0xD0,
            OP_ADC_IMM, 0x90,            

    // overflow 0xD0 + 0xD0 = 0x1A0, v = 0 
            OP_CLC_IMP,         
            OP_LDA_IMM, 0xD0,
            OP_ADC_IMM, 0xD0,

    // overflow 0xD0 + 0xD0 = 0x1A0, v=0
            OP_LDA_IMM, 0xD0,
            OP_STA_ZP, 0x78,
            OP_CLC_IMP,
            OP_ADC_ZP, 0x78,

    // sbc immediate test
            OP_SEC_IMP,
            OP_LDA_IMM, 0x50,
            OP_SBC_IMM, 0xF0,

            OP_SEC_IMP,
            OP_LDA_IMM, 0x50,
            OP_SBC_IMM, 0xB0,

            OP_SEC_IMP,
            OP_LDA_IMM, 0x50,
            OP_SBC_IMM, 0x70,

            OP_SEC_IMP,
            OP_LDA_IMM, 0x50,
            OP_SBC_IMM, 0x30,

            OP_SEC_IMP,
            OP_LDA_IMM, 0xD0,
            OP_SBC_IMM, 0xF0,

            OP_SEC_IMP,
            OP_LDA_IMM, 0xD0,
            OP_SBC_IMM, 0xB0,

            OP_SEC_IMP,
            OP_LDA_IMM, 0xD0,
            OP_SBC_IMM, 0x70,

            OP_SEC_IMP,
            OP_LDA_IMM, 0xD0,
            OP_SBC_IMM, 0x30,
            
            OP_STA_ZP, 0x78,
            OP_DEC_ZP, 0x78,

            OP_STA_ABS, 0x12, 0x34,
            OP_DEC_ABS, 0x12, 0x34,

            OP_LDA_IMM, 0xAA,       // 2 cycles
            OP_PHA_IMP,
            OP_PLA_IMP,
            OP_LDY_ABS, 0xFF, 0x01,
            OP_LDY_ABS, 0xFE, 0x01,

            OP_STA_ZP,  0x00,       // 3 cycles
            OP_LDA_ZP,  0x00,       // 3 cycles
            OP_LDX_ZP,  0x00,       // 3 cycles
            OP_LDY_ZP,  0x01,       // 3 cycles
            OP_STA_ABS, 0x34, 0x12, // 4 cycles
            OP_ADC_IMM, 0x03,       // 2 cycles
            OP_STA_ABS, 0x35, 0x12, // 4 cycles

            OP_LDX_ABS, 0x35, 0x12, // 4 cycles
            OP_LDY_ABS, 0x35, 0x12, // 4 cycles

            OP_CLC_IMP,
            OP_BCS_REL, 0x02,

            OP_SED_IMP,
            OP_SEC_IMP,
            OP_LDA_IMM, 0x58,
            OP_ADC_IMM, 0x46,

            OP_CLC_IMP,
            OP_LDA_IMM, 0x12,
            OP_ADC_IMM, 0x34,

            OP_CLC_IMP,
            OP_LDA_IMM, 0x81,
            OP_ADC_IMM, 0x92,

            OP_SEC_IMP,
            OP_LDA_IMM, 0x00,
            OP_SBC_IMM, 0x01,

            OP_SEC_IMP,
            OP_LDA_IMM, 0x46,
            OP_SBC_IMM, 0x12,

            OP_SEC_IMP,
            OP_LDA_IMM, 0x40,
            OP_SBC_IMM, 0x13,

            OP_CLC_IMP,
            OP_LDA_IMM, 0x32,
            OP_SBC_IMM, 0x02,

            OP_CLD_IMP,

            OP_LDX_IMM, 0x27,
            OP_LDA_IMM, 0x41,
            OP_STA_ABS_X, 0x00, 0x04,
            OP_DEX_IMP,
            OP_BNE_REL, 0xFA,

            OP_HLT_IMP,
            OP_CLC_IMP,
            OP_BCC_REL, 0xFD,

            OP_RTS_IMP,

            OP_BRK_IMP,            // 7 cycles
            OP_JMP_ABS, 0x00, 0x03, // 3 cycles */
        },
        .program_size = sizeof(test.program),
        .program_address = 0x0300,
        .setup = [](cpu_state *cpu) {
            // Initial setup if needed
        },
        .assertions = [](cpu_state *cpu) {
            // Assertions to check the final state
            //assert(cpu->a_lo == 0xAE);
            //assert(cpu->C == 0);
            //assert(raw_memory_read(cpu, 0x0000) == 0xAA);
//            assert(raw_memory_read(cpu, 0x1234) == 0xAA);
            //assert_cycles(cpu, 219);
        }
    };

    run_test(test);
}