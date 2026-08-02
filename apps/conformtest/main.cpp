/*
 * conformtest -- OWNED per-opcode 6502/65C02 semantic conformance test.
 *
 * WHY THIS EXISTS
 * ---------------
 * cputest depends on Klaus Dormann's 6502_functional_test.bin, which is a
 * third-party artifact that is NOT vendored in this repo. Until that fixture is
 * supplied, the CPU core is unvalidated against any semantic suite at all --
 * cycletest covers TIMING only, and it passes, which is easy to mistake for
 * "the CPU is verified". It is not: an instruction can take exactly the right
 * number of cycles and compute the wrong answer.
 *
 * Rather than depend on the outside artifact, this OWNS the primitive: an
 * explicit table of {setup, one instruction, expected registers/flags/memory}.
 * It deliberately mirrors cycletest's structure (same MMU-over-flat-RAM harness,
 * same table-of-records shape, same PC=$1000 convention) so the two read as one
 * family and a reader who knows one knows both.
 *
 * WHAT IT CHECKS THAT TIMING CANNOT
 *   - the actual arithmetic result (ADC/SBC including decimal mode)
 *   - every flag the instruction is defined to touch, and that it leaves the
 *     others alone
 *   - memory effects (stores, read-modify-write, stack push/pull ordering)
 *   - the 6502's genuinely odd corners, which are where emulators actually differ:
 *       * ADC/SBC overflow (V) sign logic
 *       * BCD (decimal-mode) results
 *       * zero-page wraparound on ZP,X
 *       * JMP ($xxFF) indirect page-boundary bug (6502-only; fixed on 65C02)
 *       * BIT setting N and V from memory bits 7 and 6
 *
 * DIAGNOSTIC STANCE: a failure names the opcode, the field, expected and actual.
 * Klaus's suite reports one failing PC and leaves you to read a listing; this is
 * a worse whole-chip proof but a far better first diagnostic, so the two are
 * complementary rather than redundant.
 *
 * A PASS HERE IS NOT A FULL CONFORMANCE CLAIM. This covers the corners most
 * likely to be wrong, not all 256 opcodes x all addressing modes. Extend the
 * table; do not read a green run as equivalent to the Klaus suite.
 */

#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "clock.hpp"
#include "gs2.hpp"
#include "cpus/cpu_implementations.hpp"   // createCPU()
#include "cpu.hpp"
#include "mmus/mmu.hpp"
#include "NClock.hpp"
#include "matrix.h"     // systematic opcode x mode matrix + reference model

uint8_t memory[65536];

/* Which fields a record actually asserts. Anything not named is not checked, so
 * a record can be precise about its claim instead of over-constraining. */
enum { CK_A = 1, CK_X = 2, CK_Y = 4, CK_SP = 8, CK_MEM = 16, CK_PC = 32 };

struct conf_record {
    const char *description;
    uint8_t  op[3];             /* the instruction under test */
    uint8_t  a_in, x_in, y_in, p_in, sp_in;

    /* pre-seeded memory: up to 4 (address, value) pairs, addr 0xFFFFFFFF = unused */
    uint32_t seed_addr[4];
    uint8_t  seed_val[4];

    int      check;             /* CK_* bitmask */
    uint8_t  a_out, x_out, y_out, sp_out;
    uint32_t mem_addr;          /* checked when CK_MEM */
    uint8_t  mem_out;
    uint16_t pc_out;            /* checked when CK_PC */

    /* Flags: only the bits set in flag_mask are compared, against flag_val. */
    uint8_t  flag_mask, flag_val;
};

#define NOSEED {0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0,0,0,0}

/* Flag bits (6502 P register) */
#define F_C 0x01
#define F_Z 0x02
#define F_D 0x08
#define F_V 0x40
#define F_N 0x80

