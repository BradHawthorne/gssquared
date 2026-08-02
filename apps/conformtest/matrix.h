/*
 * matrix.h -- systematic opcode x addressing-mode coverage for conformtest.
 *
 * WHY GENERATED RATHER THAN HAND-WRITTEN
 * --------------------------------------
 * The hand-written records in main.cpp encode understanding of specific corners
 * and are worth having. But scaling that style to ~150 legal opcodes means writing
 * ~150 expected results by hand, and a WRONG EXPECTATION IS WORSE THAN NO TEST --
 * it fails a correct emulator and sends you hunting real code. That already
 * happened once here at a scale of 34 records (the JMP ($10FF) vector was placed
 * on top of the instruction itself, and the "failure" was the emulator correctly
 * reproducing the page-wrap bug).
 *
 * So the expectations here are COMPUTED by a deliberately tiny reference model of
 * operand semantics, written from the ISA definition rather than by reading
 * gssquared's implementation. The model covers only what an ALU/load/store/RMW
 * instruction does to A/X/Y/flags/memory given an already-fetched operand -- it is
 * NOT a second CPU. Addressing-mode resolution is tabulated separately, because
 * effective-address computation is precisely where emulators tend to differ
 * (zero-page wrap, (zp),Y carry, abs,X page crossing).
 *
 * HONEST FRAMING OF "FULL COVERAGE": a differential test against a model shares
 * any misconception between model and expectation. That is a real limit. What it
 * buys is breadth no hand-written table would achieve reliably, and any
 * disagreement is INVESTIGATED rather than auto-resolved -- three times in this
 * session the test turned out to be the wrong side. Coverage is therefore REPORTED
 * AS A MEASURED NUMBER (which of 256 opcode bytes were actually executed), not
 * asserted, so the gap is always visible.
 */

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace mtx {

/* ---- Fixed, deliberately awkward setup shared by every generated record -----
 * X and Y are non-zero so indexed modes cannot pass by accident, and the zero-page
 * pointer at $80 deliberately points near a page boundary so (zp),Y carries into
 * the next page -- a classic emulator bug. */
enum {
    SET_X   = 0x04,
    SET_Y   = 0x08,
    ZP_BASE = 0x10,     /* zp operand            -> $0010            */
    ZP_PTR  = 0x80,     /* (zp,X)/(zp),Y pointer slot                 */
    ABS_ADDR= 0x2000,   /* abs operand           -> $2000            */
    PTR_TGT = 0x30F8,   /* what ZP_PTR points at; +Y($08) = $3100 -> page carry */
    OPERAND = 0x5B      /* the value the instruction will see in memory */
};

enum Mode { M_IMM, M_ZP, M_ZPX, M_ZPY, M_ABS, M_ABSX, M_ABSY, M_INDX, M_INDY, M_IMP, M_ACC };

struct ModeInfo {
    Mode        mode;
    const char *name;
    int         nbytes;       /* total instruction length */
    uint8_t     b1, b2;       /* operand bytes */
    uint32_t    eaddr;        /* effective address the operand lives at (0 = none) */
};

/* Effective addresses below are computed from the 6502 definition, with X=$04,
 * Y=$08 and the pointer at $80 -> $30F8:
 *   zp,X   : ($10 + $04) & $FF        = $0014
 *   zp,Y   : ($10 + $08) & $FF        = $0018   (LDX/STX only)
 *   abs,X  : $2000 + $04              = $2004
 *   abs,Y  : $2000 + $08              = $2008
 *   (zp,X) : ptr at ($80+$04)&$FF=$84 -> we seed $0084/$0085
 *   (zp),Y : ptr at $80 -> $30F8, + $08 = $3100  <-- crosses a page
 */
static const ModeInfo MODES[] = {
    { M_IMM,  "#",      2, OPERAND,      0,    0        },
    { M_ZP,   "zp",     2, ZP_BASE,      0,    0x0010   },
    { M_ZPX,  "zp,X",   2, ZP_BASE,      0,    0x0014   },
    { M_ZPY,  "zp,Y",   2, ZP_BASE,      0,    0x0018   },
    { M_ABS,  "abs",    3, 0x00,      0x20,    0x2000   },
    { M_ABSX, "abs,X",  3, 0x00,      0x20,    0x2004   },
    { M_ABSY, "abs,Y",  3, 0x00,      0x20,    0x2008   },
    { M_INDX, "(zp,X)", 2, ZP_PTR,       0,    PTR_TGT  },
    { M_INDY, "(zp),Y", 2, ZP_PTR,       0,    0x3100   },
};

static const ModeInfo *find_mode(Mode m) {
    for (const auto &mi : MODES) if (mi.mode == m) return &mi;
    return nullptr;
}

/* ---- The reference model -----------------------------------------------------
 * Given A/X/Y/P and the operand VALUE, produce the expected A/X/Y/P and, for a
 * store/RMW, the expected memory value. Written from the ISA definition.
 * Flag bits: C=$01 Z=$02 I=$04 D=$08 B=$10 V=$40 N=$80. */
enum { fC=0x01, fZ=0x02, fD=0x08, fV=0x40, fN=0x80 };

struct RegSet { uint8_t a, x, y, p; };

static inline uint8_t nz(uint8_t v, uint8_t p) {
    p &= (uint8_t)~(fZ | fN);
    if (v == 0)      p |= fZ;
    if (v & 0x80)    p |= fN;
    return p;
}

/* What each operation family is. `writes_mem` marks store/RMW so the harness
 * knows to compare memory instead of a register. */
enum OpKind {
    K_ORA, K_AND, K_EOR, K_ADC, K_SBC, K_CMP, K_LDA, K_STA,
    K_LDX, K_LDY, K_STX, K_STY, K_CPX, K_CPY, K_BIT,
    K_ASL, K_LSR, K_ROL, K_ROR, K_INC, K_DEC
};

static bool writes_mem(OpKind k) {
    return k == K_STA || k == K_STX || k == K_STY ||
           k == K_ASL || k == K_LSR || k == K_ROL || k == K_ROR ||
           k == K_INC || k == K_DEC;
}

/* Compare helper: sets C if reg >= val (unsigned), plus N/Z of the difference. */
static uint8_t do_cmp(uint8_t reg, uint8_t val, uint8_t p) {
    uint16_t d = (uint16_t)reg - (uint16_t)val;
    p = nz((uint8_t)(d & 0xFF), p);
    p &= (uint8_t)~fC;
    if (reg >= val) p |= fC;
    return p;
}

/* Apply the op. `mem_out` receives the written value for store/RMW ops. */
static RegSet apply(OpKind k, RegSet r, uint8_t val, uint8_t *mem_out) {
    RegSet o = r;
    switch (k) {
    case K_ORA: o.a = (uint8_t)(r.a | val); o.p = nz(o.a, r.p); break;
    case K_AND: o.a = (uint8_t)(r.a & val); o.p = nz(o.a, r.p); break;
    case K_EOR: o.a = (uint8_t)(r.a ^ val); o.p = nz(o.a, r.p); break;
    /* ADC/SBC honour the D flag. The model originally did pure binary addition and
     * ignored decimal mode, so it expected $19+$01=$1A where the CPU correctly
     * produced BCD $20 -- eight failures against a correct emulator, found the
     * moment the operand sweep introduced a decimal-mode case. Worth noting SBC did
     * NOT fail: $19-$01-1 = $17 in binary AND in BCD, so it agreed by coincidence.
     * That near-miss is precisely why one sample per opcode is not enough. */
    case K_ADC: {
        uint8_t cin = (r.p & fC) ? 1 : 0;
        if (r.p & fD) {
            unsigned lo = (unsigned)(r.a & 0x0F) + (val & 0x0F) + cin;
            if (lo > 9) lo += 6;
            unsigned hi = (unsigned)(r.a >> 4) + (val >> 4) + (lo > 0x0F ? 1 : 0);
            if (hi > 9) hi += 6;
            o.a = (uint8_t)(((hi & 0x0F) << 4) | (lo & 0x0F));
            o.p = nz(o.a, r.p);
            o.p &= (uint8_t)~fC;
            if (hi > 0x0F) o.p |= fC;
            /* V is not meaningfully defined for decimal mode on NMOS; the caller
             * masks it out rather than asserting a value nobody guarantees. */
        } else {
            uint16_t s = (uint16_t)r.a + val + cin;
            o.a = (uint8_t)(s & 0xFF);
            o.p = nz(o.a, r.p);
            o.p &= (uint8_t)~(fC | fV);
            if (s > 0xFF) o.p |= fC;
            /* V set when both inputs share a sign that differs from the result's. */
            if (((r.a ^ o.a) & (val ^ o.a) & 0x80)) o.p |= fV;
        }
        break; }
    case K_SBC: {
        uint8_t bin = (r.p & fC) ? 0 : 1;
        uint16_t d = (uint16_t)r.a - val - bin;      /* binary result drives C */
        if (r.p & fD) {
            int lo = (int)(r.a & 0x0F) - (val & 0x0F) - bin;
            int hi = (int)(r.a >> 4)  - (val >> 4);
            if (lo & 0x10) { lo -= 6; hi -= 1; }
            if (hi & 0x10) { hi -= 6; }
            o.a = (uint8_t)(((hi & 0x0F) << 4) | (lo & 0x0F));
            o.p = nz(o.a, r.p);
            o.p &= (uint8_t)~fC;
            if (d < 0x100) o.p |= fC;
        } else {
            o.a = (uint8_t)(d & 0xFF);
            o.p = nz(o.a, r.p);
            o.p &= (uint8_t)~(fC | fV);
            if (d < 0x100) o.p |= fC;            /* C = NOT borrow */
            if (((r.a ^ val) & (r.a ^ o.a) & 0x80)) o.p |= fV;
        }
        break; }
    case K_CMP: o.p = do_cmp(r.a, val, r.p); break;
    case K_CPX: o.p = do_cmp(r.x, val, r.p); break;
    case K_CPY: o.p = do_cmp(r.y, val, r.p); break;
    case K_LDA: o.a = val; o.p = nz(val, r.p); break;
    case K_LDX: o.x = val; o.p = nz(val, r.p); break;
    case K_LDY: o.y = val; o.p = nz(val, r.p); break;
    case K_STA: *mem_out = r.a; break;
    case K_STX: *mem_out = r.x; break;
    case K_STY: *mem_out = r.y; break;
    case K_BIT: {
        uint8_t t = (uint8_t)(r.a & val);
        o.p = r.p & (uint8_t)~(fZ | fN | fV);
        if (t == 0)     o.p |= fZ;
        if (val & 0x80) o.p |= fN;      /* N and V come from MEMORY, not the AND */
        if (val & 0x40) o.p |= fV;
        break; }
    case K_ASL: { uint8_t v = (uint8_t)(val << 1);
        o.p = nz(v, r.p); o.p &= (uint8_t)~fC; if (val & 0x80) o.p |= fC;
        *mem_out = v; break; }
    case K_LSR: { uint8_t v = (uint8_t)(val >> 1);
        o.p = nz(v, r.p); o.p &= (uint8_t)~fC; if (val & 0x01) o.p |= fC;
        *mem_out = v; break; }
    case K_ROL: { uint8_t v = (uint8_t)((val << 1) | ((r.p & fC) ? 1 : 0));
        o.p = nz(v, r.p); o.p &= (uint8_t)~fC; if (val & 0x80) o.p |= fC;
        *mem_out = v; break; }
    case K_ROR: { uint8_t v = (uint8_t)((val >> 1) | ((r.p & fC) ? 0x80 : 0));
        o.p = nz(v, r.p); o.p &= (uint8_t)~fC; if (val & 0x01) o.p |= fC;
        *mem_out = v; break; }
    case K_INC: { uint8_t v = (uint8_t)(val + 1); o.p = nz(v, r.p); *mem_out = v; break; }
    case K_DEC: { uint8_t v = (uint8_t)(val - 1); o.p = nz(v, r.p); *mem_out = v; break; }
    }
    return o;
}

/* Which flags each family is DEFINED to affect. Anything outside this mask is not
 * compared, so the test never over-constrains and never accidentally asserts that
 * an unrelated flag was preserved when the ISA says nothing about it. */
static uint8_t flag_mask_for(OpKind k) {
    switch (k) {
    case K_ORA: case K_AND: case K_EOR:
    case K_LDA: case K_LDX: case K_LDY:
    case K_INC: case K_DEC:                   return fZ | fN;
    case K_ADC: case K_SBC:                   return fC | fZ | fV | fN;
    case K_CMP: case K_CPX: case K_CPY:       return fC | fZ | fN;
    case K_ASL: case K_LSR: case K_ROL: case K_ROR: return fC | fZ | fN;
    case K_BIT:                               return fZ | fV | fN;
    default:                                  return 0;   /* stores touch no flags */
    }
}

struct Entry { OpKind kind; const char *mnem; Mode mode; uint8_t opcode; };

/* The legal opcode map, by family and mode. Opcode bytes are from the 6502
 * definition; the harness reports which of the 256 bytes it actually executed so
 * omissions here surface as measured gaps rather than silent ones. */
static const Entry TABLE[] = {
  {K_ORA,"ORA",M_IMM,0x09},{K_ORA,"ORA",M_ZP,0x05},{K_ORA,"ORA",M_ZPX,0x15},
  {K_ORA,"ORA",M_ABS,0x0D},{K_ORA,"ORA",M_ABSX,0x1D},{K_ORA,"ORA",M_ABSY,0x19},
  {K_ORA,"ORA",M_INDX,0x01},{K_ORA,"ORA",M_INDY,0x11},

  {K_AND,"AND",M_IMM,0x29},{K_AND,"AND",M_ZP,0x25},{K_AND,"AND",M_ZPX,0x35},
  {K_AND,"AND",M_ABS,0x2D},{K_AND,"AND",M_ABSX,0x3D},{K_AND,"AND",M_ABSY,0x39},
  {K_AND,"AND",M_INDX,0x21},{K_AND,"AND",M_INDY,0x31},

  {K_EOR,"EOR",M_IMM,0x49},{K_EOR,"EOR",M_ZP,0x45},{K_EOR,"EOR",M_ZPX,0x55},
  {K_EOR,"EOR",M_ABS,0x4D},{K_EOR,"EOR",M_ABSX,0x5D},{K_EOR,"EOR",M_ABSY,0x59},
  {K_EOR,"EOR",M_INDX,0x41},{K_EOR,"EOR",M_INDY,0x51},

  {K_ADC,"ADC",M_IMM,0x69},{K_ADC,"ADC",M_ZP,0x65},{K_ADC,"ADC",M_ZPX,0x75},
  {K_ADC,"ADC",M_ABS,0x6D},{K_ADC,"ADC",M_ABSX,0x7D},{K_ADC,"ADC",M_ABSY,0x79},
  {K_ADC,"ADC",M_INDX,0x61},{K_ADC,"ADC",M_INDY,0x71},

  {K_SBC,"SBC",M_IMM,0xE9},{K_SBC,"SBC",M_ZP,0xE5},{K_SBC,"SBC",M_ZPX,0xF5},
  {K_SBC,"SBC",M_ABS,0xED},{K_SBC,"SBC",M_ABSX,0xFD},{K_SBC,"SBC",M_ABSY,0xF9},
  {K_SBC,"SBC",M_INDX,0xE1},{K_SBC,"SBC",M_INDY,0xF1},

  {K_CMP,"CMP",M_IMM,0xC9},{K_CMP,"CMP",M_ZP,0xC5},{K_CMP,"CMP",M_ZPX,0xD5},
  {K_CMP,"CMP",M_ABS,0xCD},{K_CMP,"CMP",M_ABSX,0xDD},{K_CMP,"CMP",M_ABSY,0xD9},
  {K_CMP,"CMP",M_INDX,0xC1},{K_CMP,"CMP",M_INDY,0xD1},

  {K_LDA,"LDA",M_IMM,0xA9},{K_LDA,"LDA",M_ZP,0xA5},{K_LDA,"LDA",M_ZPX,0xB5},
  {K_LDA,"LDA",M_ABS,0xAD},{K_LDA,"LDA",M_ABSX,0xBD},{K_LDA,"LDA",M_ABSY,0xB9},
  {K_LDA,"LDA",M_INDX,0xA1},{K_LDA,"LDA",M_INDY,0xB1},

  /* STA has no immediate form. */
  {K_STA,"STA",M_ZP,0x85},{K_STA,"STA",M_ZPX,0x95},{K_STA,"STA",M_ABS,0x8D},
  {K_STA,"STA",M_ABSX,0x9D},{K_STA,"STA",M_ABSY,0x99},
  {K_STA,"STA",M_INDX,0x81},{K_STA,"STA",M_INDY,0x91},

  {K_LDX,"LDX",M_IMM,0xA2},{K_LDX,"LDX",M_ZP,0xA6},{K_LDX,"LDX",M_ZPY,0xB6},
  {K_LDX,"LDX",M_ABS,0xAE},{K_LDX,"LDX",M_ABSY,0xBE},

  {K_LDY,"LDY",M_IMM,0xA0},{K_LDY,"LDY",M_ZP,0xA4},{K_LDY,"LDY",M_ZPX,0xB4},
  {K_LDY,"LDY",M_ABS,0xAC},{K_LDY,"LDY",M_ABSX,0xBC},

  {K_STX,"STX",M_ZP,0x86},{K_STX,"STX",M_ZPY,0x96},{K_STX,"STX",M_ABS,0x8E},
  {K_STY,"STY",M_ZP,0x84},{K_STY,"STY",M_ZPX,0x94},{K_STY,"STY",M_ABS,0x8C},

  {K_CPX,"CPX",M_IMM,0xE0},{K_CPX,"CPX",M_ZP,0xE4},{K_CPX,"CPX",M_ABS,0xEC},
  {K_CPY,"CPY",M_IMM,0xC0},{K_CPY,"CPY",M_ZP,0xC4},{K_CPY,"CPY",M_ABS,0xCC},

  {K_BIT,"BIT",M_ZP,0x24},{K_BIT,"BIT",M_ABS,0x2C},

  {K_ASL,"ASL",M_ZP,0x06},{K_ASL,"ASL",M_ZPX,0x16},{K_ASL,"ASL",M_ABS,0x0E},{K_ASL,"ASL",M_ABSX,0x1E},
  {K_LSR,"LSR",M_ZP,0x46},{K_LSR,"LSR",M_ZPX,0x56},{K_LSR,"LSR",M_ABS,0x4E},{K_LSR,"LSR",M_ABSX,0x5E},
  {K_ROL,"ROL",M_ZP,0x26},{K_ROL,"ROL",M_ZPX,0x36},{K_ROL,"ROL",M_ABS,0x2E},{K_ROL,"ROL",M_ABSX,0x3E},
  {K_ROR,"ROR",M_ZP,0x66},{K_ROR,"ROR",M_ZPX,0x76},{K_ROR,"ROR",M_ABS,0x6E},{K_ROR,"ROR",M_ABSX,0x7E},
  {K_INC,"INC",M_ZP,0xE6},{K_INC,"INC",M_ZPX,0xF6},{K_INC,"INC",M_ABS,0xEE},{K_INC,"INC",M_ABSX,0xFE},
  {K_DEC,"DEC",M_ZP,0xC6},{K_DEC,"DEC",M_ZPX,0xD6},{K_DEC,"DEC",M_ABS,0xCE},{K_DEC,"DEC",M_ABSX,0xDE},
};

static const int TABLE_N = (int)(sizeof(TABLE) / sizeof(TABLE[0]));

/* ---- Implied / accumulator / branch / flow ----------------------------------
 * These take no operand from memory, so the matrix above cannot express them.
 * Each carries an explicit expected effect. Kept as a table (rather than folded
 * into the hand-written section) so the coverage counter sees them and so adding
 * a missing opcode is a one-line change.
 *
 * `sets`/`expect` semantics per class:
 *   IMPL_REG  -- expect register `reg_out` in A/X/Y/SP as named by `which`
 *   IMPL_FLAG -- expect (P & mask) == val
 *   IMPL_BR   -- expect PC == pc_out (branch taken/not-taken from p_in)
 */
enum ImplClass { IC_REG, IC_FLAG, IC_BR };
enum WhichReg  { W_A, W_X, W_Y, W_SP, W_NONE };

struct Implied {
    const char *mnem;
    uint8_t     opcode;
    ImplClass   cls;
    uint8_t     a_in, x_in, y_in, p_in, sp_in;
    WhichReg    which;
    uint8_t     reg_out;
    uint8_t     fmask, fval;
    uint16_t    pc_out;      /* IC_BR only */
};

static const Implied IMPLIED[] = {
  /* --- flag set/clear: each must set or clear exactly its own bit ----------- */
  {"CLC",0x18,IC_FLAG,0,0,0,fC,      0xFF,W_NONE,0, fC,0,        0},
  {"SEC",0x38,IC_FLAG,0,0,0,0,       0xFF,W_NONE,0, fC,fC,       0},
  {"CLD",0xD8,IC_FLAG,0,0,0,fD,      0xFF,W_NONE,0, fD,0,        0},
  {"SED",0xF8,IC_FLAG,0,0,0,0,       0xFF,W_NONE,0, fD,fD,       0},
  {"CLI",0x58,IC_FLAG,0,0,0,0x04,    0xFF,W_NONE,0, 0x04,0,      0},
  {"SEI",0x78,IC_FLAG,0,0,0,0,       0xFF,W_NONE,0, 0x04,0x04,   0},
  {"CLV",0xB8,IC_FLAG,0,0,0,fV,      0xFF,W_NONE,0, fV,0,        0},

  /* --- transfers: value moves, and N/Z reflect it (TXS does NOT touch flags) - */
  {"TAX",0xAA,IC_REG,0x81,0x00,0,0,  0xFF,W_X, 0x81, fZ|fN,fN,   0},
  {"TAY",0xA8,IC_REG,0x81,0,0x00,0,  0xFF,W_Y, 0x81, fZ|fN,fN,   0},
  {"TXA",0x8A,IC_REG,0x00,0x7F,0,0,  0xFF,W_A, 0x7F, fZ|fN,0,    0},
  {"TYA",0x98,IC_REG,0x00,0,0x00,0,  0xFF,W_A, 0x00, fZ|fN,fZ,   0},
  {"TSX",0xBA,IC_REG,0,0x00,0,0,     0xC0,W_X, 0xC0, fZ|fN,fN,   0},
  {"TXS",0x9A,IC_REG,0,0xC5,0,0,     0xFF,W_SP,0xC5, 0,0,        0},

  /* --- inc/dec on index registers ----------------------------------------- */
  {"INX",0xE8,IC_REG,0,0xFF,0,0,     0xFF,W_X, 0x00, fZ|fN,fZ,   0},
  {"DEX",0xCA,IC_REG,0,0x00,0,0,     0xFF,W_X, 0xFF, fZ|fN,fN,   0},
  {"INY",0xC8,IC_REG,0,0,0x7F,0,     0xFF,W_Y, 0x80, fZ|fN,fN,   0},
  {"DEY",0x88,IC_REG,0,0,0x01,0,     0xFF,W_Y, 0x00, fZ|fN,fZ,   0},

  /* --- accumulator shifts/rotates ----------------------------------------- */
  {"ASL A",0x0A,IC_REG,0xC0,0,0,0,   0xFF,W_A, 0x80, fC|fZ|fN,fC|fN, 0},
  {"LSR A",0x4A,IC_REG,0x03,0,0,0,   0xFF,W_A, 0x01, fC|fZ|fN,fC,    0},
  {"ROL A",0x2A,IC_REG,0x40,0,0,fC,  0xFF,W_A, 0x81, fC|fZ|fN,fN,    0},
  {"ROR A",0x6A,IC_REG,0x02,0,0,0,   0xFF,W_A, 0x01, fC|fZ|fN,0,     0},

  {"NOP",0xEA,IC_REG,0x5A,0,0,0,     0xFF,W_A, 0x5A, 0,0,        0},

  /* --- branches: each tested in the direction that is TAKEN.
   * Operand $10 at $1000 -> not-taken PC is $1002, taken PC is $1012. --------- */
  {"BPL taken",0x10,IC_BR,0,0,0,0,        0xFF,W_NONE,0,0,0, 0x1012},
  {"BMI taken",0x30,IC_BR,0,0,0,fN,       0xFF,W_NONE,0,0,0, 0x1012},
  {"BVC taken",0x50,IC_BR,0,0,0,0,        0xFF,W_NONE,0,0,0, 0x1012},
  {"BVS taken",0x70,IC_BR,0,0,0,fV,       0xFF,W_NONE,0,0,0, 0x1012},
  {"BCC taken",0x90,IC_BR,0,0,0,0,        0xFF,W_NONE,0,0,0, 0x1012},
  {"BCS taken",0xB0,IC_BR,0,0,0,fC,       0xFF,W_NONE,0,0,0, 0x1012},
  {"BNE taken",0xD0,IC_BR,0,0,0,0,        0xFF,W_NONE,0,0,0, 0x1012},
  {"BEQ taken",0xF0,IC_BR,0,0,0,fZ,       0xFF,W_NONE,0,0,0, 0x1012},
  /* and each in the direction that is NOT taken, so a stuck-always-branch
   * implementation cannot pass by satisfying only the taken cases. */
  {"BPL not taken",0x10,IC_BR,0,0,0,fN,   0xFF,W_NONE,0,0,0, 0x1002},
  {"BMI not taken",0x30,IC_BR,0,0,0,0,    0xFF,W_NONE,0,0,0, 0x1002},
  {"BVC not taken",0x50,IC_BR,0,0,0,fV,   0xFF,W_NONE,0,0,0, 0x1002},
  {"BVS not taken",0x70,IC_BR,0,0,0,0,    0xFF,W_NONE,0,0,0, 0x1002},
  {"BCC not taken",0x90,IC_BR,0,0,0,fC,   0xFF,W_NONE,0,0,0, 0x1002},
  {"BCS not taken",0xB0,IC_BR,0,0,0,0,    0xFF,W_NONE,0,0,0, 0x1002},
  {"BNE not taken",0xD0,IC_BR,0,0,0,fZ,   0xFF,W_NONE,0,0,0, 0x1002},
  {"BEQ not taken",0xF0,IC_BR,0,0,0,0,    0xFF,W_NONE,0,0,0, 0x1002},
};

static const int IMPLIED_N = (int)(sizeof(IMPLIED) / sizeof(IMPLIED[0]));

} // namespace mtx
