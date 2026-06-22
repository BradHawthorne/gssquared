#pragma once
// ============================================================================
// iigs_video_summary.hpp — headless Super Hi-Res video-state summary.
//
// Decodes the Apple IIgs SHR display window (Mega II bank $E1, $2000-$9FFF) the
// way the scanline renderer does and prints a human-readable summary to stdout:
//   (a) the per-line SCB mode/palette histogram (320 vs 640, palette usage),
//   (b) the active palettes as RGB888,
//   (c) the pixel-index histogram decoded in each line's ACTUAL mode.
// This answers "what mode is the screen in, and what did the program actually
// draw?" without hand-decoding the raw write trace — the screenshot alone is
// mode/palette-blind.
//
// Header-only (mirrors bus_trace.hpp), canonical Apple IIgs terminology only.
// The caller gates on getenv("A2GSPU_VIDEOSUM") and passes the already-bound
// const uint8_t *e1 (Mega II bank $E1 base). No MMU, no side effects.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include "iigs_shr_layout.hpp"

// SHR window layout within bank $E1 (indexed e1[off]):
//   pixels   $2000-$9CFF : 200 scanlines x 160 bytes
//   SCBs     $9D00-$9DFF : one byte/scanline; bit7=640 mode, bits0-3=palette#
//   palettes $9E00-$9FFF : 16 palettes x 16 colors x a $0RGB 16-bit LE word
inline void iigs_video_summary(const uint8_t *e1) {
    // (a) per-line SCB mode + palette histogram.
    int n320 = 0, n640 = 0, palcount[16] = {0};
    bool pal_used[16] = {false};
    for (int vc = 0; vc < iigs_shr::LINES; vc++) {
        uint8_t scb = e1[iigs_shr::SCB + vc];
        if (scb & 0x80) n640++; else n320++;
        int p = scb & 0x0F;
        palcount[p]++; pal_used[p] = true;
    }
    printf("IIGS VIDEO: SCB modes: %d lines 320-mode, %d lines 640-mode\n", n320, n640);
    printf("IIGS VIDEO: SCB palette use:");
    for (int p = 0; p < 16; p++) if (palcount[p]) printf(" pal%d=%dL", p, palcount[p]);
    printf("\n");

    // (b) active palettes as RGB888 ($0RGB 12-bit -> 8-bit/channel via *0x11).
    for (int p = 0; p < 16; p++) {
        if (!pal_used[p]) continue;
        printf("IIGS VIDEO: pal%d:", p);
        for (int c = 0; c < 16; c++) {
            int o = iigs_shr::PAL + p * 32 + c * 2;
            uint16_t w = (uint16_t)(e1[o] | (e1[o + 1] << 8));
            int r = ((w >> 8) & 0xF) * 0x11;
            int g = ((w >> 4) & 0xF) * 0x11;
            int b = (w & 0xF) * 0x11;
            printf(" %02X%02X%02X", r, g, b);
        }
        printf("\n");
    }

    // (c) pixel-index histogram, decoded in each line's ACTUAL mode.
    //   320: byte = [hi nibble = left px][lo nibble = right px], index 0..15.
    //   640: byte = 4 dots x 2 bits ([7:6]=dot0 .. [1:0]=dot3); the effective
    //        palette index = 2-bit value + the dot's color sub-bank offset
    //        (dot0:+8, dot1:+12, dot2:+0, dot3:+4) — matches the renderer.
    long hist[16];
    iigs_shr::histogram(e1, hist);              // shared mode-correct decode (see iigs_shr_layout.hpp)
    printf("IIGS VIDEO: pixel-index histogram (mode-correct):");
    for (int i = 0; i < 16; i++) if (hist[i]) printf(" idx%d=%ld", i, hist[i]);
    printf("\n");
}

// Downsampled ASCII map of the SHR screen: 40 cols x 25 rows, each cell = the
// DOMINANT pixel index (one hex digit) in its block. Lets a headless run SEE the
// on-screen layout (e.g. a PaintRect rectangle prints as a block of one digit)
// without a framebuffer or the mode/palette-blind screenshot. Caller gates on
// getenv("A2GSPU_VIDEOMAP"); same const uint8_t *e1 (Mega II bank $E1 base).
inline void iigs_video_map(const uint8_t *e1) {
    static const char HEX[] = "0123456789ABCDEF";
    static const int off640[4] = { 8, 12, 0, 4 };
    const int MAPW = 40, MAPH = 25;
    const int rows_per_cell = iigs_shr::LINES / MAPH;        // 200/25 = 8 lines/cell
    printf("IIGS VIDEOMAP: %dx%d cells, each = dominant pixel index (hex) of an %dx~8 block\n",
           MAPW, MAPH, 320 / MAPW);
    for (int cr = 0; cr < MAPH; cr++) {
        long cellhist[40][16];
        for (int c = 0; c < MAPW; c++) for (int i = 0; i < 16; i++) cellhist[c][i] = 0;
        for (int sub = 0; sub < rows_per_cell; sub++) {
            int vc = cr * rows_per_cell + sub;
            uint8_t scb = e1[iigs_shr::SCB + vc];
            const uint8_t *row = e1 + iigs_shr::PIX + vc * iigs_shr::BYTES_PER_LINE;
            int width = (scb & 0x80) ? 640 : 320;
            for (int px = 0; px < width; px++) {
                int idx;
                if (scb & 0x80) {                            // 640 mode
                    int bx = px >> 2, dot = px & 3;
                    int v = (row[bx] >> (6 - dot * 2)) & 0x3;
                    idx = (v + off640[dot]) & 0xF;
                } else {                                     // 320 mode
                    int bx = px >> 1;
                    idx = (px & 1) ? (row[bx] & 0xF) : ((row[bx] >> 4) & 0xF);
                }
                int col = px * MAPW / width;
                if (col >= MAPW) col = MAPW - 1;
                cellhist[col][idx]++;
            }
        }
        char line[41];
        for (int c = 0; c < MAPW; c++) {
            int best = 0; long bestn = cellhist[c][0];
            for (int i = 1; i < 16; i++) if (cellhist[c][i] > bestn) { bestn = cellhist[c][i]; best = i; }
            line[c] = HEX[best];
        }
        line[MAPW] = '\0';
        printf("IIGS VIDEOMAP: |%s|\n", line);
    }
}
