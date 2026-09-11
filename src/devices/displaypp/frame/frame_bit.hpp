#pragma once

#include <cstdint>

#define FB_BITSTREAM_WIDTH 560
#define FB_BITSTREAM_WIDTH_WORDS ((FB_BITSTREAM_WIDTH / 64)+1)
#define FB_BITSTREAM_HEIGHT 192

class Frame_Bitstream {
private:
    uint64_t display_bitstream[FB_BITSTREAM_HEIGHT][FB_BITSTREAM_WIDTH_WORDS];

    uint64_t working;
    uint16_t f_width; // purely informational, for consumers
    uint16_t f_height;
    uint16_t scanline;
    uint16_t hpos; // bit position in working
    uint64_t *hloc;

public:
    Frame_Bitstream(uint16_t width, uint16_t height);  // pixels
    ~Frame_Bitstream();
    void print();

    inline void close_line() {
        if (hpos == 0) return;
        working <<= (64 - hpos); // shift last bits into place for read
        *hloc = working;
        hloc++;
    };

    inline void push(bool bit) { 
        working = (working << 1) | bit;
        hpos++;
        if (hpos>=64) { 
            *hloc = working; 
            hloc++;
            hpos = 0;
            working = 0;
        } 
    };

    inline bool pull() { 
        bool bit = (working & ((uint64_t)1 << 63)) != 0; 
        hpos++; 
        if (hpos>=64) { 
            working = *hloc;
            hloc++;
            hpos = 0;
        }
        return bit;
    };
    
    inline void read_line(int line) { 
        scanline = line; 
        hpos = 0;
        hloc = &display_bitstream[scanline][0];
        working = *hloc; // preload first word
        hloc++;
    };

    inline void write_line(int line) { 
        scanline = line; 
        hpos = 0;
        hloc = &display_bitstream[scanline][0];
        working = 0;
    };

    void clear();
};
