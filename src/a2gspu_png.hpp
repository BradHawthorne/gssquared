// a2gspu_png.hpp -- minimal, dependency-free PNG writer + HGR decoder.
//
// WHY THIS AND NOT A COMPUTER-VISION LIBRARY
// ------------------------------------------
// The consumer of this telemetry is Claude Code on a login (Max) account, which
// reads images natively -- hand it a PNG and it sees the picture. So the "vision
// model" is already present; what was missing was a way to GET a picture to it:
//
//   * `shot` wrote a .BMP (videosystem.cpp SDL_SaveBMP). BMP is not a format the
//     agent's file reader renders, so the framebuffer was effectively write-only.
//   * It also reads back the SDL RENDERER, so it needs a window/GPU path and its
//     output varies with scaling and host video state -- bad for regression.
//
// So the maximally useful thing is not OpenCV/tesseract, it is: decode the guest's
// hi-res page straight out of emulated RAM and emit a PNG. That is
//   - deterministic (the exact bytes the guest wrote; no renderer in the loop),
//   - headless-safe (no window, no GPU),
//   - and directly viewable by the agent.
//
// Deliberately NOT done here, because memory is exact and pixels are inference:
//   - OCR of the text page -- read $0400-$07FF as ASCII instead (`text` verb).
//   - tile classification from pixels -- read the engine's tile indices instead.
// Use vision for "does this LOOK right", use memory for "what IS this".
//
// Output is 8-bit greyscale. Monochrome is a feature, not a limitation: for
// reading glyph and tile SHAPE, NTSC colour artifacting is noise. Bit 7 of each
// HGR byte is the palette selector, not a pixel, and is excluded.
//
// PNG is written with STORED (uncompressed) deflate blocks, so there is no zlib
// dependency. Files are larger than an optimal encoder would produce; for a
// 280x192 screen at 3x that is a few hundred KB, which is irrelevant here.

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace a2png {

inline uint32_t crc32_of(const uint8_t *p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    static uint32_t tbl[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        init = true;
    }
    for (size_t i = 0; i < n; i++) crc = tbl[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

inline void be32(std::vector<uint8_t> &v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)(x >> 16));
    v.push_back((uint8_t)(x >> 8));  v.push_back((uint8_t)x);
}

inline void chunk(std::vector<uint8_t> &out, const char *type,
                  const uint8_t *data, size_t n) {
    be32(out, (uint32_t)n);
    size_t start = out.size();
    out.insert(out.end(), type, type + 4);
    if (n) out.insert(out.end(), data, data + n);
    uint32_t c = crc32_of(&out[start], out.size() - start) ^ 0xFFFFFFFFu;
    be32(out, c);
}

// 8-bit greyscale PNG. `pix` is h rows of w bytes, top-down.
inline bool write_gray(const char *path, const uint8_t *pix, int w, int h) {
    std::vector<uint8_t> png;
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    png.insert(png.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16); ihdr[2]=(uint8_t)(w>>8); ihdr[3]=(uint8_t)w;
    ihdr[4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16); ihdr[6]=(uint8_t)(h>>8); ihdr[7]=(uint8_t)h;
    ihdr[8]=8;    // bit depth
    ihdr[9]=0;    // colour type 0 = greyscale
    ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;   // deflate / no filter / no interlace
    chunk(png, "IHDR", ihdr, sizeof ihdr);

    // Raw scanlines, each prefixed by filter byte 0.
    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * ((size_t)w + 1));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), pix + (size_t)y * w, pix + (size_t)y * w + w);
    }

    // zlib stream: header, STORED deflate blocks, adler32.
    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t n = raw.size() - off; if (n > 65535) n = 65535;
        bool last = (off + n >= raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8));
        uint16_t nl = (uint16_t)~n;
        z.push_back((uint8_t)(nl & 0xFF)); z.push_back((uint8_t)(nl >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    chunk(png, "IDAT", z.data(), z.size());
    chunk(png, "IEND", nullptr, 0);

    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

// 8-bit RGB PNG. `pix` is h rows of w*3 bytes (RGB), top-down.
inline bool write_rgb(const char *path, const uint8_t *pix, int w, int h) {
    std::vector<uint8_t> png;
    static const uint8_t sig[8] = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    png.insert(png.end(), sig, sig + 8);

    uint8_t ihdr[13];
    ihdr[0]=(uint8_t)(w>>24); ihdr[1]=(uint8_t)(w>>16); ihdr[2]=(uint8_t)(w>>8); ihdr[3]=(uint8_t)w;
    ihdr[4]=(uint8_t)(h>>24); ihdr[5]=(uint8_t)(h>>16); ihdr[6]=(uint8_t)(h>>8); ihdr[7]=(uint8_t)h;
    ihdr[8]=8;    // bit depth
    ihdr[9]=2;    // colour type 2 = truecolour RGB
    ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    chunk(png, "IHDR", ihdr, sizeof ihdr);

    std::vector<uint8_t> raw;
    raw.reserve((size_t)h * ((size_t)w * 3 + 1));
    for (int y = 0; y < h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), pix + (size_t)y * w * 3, pix + (size_t)y * w * 3 + (size_t)w * 3);
    }

    std::vector<uint8_t> z;
    z.push_back(0x78); z.push_back(0x01);
    size_t off = 0;
    while (off < raw.size()) {
        size_t n = raw.size() - off; if (n > 65535) n = 65535;
        bool last = (off + n >= raw.size());
        z.push_back(last ? 1 : 0);
        z.push_back((uint8_t)(n & 0xFF)); z.push_back((uint8_t)(n >> 8));
        uint16_t nl = (uint16_t)~n;
        z.push_back((uint8_t)(nl & 0xFF)); z.push_back((uint8_t)(nl >> 8));
        z.insert(z.end(), raw.begin() + off, raw.begin() + off + n);
        off += n;
    }
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw.size(); i++) { a = (a + raw[i]) % 65521; b = (b + a) % 65521; }
    be32(z, (b << 16) | a);
    chunk(png, "IDAT", z.data(), z.size());
    chunk(png, "IEND", nullptr, 0);

    FILE *f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(png.data(), 1, png.size(), f) == png.size();
    fclose(f);
    return ok;
}

// HGR byte address for row y (0..191), byte column cx (0..39):
//   base + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*$28 + cx
inline uint32_t hgr_addr(uint32_t base, int y, int cx) {
    return base + (uint32_t)((y & 7) << 10)
                + (uint32_t)(((y >> 3) & 7) << 7)
                + (uint32_t)((y >> 6) * 0x28) + (uint32_t)cx;
}

} // namespace a2png
