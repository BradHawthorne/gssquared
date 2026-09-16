#pragma once
#include <cstdint>
// ============================================================================
// iigs_shr_layout.hpp - the canonical Super Hi-Res memory layout within Mega II
// bank $E1. One reference so the diagnostics (and any new SHR code) don't
// re-derive the offsets by hand. The golden-critical paths (the spike's $E1 hash in a2gspu_spike.cpp
// window + bus_trace.hpp's BUS_TRACE_SHR_LO/HI) deliberately keep their own
// already-named constants - these MUST equal the values below; this header is
// the single documented source they agree with.
//
// Scan-line Control Byte (Apple IIgs Hardware Reference Table 4-19 / Fig 4-19):
//   bit 7 (0x80)  640 mode (1=640, 0=320)
//   bit 6 (0x40)  scanline interrupt enable
//   bit 5 (0x20)  fill mode (320 only): a 0 nibble repeats the previous color
//                 on that line. First-pixel 0 is undefined on the VGC
//                 (leftover; Brooks/MAME table is in the scan generator).
//   bit 4         reserved
//   bits 3-0      palette number 0-15
// $40 is IRQ, not fill. $20 is fill, not 640. Do not rotate these bits.
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

    inline constexpr uint8_t SCB_PALETTE = 0x0F;
    inline constexpr uint8_t SCB_FILL    = 0x20;  // bit 5, 320 only
    inline constexpr uint8_t SCB_IRQ     = 0x40;  // bit 6
    inline constexpr uint8_t SCB_640     = 0x80;  // bit 7

    // Decode one scanline to displayed palette indices (320 or 640 entries).
    // Fill (bit 5, 320 only) expands a 0 nibble as a repeat of the previous
    // color on that line. Diagnostics start last=0 (HW first-pixel-0 leftover
    // is latched in the scan generator from SCB&0x1F; idxN of a line whose
    // first nibble is non-zero matches the display either way).
    inline int line_width(uint8_t scb) {
        return (scb & SCB_640) ? 640 : 320;
    }
    inline void decode_line_indices(const uint8_t *row, uint8_t scb, uint8_t *idx_out) {
        static const int off640[4] = { 8, 12, 0, 4 };
        if (scb & SCB_640) {
            for (int bx = 0; bx < BYTES_PER_LINE; bx++) {
                uint8_t b = row[bx];
                for (int sub = 0; sub < 4; sub++) {
                    int v = (b >> (6 - sub * 2)) & 0x3;
                    idx_out[bx * 4 + sub] = (uint8_t)((v + off640[sub]) & 0xF);
                }
            }
        } else {
            const bool fill = (scb & SCB_FILL) != 0;
            uint8_t last = 0;
            for (int bx = 0; bx < BYTES_PER_LINE; bx++) {
                uint8_t b = row[bx];
                uint8_t hi = (uint8_t)((b >> 4) & 0xF);
                uint8_t lo = (uint8_t)(b & 0xF);
                if (fill) {
                    if (hi == 0) hi = last; else last = hi;
                    if (lo == 0) lo = last; else last = lo;
                }
                idx_out[bx * 2]     = hi;
                idx_out[bx * 2 + 1] = lo;
            }
        }
    }

    // Mode+fill-correct per-palette-index pixel histogram of the whole SHR
    // window: each line decoded in its ACTUAL 320/640 mode (640 dot offsets
    // {dot0:+8,dot1:+12,dot2:+0,dot3:+4}), and 320 fill-mode zeros expanded
    // to the previous color. hist must hold 16 longs. ONE source so the video
    // summary and the assertion gate agree on "how many pixels are colour N".
    inline void histogram(const uint8_t *e1, long hist[16]) {
        uint8_t idx[640];
        for (int i = 0; i < 16; i++) hist[i] = 0;
        for (int vc = 0; vc < LINES; vc++) {
            uint8_t scb = e1[SCB + vc];
            const uint8_t *row = e1 + PIX + vc * BYTES_PER_LINE;
            int w = line_width(scb);
            decode_line_indices(row, scb, idx);
            for (int px = 0; px < w; px++) hist[idx[px] & 0xF]++;
        }
    }
}
