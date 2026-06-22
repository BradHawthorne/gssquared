#pragma once
#include <cstdint>
// ============================================================================
// iigs_shr_layout.hpp — the canonical Super Hi-Res memory layout within Mega II
// bank $E1. One reference so the diagnostics (and any new SHR code) don't
// re-derive the offsets by hand. The golden-critical paths (gs2.cpp's $E1 hash
// window + bus_trace.hpp's BUS_TRACE_SHR_LO/HI) deliberately keep their own
// already-named constants — these MUST equal the values below; this header is
// the single documented source they agree with.
// ============================================================================
namespace iigs_shr {
    inline constexpr uint32_t MEGAII_E1      = 0x10000;  // bank $E1 in the 128KB Mega II image
    inline constexpr uint32_t PIX            = 0x2000;   // pixel data    ($E1:$2000-$9CFF)
    inline constexpr uint32_t SCB            = 0x9D00;   // per-line SCBs  ($E1:$9D00-$9DFF)
    inline constexpr uint32_t PAL            = 0x9E00;   // 16 palettes    ($E1:$9E00-$9FFF)
    inline constexpr uint32_t WIN_LO         = 0x2000;   // SHR window lo  (pixels+SCB+pal)
    inline constexpr uint32_t WIN_HI         = 0x9FFF;   // SHR window hi
    inline constexpr int      BYTES_PER_LINE = 160;
    inline constexpr int      LINES          = 200;
    // matches bus_trace.hpp's BUS_TRACE_SHR_LO/HI (Mega II linear index space)
    inline constexpr uint32_t WIN_LO_M2      = MEGAII_E1 + WIN_LO;  // 0x12000
    inline constexpr uint32_t WIN_HI_M2      = MEGAII_E1 + WIN_HI;  // 0x19FFF

    // Mode-correct per-palette-index pixel histogram of the whole SHR window: each
    // line decoded in its ACTUAL 320/640 mode (640 dot offsets {dot0:+8,dot1:+12,
    // dot2:+0,dot3:+4}). hist must hold 16 longs. ONE source so the video summary
    // and the assertion gate agree on "how many pixels are colour N".
    inline void histogram(const uint8_t *e1, long hist[16]) {
        static const int off640[4] = { 8, 12, 0, 4 };
        for (int i = 0; i < 16; i++) hist[i] = 0;
        for (int vc = 0; vc < LINES; vc++) {
            uint8_t scb = e1[SCB + vc];
            const uint8_t *row = e1 + PIX + vc * BYTES_PER_LINE;
            if (scb & 0x80) {                      // 640 mode
                for (int bx = 0; bx < BYTES_PER_LINE; bx++) {
                    uint8_t b = row[bx];
                    for (int sub = 0; sub < 4; sub++) {
                        int v = (b >> (6 - sub * 2)) & 0x3;
                        hist[(v + off640[sub]) & 0xF]++;
                    }
                }
            } else {                               // 320 mode
                for (int bx = 0; bx < BYTES_PER_LINE; bx++) {
                    uint8_t b = row[bx];
                    hist[(b >> 4) & 0xF]++;
                    hist[b & 0xF]++;
                }
            }
        }
    }
}
