/*
 * decimal_clark.h -- ADC and SBC in decimal mode, exhaustively, against Bruce Clark's
 * "Decimal Mode" tutorial, Appendix A (6502.org). The reference below is Appendix A's
 * sequences written out as given, not derived from the CPU core:
 *
 *   Seq.1 (every part, ADC accumulator and carry)
 *     AL = (A & $0F) + (B & $0F) + C;  if AL >= $0A: AL = ((AL + $06) & $0F) + $10
 *     A  = (A & $F0) + (B & $F0) + AL; if A >= $A0: A = A + $60
 *     accumulator = A & $FF; C = A >= $100
 *   Seq.2 (ADC N and V where the part takes them from it)
 *     AL as Seq.1; A = (A & $F0) + (B & $F0) + AL in signed arithmetic
 *     N = bit 7 of A; V = A < -128 or A > 127
 *   Seq.3 (SBC accumulator, 6502 and 65816)
 *     AL = (A & $0F) - (B & $0F) + C - 1; if AL < 0: AL = ((AL - $06) & $0F) - $10
 *     A  = (A & $F0) - (B & $F0) + AL;     if A < 0: A = A - $60
 *   Seq.4 (SBC accumulator, 65C02)
 *     AL = (A & $0F) - (B & $0F) + C - 1; A = A - B + C - 1
 *     if A < 0: A = A - $60;  if AL < 0: A = A - $06
 *
 *   NMOS 6502   ADC: A,C Seq.1; Z binary; N,V Seq.2.   SBC: A Seq.3; N,V,Z,C binary.
 *   65C02       ADC: A,C Seq.1; N,Z the result; V Seq.2. SBC: A Seq.4; N,Z the result; V,C binary.
 *   65816       ADC as the 65C02.                       SBC: A Seq.3; N,Z the result; V,C binary.
 *
 * The 65816's 16-bit decimal mode has no appendix. It is checked only with valid BCD
 * operands, where the answer is plain decimal arithmetic: the result and carry of the
 * 4-digit sum or difference, N and Z from the result, V for ADC by Seq.2 carried to 16
 * bits (the sign of (A & $F000) + (B & $F000) + the three low digits' corrected sum), and
 * V for SBC binary.
 */
#pragma once

#include <cstdint>
#include <cstdio>

namespace clark {

struct Out { uint8_t a; bool c, z, n, v; };

inline Out adc8(processor_type t, uint8_t A, uint8_t B, bool C) {
    int AL = (A & 0x0F) + (B & 0x0F) + (C ? 1 : 0);
    if (AL >= 0x0A) AL = ((AL + 0x06) & 0x0F) + 0x10;
    int S1 = (A & 0xF0) + (B & 0xF0) + AL;               // Seq.1 before the $60
    int S2 = (int8_t)(A & 0xF0) + (int8_t)(B & 0xF0) + AL;  // Seq.2, signed
    Out o;
    int acc = S1 >= 0xA0 ? S1 + 0x60 : S1;
    o.a = (uint8_t)acc;
    o.c = acc >= 0x100;
    o.v = S2 < -128 || S2 > 127;
    if (t == PROCESSOR_6502) {
        o.z = (uint8_t)(A + B + (C ? 1 : 0)) == 0;
        o.n = (S2 & 0x80) != 0;
    } else {
        o.z = o.a == 0;
        o.n = (o.a & 0x80) != 0;
    }
    return o;
}

inline Out sbc8(processor_type t, uint8_t A, uint8_t B, bool C) {
    int c = C ? 1 : 0;
    int bin = A - B + c - 1;
    uint8_t bin8 = (uint8_t)bin;
    Out o;
    int AL = (A & 0x0F) - (B & 0x0F) + c - 1;
    int acc;
    if (t == PROCESSOR_65C02) {                           // Seq.4
        acc = A - B + c - 1;
        if (acc < 0) acc -= 0x60;
        if (AL < 0) acc -= 0x06;
    } else {                                              // Seq.3
        if (AL < 0) AL = ((AL - 0x06) & 0x0F) - 0x10;
        acc = (A & 0xF0) - (B & 0xF0) + AL;
        if (acc < 0) acc -= 0x60;
    }
    o.a = (uint8_t)acc;
    o.c = bin >= 0;
    o.v = ((A ^ B) & 0x80) != 0 && ((A ^ bin8) & 0x80) != 0;
    if (t == PROCESSOR_6502) {
        o.z = bin8 == 0;
        o.n = (bin8 & 0x80) != 0;
    } else {
        o.z = o.a == 0;
        o.n = (o.a & 0x80) != 0;
    }
    return o;
}

struct Out16 { uint16_t a; bool c, z, n, v; };

inline int dec4(uint16_t x) { return ((x >> 12) & 15) * 1000 + ((x >> 8) & 15) * 100 + ((x >> 4) & 15) * 10 + (x & 15); }
inline uint16_t bcd4(int n) { return (uint16_t)((n / 1000) << 12 | ((n / 100) % 10) << 8 | ((n / 10) % 10) << 4 | n % 10); }

inline Out16 adc16(uint16_t A, uint16_t B, bool C) {
    int sum = dec4(A) + dec4(B) + (C ? 1 : 0);
    Out16 o;
    o.a = bcd4(sum % 10000);
    o.c = sum > 9999;
    int low3 = dec4(A & 0x0FFF) + dec4(B & 0x0FFF) + (C ? 1 : 0);   // 0..1999
    int s2 = (int16_t)(A & 0xF000) + (int16_t)(B & 0xF000) + (low3 >= 1000 ? 0x1000 : 0) + bcd4(low3 % 1000);
    o.v = s2 < -32768 || s2 > 32767;
    o.z = o.a == 0;
    o.n = (o.a & 0x8000) != 0;
    return o;
}

inline Out16 sbc16(uint16_t A, uint16_t B, bool C) {
    int d = dec4(A) - dec4(B) - 1 + (C ? 1 : 0);
    int bin = (int)A - (int)B - 1 + (C ? 1 : 0);
    uint16_t bin16 = (uint16_t)bin;
    Out16 o;
    o.a = bcd4(d < 0 ? d + 10000 : d);
    o.c = bin >= 0;
    o.v = ((A ^ B) & 0x8000) != 0 && ((A ^ bin16) & 0x8000) != 0;
    o.z = o.a == 0;
    o.n = (o.a & 0x8000) != 0;
    return o;
}

}  // namespace clark
