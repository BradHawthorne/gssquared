#pragma once

#include <cstdint>

#include "devices/displaypp/RGBA.hpp"

struct SHRColor {
    union {
        struct {
            uint8_t b:4; 
            uint8_t g:4;
            uint8_t r:4;
            uint8_t _x:4;
        };
        uint16_t v;
    };
};

struct SHRMode {
    // Hardware Reference Table 4-19: bit7=640, bit6=IRQ, bit5=FILL, bits3-0=palette.
    // $40 is scanline IRQ (not fill). $20 is fill (320 nibble-repeat). $80 is 640.
    union {
        struct {
            uint8_t p:4;          // bits 3-0: palette 0-15
            uint8_t _unused:1;    // bit 4: reserved
            uint8_t fill:1;       // bit 5 (0x20): fill mode, 320 only
            uint8_t interrupt:1;  // bit 6 (0x40): scanline IRQ
            uint8_t mode640:1;    // bit 7 (0x80): 1=640, 0=320
        };
        uint8_t v;
    };
};

// VGC leftover fill index when a 320 fill-mode line starts with a 0 nibble.
// Hardware testing by John Brooks; same table as MAME. Indexed by SCB & 0x1F
// (not vcount) when that line's SCB is latched.
inline constexpr uint8_t shr_fillmode_init[32] = {
    2, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf,
};

struct Palette {
    SHRColor colors[16]; // 12-bit color values
    RGBA_t active[16] = {0}; 
    /* RGBA_t color[16];
    RGBA_t mono[16]; */
};

struct SHR {
    uint8_t pixels[32000];
    SHRMode modes[200];
    uint8_t unused1[56];
    Palette palettes[16];
};

// Convert 12-bit color value to 24-bit color value
inline RGBA_t convert12bitTo24bit(SHRColor c12) {
    RGBA_t c24;

    // Expand each 4-bit component to 8 bits
    // By duplicating the 4 bits to fill the 8-bit space
    c24.r = (c12.r << 4) | c12.r;
    c24.g = (c12.g << 4) | c12.g;
    c24.b = (c12.b << 4) | c12.b;
    c24.a = 0xFF;
    // Combine into 24-bit color value
    return c24;
}

inline RGBA_t convert24bitColorToMono(RGBA_t c24) {
    uint16_t luma = (c24.r * 0.299 + c24.g * 0.587 + c24.b * 0.114);
    return RGBA_t::make(luma, luma, luma, 0xFF);
}

template<int pixelno>
inline uint8_t pixel640(uint8_t pval) {
    return (pval & (0b11 << (pixelno*2)))>>(pixelno*2);
}

template<int pixelno>
inline uint8_t pixel320(uint8_t pval) {
    return (pval & (0b1111 << (pixelno*4)))>>(pixelno*4);
}