static conf_record tests[] = {

/* ---- ADC: result + carry + overflow + zero + negative --------------------- */
{ "ADC #$01 (1+1=2)",           {0x69,0x01}, 0x01,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x02,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, 0x00 },
{ "ADC # carry out (FF+01)",    {0x69,0x01}, 0xFF,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x00,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, F_C|F_Z },
{ "ADC # carry in (01+01+C)",   {0x69,0x01}, 0x01,0,0,F_C,0xFF, NOSEED,
  CK_A, 0x03,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, 0x00 },
/* 0x7F + 0x01 = 0x80: signed overflow, negative result, no carry. */
{ "ADC # overflow 7F+01",       {0x69,0x01}, 0x7F,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x80,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, F_V|F_N },
/* 0x80 + 0xFF = 0x7F with carry: overflow the other direction. */
{ "ADC # overflow 80+FF",       {0x69,0xFF}, 0x80,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x7F,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, F_C|F_V },
/* Decimal mode: 0x19 + 0x01 = 0x20 in BCD, not 0x1A. */
{ "ADC # BCD 19+01=20",         {0x69,0x01}, 0x19,0,0,F_D,0xFF, NOSEED,
  CK_A, 0x20,0,0,0, 0,0, 0, F_C|F_D, F_D },

/* ---- SBC ------------------------------------------------------------------ */
{ "SBC # 05-03 (C set)",        {0xE9,0x03}, 0x05,0,0,F_C,0xFF, NOSEED,
  CK_A, 0x02,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, F_C },
{ "SBC # borrow 03-05",         {0xE9,0x05}, 0x03,0,0,F_C,0xFF, NOSEED,
  CK_A, 0xFE,0,0,0, 0,0, 0, F_C|F_Z|F_V|F_N, F_N },
{ "SBC # BCD 21-01=20",         {0xE9,0x01}, 0x21,0,0,F_D|F_C,0xFF, NOSEED,
  CK_A, 0x20,0,0,0, 0,0, 0, F_C|F_D, F_C|F_D },

/* ---- Logic + shifts ------------------------------------------------------- */
{ "AND #$0F",                   {0x29,0x0F}, 0xAA,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x0A,0,0,0, 0,0, 0, F_Z|F_N, 0x00 },
{ "ORA #$0F",                   {0x09,0x0F}, 0xA0,0,0,0x00,0xFF, NOSEED,
  CK_A, 0xAF,0,0,0, 0,0, 0, F_Z|F_N, F_N },
{ "EOR #$FF",                   {0x49,0xFF}, 0xAA,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x55,0,0,0, 0,0, 0, F_Z|F_N, 0x00 },
{ "ASL A (81 -> 02 +C)",        {0x0A},      0x81,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x02,0,0,0, 0,0, 0, F_C|F_Z|F_N, F_C },
{ "LSR A (01 -> 00 +C +Z)",     {0x4A},      0x01,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x00,0,0,0, 0,0, 0, F_C|F_Z|F_N, F_C|F_Z },
{ "ROL A with carry in",        {0x2A},      0x80,0,0,F_C,0xFF, NOSEED,
  CK_A, 0x01,0,0,0, 0,0, 0, F_C|F_Z|F_N, F_C },
{ "ROR A with carry in",        {0x6A},      0x01,0,0,F_C,0xFF, NOSEED,
  CK_A, 0x80,0,0,0, 0,0, 0, F_C|F_Z|F_N, F_C|F_N },

/* ---- BIT: N and V come from MEMORY bits 7 and 6, not from the AND result --- */
{ "BIT $10 (N,V from mem)",     {0x24,0x10}, 0x01,0,0,0x00,0xFF,
  {0x0010,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0xC0,0,0,0},
  CK_A, 0x01,0,0,0, 0,0, 0, F_Z|F_V|F_N, F_Z|F_V|F_N },

/* ---- Compare ------------------------------------------------------------- */
{ "CMP # equal",                {0xC9,0x42}, 0x42,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x42,0,0,0, 0,0, 0, F_C|F_Z|F_N, F_C|F_Z },
{ "CMP # A less",               {0xC9,0x50}, 0x40,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x40,0,0,0, 0,0, 0, F_C|F_Z|F_N, F_N },

/* ---- Loads / stores / transfers ------------------------------------------ */
{ "LDA #$00 sets Z",            {0xA9,0x00}, 0xFF,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x00,0,0,0, 0,0, 0, F_Z|F_N, F_Z },
{ "LDA #$80 sets N",            {0xA9,0x80}, 0x00,0,0,0x00,0xFF, NOSEED,
  CK_A, 0x80,0,0,0, 0,0, 0, F_Z|F_N, F_N },
{ "STA $20 writes memory",      {0x85,0x20}, 0x5A,0,0,0x00,0xFF, NOSEED,
  CK_MEM, 0,0,0,0, 0x0020,0x5A, 0, 0, 0 },
/* Zero-page,X WRAPS within page zero: $F0 + X($20) = $10, not $0110. */
{ "STA $F0,X wraps in ZP",      {0x95,0xF0}, 0x77,0x20,0,0x00,0xFF, NOSEED,
  CK_MEM, 0,0,0,0, 0x0010,0x77, 0, 0, 0 },
{ "TAX",                        {0xAA},      0x33,0,0,0x00,0xFF, NOSEED,
  CK_X, 0,0x33,0,0, 0,0, 0, F_Z|F_N, 0x00 },
{ "INX 7F->80 sets N",          {0xE8},      0,0x7F,0,0x00,0xFF, NOSEED,
  CK_X, 0,0x80,0,0, 0,0, 0, F_Z|F_N, F_N },
{ "DEY 01->00 sets Z",          {0x88},      0,0,0x01,0x00,0xFF, NOSEED,
  CK_Y, 0,0,0x00,0, 0,0, 0, F_Z|F_N, F_Z },

/* ---- Read-modify-write on memory ---------------------------------------- */
{ "INC $30",                    {0xE6,0x30}, 0,0,0,0x00,0xFF,
  {0x0030,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0x41,0,0,0},
  CK_MEM, 0,0,0,0, 0x0030,0x42, 0, F_Z|F_N, 0x00 },
{ "DEC $30 to zero",            {0xC6,0x30}, 0,0,0,0x00,0xFF,
  {0x0030,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0x01,0,0,0},
  CK_MEM, 0,0,0,0, 0x0030,0x00, 0, F_Z|F_N, F_Z },

/* ---- Stack: push order and pull ----------------------------------------- */
{ "PHA writes at SP",           {0x48},      0x9C,0,0,0x00,0xFF, NOSEED,
  CK_MEM|CK_SP, 0,0,0,0xFE, 0x01FF,0x9C, 0, 0, 0 },
{ "PLA reads and bumps SP",     {0x68},      0x00,0,0,0x00,0xFE,
  {0x01FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0x6B,0,0,0},
  CK_A|CK_SP, 0x6B,0,0,0xFF, 0,0, 0, F_Z|F_N, 0x00 },

/* ---- Flow --------------------------------------------------------------- */
{ "JMP $1234 absolute",         {0x4C,0x34,0x12}, 0,0,0,0x00,0xFF, NOSEED,
  CK_PC, 0,0,0,0, 0,0, 0x1234, 0, 0 },
/* The 6502 JMP ($xxFF) bug: the high byte comes from $xx00, NOT $(xx+1)00.
 * 65C02 fixed this, so this record is 6502-only and is skipped elsewhere.
 *
 * The vector page MUST NOT be $10: the instruction itself lives at $1000, and
 * the wrap this test is checking reads the high byte from exactly $xx00. Using
 * $10FF made the test read back its own opcode ($6C) as the high byte and report
 * got=$6C34 -- a self-inflicted failure against a CPU that was reproducing the
 * quirk correctly. $20FF keeps the vector clear of the code. */
{ "JMP ($20FF) 6502 page bug",  {0x6C,0xFF,0x20}, 0,0,0,0x00,0xFF,
  {0x20FF,0x2000,0x2100,0xFFFFFFFF},{0x34,0x12,0xAB,0},
  CK_PC, 0,0,0,0, 0,0, 0x1234, 0, 0 },
{ "BEQ taken when Z set",       {0xF0,0x10}, 0,0,0,F_Z,0xFF, NOSEED,
  CK_PC, 0,0,0,0, 0,0, 0x1012, 0, 0 },
{ "BEQ not taken when Z clear", {0xF0,0x10}, 0,0,0,0x00,0xFF, NOSEED,
  CK_PC, 0,0,0,0, 0,0, 0x1002, 0, 0 },

/* ---- Stack-and-flow: the last six documented opcodes ----------------------
 * These need multi-byte stack effects, so they stay hand-written rather than
 * generated. Together with the matrix and implied tables they complete all 151
 * documented 6502 opcodes. */

/* PHP pushes P with the B (bit 4) and unused (bit 5) bits SET -- $81|$30 = $B1.
 * That is 6502-defined behavior, not an implementation detail. */
{ "PHP pushes P|$30",           {0x08},      0,0,0,(uint8_t)(F_C|F_N),0xFF, NOSEED,
  CK_MEM|CK_SP, 0,0,0,0xFE, 0x01FF,0xB1, 0, 0, 0 },
/* PLP restores the flags. B is not a real flag, so compare only the real ones. */
{ "PLP restores flags",         {0x28},      0,0,0,0x00,0xFE,
  {0x01FF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF},{0xB5,0,0,0},
  CK_SP, 0,0,0,0xFF, 0,0, 0, (uint8_t)(F_C|F_Z|F_N|F_V|F_D), 0x81 },

/* JSR pushes the address of its OWN LAST BYTE (PC-1 = $1002), high byte first,
 * then jumps. RTS later adds 1 to what it pulls. Two records so both pushed
 * bytes are checked. */
{ "JSR $2000 pushes hi ($10)",  {0x20,0x00,0x20}, 0,0,0,0x00,0xFF, NOSEED,
  CK_MEM|CK_SP|CK_PC, 0,0,0,0xFD, 0x01FF,0x10, 0x2000, 0, 0 },
{ "JSR $2000 pushes lo ($02)",  {0x20,0x00,0x20}, 0,0,0,0x00,0xFF, NOSEED,
  CK_MEM, 0,0,0,0, 0x01FE,0x02, 0, 0, 0 },
/* RTS pulls $1002 and resumes at $1003 -- the +1 is the whole subtlety. */
{ "RTS pulls addr and adds 1",  {0x60},      0,0,0,0x00,0xFD,
  {0x01FE,0x01FF,0xFFFFFFFF,0xFFFFFFFF},{0x02,0x10,0,0},
  CK_SP|CK_PC, 0,0,0,0xFF, 0,0, 0x1003, 0, 0 },
/* RTI pulls P then the address, and does NOT add 1 (unlike RTS). */
{ "RTI pulls P then PC (no +1)",{0x40},      0,0,0,0x00,0xFC,
  {0x01FD,0x01FE,0x01FF,0xFFFFFFFF},{0x41,0x34,0x12,0},
  CK_SP|CK_PC, 0,0,0,0xFF, 0,0, 0x1234, (uint8_t)(F_C|F_V), (uint8_t)(F_C|F_V) },
/* BRK pushes PC+2 and P, sets I, and vectors through $FFFE/$FFFF. */
{ "BRK vectors via $FFFE",      {0x00},      0,0,0,0x00,0xFF,
  {0xFFFE,0xFFFF,0xFFFFFFFF,0xFFFFFFFF},{0x00,0x40,0,0},
  CK_SP|CK_PC, 0,0,0,0xFC, 0,0, 0x4000, 0x04, 0x04 },
};

static const int tests_count = sizeof(tests) / sizeof(tests[0]);

/* Records exercising documented 6502-only quirks that the 65C02 deliberately
 * changed. Skipped on other cores rather than silently reported as failures. */
static bool is_6502_only(const char *d) { return strstr(d, "6502 page bug") != nullptr; }

int main(int argc, char **argv) {
    // Unbuffered: this harness can fault, and with buffering the captured output
    // stops wherever the buffer flushed rather than where execution stopped.
    setvbuf(stdout, nullptr, _IONBF, 0);

    processor_type cputype = PROCESSOR_6502;
    const char *cpuname = "6502";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "65c02")) { cputype = PROCESSOR_65C02; cpuname = "65c02"; }
        else if (!strcmp(argv[i], "6502")) { cputype = PROCESSOR_6502; cpuname = "6502"; }
    }
    printf("=== OWNED 6502 semantic conformance test (core: %s) ===\n", cpuname);
    printf("    Asserts results/flags/memory, NOT cycle counts (cycletest covers those).\n");
    printf("    Coverage is the error-prone corners, not all 256 opcodes -- a pass here\n");
    printf("    is NOT equivalent to the Klaus Dormann functional suite.\n\n");

    // No gs2_app_values setup: that global exists for ResourceFile path resolution
    // and this test loads no files (that is the whole point -- it owns its data
    // instead of depending on an external fixture), so referencing it would only
    // add a link dependency for nothing.
    MMU *mmu = new MMU(256, GS2_PAGE_SIZE);
    for (int i = 0; i < 256; i++) mmu->map_page_both(i, &memory[i*256], "TEST RAM");

    cpu_state *cpu = new cpu_state(cputype);
    NClock *clock = new NClock(CLOCK_SET_US, CLOCK_FREE_RUN);
    cpu->cpun = createCPU(cputype, clock);
    cpu->core = cpu->cpun.get();
    cpu->trace = false;
    cpu->set_mmu(mmu);
    cpu->reset();

    int failed = 0, skipped = 0, ran = 0;

    for (int i = 0; i < tests_count; i++) {
        conf_record *t = &tests[i];
        if (cputype != PROCESSOR_6502 && is_6502_only(t->description)) {
            printf("%-32s SKIP (6502-only behavior)\n", t->description);
            skipped++;
            continue;
        }

        memset(memory, 0, sizeof(memory));
        for (int s = 0; s < 4; s++)
            if (t->seed_addr[s] != 0xFFFFFFFF)
                mmu->write(t->seed_addr[s], t->seed_val[s]);

        mmu->write(0x1000, t->op[0]);
        mmu->write(0x1001, t->op[1]);
        mmu->write(0x1002, t->op[2]);

        cpu->pc = 0x1000;
        cpu->a  = t->a_in; cpu->x = t->x_in; cpu->y = t->y_in;
        cpu->p  = t->p_in; cpu->sp = t->sp_in;

        (cpu->cpun->execute_next)(cpu);
        ran++;

        char why[512]; why[0] = 0;
        auto bad = [&](const char *f, unsigned exp, unsigned got) {
            char b[96];
            snprintf(b, sizeof b, "%s%s exp=$%02X got=$%02X", why[0] ? "; " : "", f, exp, got);
            strncat(why, b, sizeof(why) - strlen(why) - 1);
        };

        if ((t->check & CK_A)  && (cpu->a  & 0xFF) != t->a_out)  bad("A",  t->a_out,  cpu->a  & 0xFF);
        if ((t->check & CK_X)  && (cpu->x  & 0xFF) != t->x_out)  bad("X",  t->x_out,  cpu->x  & 0xFF);
        if ((t->check & CK_Y)  && (cpu->y  & 0xFF) != t->y_out)  bad("Y",  t->y_out,  cpu->y  & 0xFF);
        if ((t->check & CK_SP) && (cpu->sp & 0xFF) != t->sp_out) bad("SP", t->sp_out, cpu->sp & 0xFF);
        if (t->check & CK_MEM) {
            uint8_t got = mmu->read(t->mem_addr);
            if (got != t->mem_out) bad("MEM", t->mem_out, got);
        }
        if (t->check & CK_PC) {
            uint16_t got = (uint16_t)(cpu->pc & 0xFFFF);
            if (got != t->pc_out) {
                char b[96];
                snprintf(b, sizeof b, "%sPC exp=$%04X got=$%04X",
                         why[0] ? "; " : "", t->pc_out, got);
                strncat(why, b, sizeof(why) - strlen(why) - 1);
            }
        }
        if (t->flag_mask) {
            uint8_t got = (uint8_t)(cpu->p & t->flag_mask);
            if (got != (t->flag_val & t->flag_mask)) {
                char b[160];
                snprintf(b, sizeof b, "%sP&$%02X exp=$%02X got=$%02X",
                         why[0] ? "; " : "", t->flag_mask,
                         (unsigned)(t->flag_val & t->flag_mask), (unsigned)got);
                strncat(why, b, sizeof(why) - strlen(why) - 1);
            }
        }

        if (why[0]) { printf("%-32s FAILED  %s\n", t->description, why); failed++; }
        else        { printf("%-32s ok\n", t->description); }
    }

    /* ================= SYSTEMATIC OPCODE x MODE MATRIX =====================
     * Expectations here are COMPUTED by the small reference model in matrix.h
     * rather than hand-written, because hand-writing ~130 expected results
     * reliably is not achievable -- and a wrong expectation fails a correct
     * emulator, which is strictly worse than no test. See matrix.h for the
     * honest limits of differential testing against a model. */
    bool opcode_seen[256] = {false};
    for (int i = 0; i < tests_count; i++) opcode_seen[tests[i].op[0]] = true;

    /* Operand-VALUE sweep. The matrix previously ran each opcode at exactly one
     * (A, operand, P) triple, which checks wiring but not arithmetic: a broken
     * carry or overflow rule can easily survive a single sample. These 10 triples
     * are chosen for the boundaries where 6502 arithmetic actually goes wrong --
     * zero, $7F/$80 sign edges, $FF wrap, carry-in set vs clear, and decimal mode --
     * so each opcode/mode pair is now checked 10 times instead of once. */
    struct Sweep { uint8_t a, val, p; const char *why; };
    static const Sweep SWEEP[] = {
        { 0x3C, 0x5B, mtx::fC, "baseline, C set"        },
        { 0x00, 0x00, 0x00,    "zero + zero"            },
        { 0x00, 0x01, 0x00,    "zero minus/plus one"    },
        { 0x7F, 0x01, 0x00,    "signed overflow edge"   },
        { 0x80, 0xFF, 0x00,    "overflow other way"     },
        { 0xFF, 0x01, 0x00,    "unsigned wrap"          },
        { 0xFF, 0xFF, mtx::fC, "all ones, C set"        },
        { 0x50, 0x50, 0x00,    "equal operands"         },
        { 0x01, 0x80, 0x00,    "high bit operand"       },
        { 0x19, 0x01, mtx::fD, "decimal mode"           },
    };
    static const int SWEEP_N = (int)(sizeof(SWEEP) / sizeof(SWEEP[0]));

    printf("\n--- systematic opcode x addressing-mode matrix (x%d operand values) ---\n",
           SWEEP_N);
    int mfail = 0, mran = 0;
    for (int e = 0; e < mtx::TABLE_N * SWEEP_N; e++) {
      {
        const mtx::Entry &en = mtx::TABLE[e / SWEEP_N];
        const Sweep &sw = SWEEP[e % SWEEP_N];
        const mtx::ModeInfo *mi = mtx::find_mode(en.mode);
        if (!mi) continue;

        memset(memory, 0, sizeof(memory));

        /* Seed the indirect pointers the indexed-indirect modes resolve through. */
        mmu->write(0x0084, (uint8_t)(mtx::PTR_TGT & 0xFF));        /* (zp,X): $80+X=$84 */
        mmu->write(0x0085, (uint8_t)(mtx::PTR_TGT >> 8));
        mmu->write(0x0080, (uint8_t)(mtx::PTR_TGT & 0xFF));        /* (zp),Y: ptr at $80 */
        mmu->write(0x0081, (uint8_t)(mtx::PTR_TGT >> 8));

        /* Put this sweep's operand where the mode will look for it. Immediate mode
         * carries the value in the instruction, so patch b1 for that case. */
        uint8_t b1 = (en.mode == mtx::M_IMM) ? sw.val : mi->b1;
        if (mi->eaddr) mmu->write(mi->eaddr, sw.val);

        /* Instruction under test. */
        mmu->write(0x1000, en.opcode);
        mmu->write(0x1001, b1);
        mmu->write(0x1002, mi->b2);

        mtx::RegSet in;
        in.a = sw.a; in.x = mtx::SET_X; in.y = mtx::SET_Y; in.p = sw.p;

        cpu->pc = 0x1000;
        cpu->a = in.a; cpu->x = in.x; cpu->y = in.y; cpu->p = in.p; cpu->sp = 0xFF;

        uint8_t opval = sw.val;

        uint8_t mem_exp = 0;
        mtx::RegSet out = mtx::apply(en.kind, in, opval, &mem_exp);
        uint8_t fmask = mtx::flag_mask_for(en.kind);
        /* NMOS 6502 leaves V undefined for ADC/SBC in decimal mode, so do not
         * assert it there -- asserting an undefined value is how a test starts
         * failing correct hardware. */
        if ((sw.p & mtx::fD) &&
            (en.kind == mtx::K_ADC || en.kind == mtx::K_SBC))
            fmask &= (uint8_t)~mtx::fV;

        (cpu->cpun->execute_next)(cpu);
        opcode_seen[en.opcode] = true;
        mran++;

        char why[256]; why[0] = 0;
        auto note = [&](const char *f, unsigned exp, unsigned got) {
            char b[80];
            snprintf(b, sizeof b, "%s%s exp=$%02X got=$%02X", why[0] ? "; " : "", f, exp, got);
            strncat(why, b, sizeof(why) - strlen(why) - 1);
        };

        if (mtx::writes_mem(en.kind)) {
            uint8_t got = mmu->read(mi->eaddr);
            if (got != mem_exp) note("MEM", mem_exp, got);
        } else {
            if ((cpu->a & 0xFF) != out.a) note("A", out.a, cpu->a & 0xFF);
            if ((cpu->x & 0xFF) != out.x) note("X", out.x, cpu->x & 0xFF);
            if ((cpu->y & 0xFF) != out.y) note("Y", out.y, cpu->y & 0xFF);
        }
        if (fmask) {
            uint8_t g = (uint8_t)(cpu->p & fmask), x = (uint8_t)(out.p & fmask);
            if (g != x) {
                char b[96];
                snprintf(b, sizeof b, "%sP&$%02X exp=$%02X got=$%02X",
                         why[0] ? "; " : "", fmask, x, g);
                strncat(why, b, sizeof(why) - strlen(why) - 1);
            }
        }

        char label[80];
        snprintf(label, sizeof label, "%s %s ($%02X) A=$%02X op=$%02X P=$%02X [%s]",
                 en.mnem, mi->name, en.opcode, sw.a, sw.val, sw.p, sw.why);
        if (why[0]) { printf("%-64s FAILED  %s\n", label, why); mfail++; }
      }
    }
    printf("matrix: %d opcode/mode/value cases, %d failed\n", mran, mfail);

    /* ---- implied / accumulator / branch ----------------------------------- */
    printf("\n--- implied, accumulator and branch instructions ---\n");
    int ifail = 0, iran = 0;
    for (int e = 0; e < mtx::IMPLIED_N; e++) {
        const mtx::Implied &im = mtx::IMPLIED[e];
        memset(memory, 0, sizeof(memory));
        mmu->write(0x1000, im.opcode);
        mmu->write(0x1001, 0x10);      /* branch displacement / harmless operand */
        cpu->pc = 0x1000;
        cpu->a = im.a_in; cpu->x = im.x_in; cpu->y = im.y_in;
        cpu->p = im.p_in; cpu->sp = im.sp_in;

        (cpu->cpun->execute_next)(cpu);
        opcode_seen[im.opcode] = true;
        iran++;

        char why[192]; why[0] = 0;
        if (im.cls == mtx::IC_BR) {
            uint16_t got = (uint16_t)(cpu->pc & 0xFFFF);
            if (got != im.pc_out)
                snprintf(why, sizeof why, "PC exp=$%04X got=$%04X", im.pc_out, got);
        } else if (im.which != mtx::W_NONE) {
            unsigned got = 0;
            switch (im.which) {
                case mtx::W_A:  got = cpu->a  & 0xFF; break;
                case mtx::W_X:  got = cpu->x  & 0xFF; break;
                case mtx::W_Y:  got = cpu->y  & 0xFF; break;
                case mtx::W_SP: got = cpu->sp & 0xFF; break;
                default: break;
            }
            if (got != im.reg_out)
                snprintf(why, sizeof why, "reg exp=$%02X got=$%02X", im.reg_out, got);
        }
        if (im.fmask) {
            uint8_t g = (uint8_t)(cpu->p & im.fmask), x = (uint8_t)(im.fval & im.fmask);
            if (g != x) {
                char b[96];
                snprintf(b, sizeof b, "%sP&$%02X exp=$%02X got=$%02X",
                         why[0] ? "; " : "", im.fmask, x, g);
                strncat(why, b, sizeof(why) - strlen(why) - 1);
            }
        }
        char label[48];
        snprintf(label, sizeof label, "%s ($%02X)", im.mnem, im.opcode);
        if (why[0]) { printf("%-32s FAILED  %s\n", label, why); ifail++; }
    }
    printf("implied/branch: %d instructions, %d failed\n", iran, ifail);
    mfail += ifail;
    mran  += iran;

    /* ---- MEASURED coverage, not asserted ---------------------------------- */
    int seen = 0;
    for (int i = 0; i < 256; i++) if (opcode_seen[i]) seen++;
    printf("\n--- coverage (measured, not claimed) ---\n");
    printf("distinct opcode bytes executed: %d of 256\n", seen);
    printf("NOT executed by this suite (%d):", 256 - seen);
    int shown = 0;
    for (int i = 0; i < 256 && shown < 64; i++)
        if (!opcode_seen[i]) { printf(" %02X", i); shown++; }
    if (256 - seen > shown) printf(" ... (+%d more)", 256 - seen - shown);
    printf("\n");
    if (seen >= 151) {
        printf("That is the COMPLETE documented 6502 instruction set (151 legal\n");
        printf("opcodes). The %d unexecuted bytes are the undocumented/illegal NMOS\n", 256 - seen);
        printf("slots.\n");
    }
    printf("\nWHAT A GREEN RUN DOES AND DOES NOT MEAN:\n");
    printf("  DOES  -- every documented opcode was executed and its result, flags and\n");
    printf("           memory effect matched an independently written reference model,\n");
    printf("           across all addressing modes including zero-page wrap, (zp),Y\n");
    printf("           page carry and abs,X/Y indexing.\n");
    printf("  DOES NOT -- prove the undocumented opcodes behave like NMOS silicon;\n");
    printf("           prove cycle timing (that is cycletest);\n");
    printf("           exhaust operand VALUES -- each opcode is tested at a handful of\n");
    printf("           inputs chosen for their corners, not over all 2^16 combinations;\n");
    printf("           or catch a misconception SHARED by the model and the CPU.\n");
    printf("  So this is a strong breadth check, not a substitute for the Klaus\n");
    printf("  Dormann suite, which exercises long dependent instruction sequences.\n");

    failed += mfail;
    ran    += mran;
    printf("\nRan %d, skipped %d. Failed tests: %d\n", ran, skipped, failed);
    return failed ? 1 : 0;
}
