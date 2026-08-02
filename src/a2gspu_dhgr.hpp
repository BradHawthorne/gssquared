// a2gspu_dhgr.hpp -- shared discrete DHGR 4-dot colour model + profile export.
//
// FIRST PRINCIPLES
// ----------------
// DHGR colour (discrete Mega-II / Virtual-II class model) is a sliding 4-dot
// window keyed by phase (x mod 4). Bit 0 of each byte is the leftmost dot; bit
// 7 is never displayed. AUX supplies the first 7 dots of a column, MAIN the next 7.
//
// Window convention (matches a2engine tools/calibrate.py + a2tile render):
//   window = state | (bit << 3);   // bit3 = CURRENT, bit0 = oldest
//   rgb    = palette[window][phase];
//   state  = ((state >> 1) | (bit << 2)) & 7;
//
// RGB table is a2engine hardware-order LORES colours, rotated into the phase-3
// base table the same way ii-pix / a2tile build four-dot palettes. This is the
// *named* art oracle for discrete colour — not the HGR RGB LUT (wrong model).
//
// File formats (a2tile loads these):
//   GSDHGR4D  version:u32 depth:u32=4  then 16*4*3 u8 RGB
//   GSNTSC01  version:u32 taps:u32     then 4 * 2^(2*taps+1) * RGBA8

#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "devices/displaypp/RGBA.hpp"
#include "display/ntsc.hpp"
#include "house_fnv.hpp"

// Defined in display/ntsc.cpp; also used by NTSC560.hpp
extern RGBA_t g_hgr_LUT[4][(1 << ((NUM_TAPS * 2) + 1))];

namespace a2dhgr {

// a2engine APPLE2_16 hardware-order RGB (matches tools/palette.py)
static const uint8_t LORES_RGB[16][3] = {
    {0x00, 0x00, 0x00}, // 0 Black
    {0xB0, 0x02, 0x58}, // 1 Magenta
    {0x1E, 0x2B, 0xFF}, // 2 Dark Blue
    {0xCE, 0x2D, 0xFF}, // 3 Purple
    {0x00, 0x7E, 0x28}, // 4 Dark Green
    {0x80, 0x80, 0x80}, // 5 Grey 1
    {0x00, 0xA9, 0xFF}, // 6 Medium Blue
    {0x9E, 0xAB, 0xFF}, // 7 Light Blue
    {0x61, 0x54, 0x00}, // 8 Brown
    {0xFF, 0x56, 0x00}, // 9 Orange
    {0xA2, 0xA9, 0xB1}, // 10 Grey 2
    {0xFF, 0x81, 0xD7}, // 11 Pink
    {0x31, 0xD2, 0x00}, // 12 Green
    {0xE1, 0xD4, 0x00}, // 13 Yellow
    {0x4F, 0xFD, 0xA7}, // 14 Aqua
    {0xFF, 0xFF, 0xFF}, // 15 White
};

inline uint8_t ror4(uint8_t v) { return (uint8_t)(((v >> 1) | (v << 3)) & 0x0F); }
inline uint8_t rol4(uint8_t v) { return (uint8_t)(((v << 1) | (v >> 3)) & 0x0F); }
inline uint8_t lores_to_seq(uint8_t idx) { return ror4(idx); }

// palette[window 0..15][phase 0..3] as RGBA
struct Discrete4 {
    RGBA_t lut[16][4];

    Discrete4() { build(); }

    void build() {
        // phase3 base: sequence -> RGB for phase 3
        uint8_t phase3[16][3];
        memset(phase3, 0, sizeof phase3);
        for (int lores = 0; lores < 16; lores++) {
            uint8_t seq = lores_to_seq((uint8_t)lores);
            phase3[seq][0] = LORES_RGB[lores][0];
            phase3[seq][1] = LORES_RGB[lores][1];
            phase3[seq][2] = LORES_RGB[lores][2];
        }
        // rotate into all phases (ii-pix / a2tile _four_dot_palette)
        for (int pattern = 0; pattern < 16; pattern++) {
            int value = pattern;
            lut[value][3] = RGBA_t::make(phase3[pattern][0], phase3[pattern][1],
                                         phase3[pattern][2], 0xFF);
            for (int phase = 0; phase < 3; phase++) {
                int lsb = value & 1;
                value = ((value >> 1) | (lsb << 3)) & 0x0F;
                lut[value][phase] = RGBA_t::make(phase3[pattern][0], phase3[pattern][1],
                                                 phase3[pattern][2], 0xFF);
            }
        }
    }

    RGBA_t color(uint8_t window, int phase) const {
        return lut[window & 0x0F][phase & 3];
    }
};

// Stateful 4-dot decoder for one scanline.
struct Decoder4 {
    uint8_t state = 0; // 3-bit history
    int x = 0;
    int phase_offset = 0; // DHGR uses 1 in Comp/RGB generators
    const Discrete4 *pal;

    explicit Decoder4(const Discrete4 *p, int poff = 0) : phase_offset(poff), pal(p) {}

    void reset(int poff = 0) {
        state = 0;
        x = 0;
        phase_offset = poff;
    }

    RGBA_t push(bool bit) {
        uint8_t window = (uint8_t)((state & 7) | ((bit ? 1u : 0u) << 3));
        int phase = (phase_offset + x) & 3;
        RGBA_t c = pal->color(window, phase);
        state = (uint8_t)(((state >> 1) | ((bit ? 1u : 0u) << 2)) & 7);
        x++;
        return c;
    }
};

// Solid LORES colour: repeating 4-dot sequence
inline void solid_dots(uint8_t lores_idx, bool *out, int n) {
    uint8_t seq = lores_to_seq(lores_idx);
    for (int i = 0; i < n; i++)
        out[i] = (seq >> (i & 3)) & 1;
}

// FNV-1a-64 over 8K AUX then 8K MAIN (profile=vram-raw). Same contract as CTRL
// `dhgr-golden` and SPIKE `A2GSPU_DHGR_GOLDEN` so bless/compare is one number.
inline uint64_t page_fnv64(const uint8_t *aux_8k, const uint8_t *main_8k) {
    uint64_t h = HOUSE_FNV_BASIS;
    for (int i = 0; i < 0x2000; i++) {
        h ^= (uint64_t)aux_8k[i];
        h *= HOUSE_FNV_PRIME;
    }
    for (int i = 0; i < 0x2000; i++) {
        h ^= (uint64_t)main_8k[i];
        h *= HOUSE_FNV_PRIME;
    }
    return h;
}

// Expand one DHGR page (aux 8K + main 8K) into 560*192 RGB using discrete 4-dot.
// aux_base / main_base point at $2000 of each bank image.
inline void render_page_rgb(
    const uint8_t *aux, const uint8_t *main, uint8_t *rgb_out /* 560*192*3 */,
    int phase_offset = 1) {
    static Discrete4 pal;
    auto hgr_addr = [](int y, int cx) -> uint32_t {
        return (uint32_t)((((y & 7) << 10) | (((y >> 3) & 7) << 7)) + (y >> 6) * 0x28 + cx);
    };
    for (int y = 0; y < 192; y++) {
        Decoder4 dec(&pal, phase_offset);
        for (int cx = 0; cx < 40; cx++) {
            uint32_t a = hgr_addr(y, cx);
            uint8_t ba = aux[a];
            uint8_t bm = main[a];
            for (int k = 0; k < 7; k++) {
                bool bit = (ba >> k) & 1;
                RGBA_t c = dec.push(bit);
                size_t o = ((size_t)y * 560 + (size_t)cx * 14 + k) * 3;
                rgb_out[o] = c.r;
                rgb_out[o + 1] = c.g;
                rgb_out[o + 2] = c.b;
            }
            for (int k = 0; k < 7; k++) {
                bool bit = (bm >> k) & 1;
                RGBA_t c = dec.push(bit);
                size_t o = ((size_t)y * 560 + (size_t)cx * 14 + 7 + k) * 3;
                rgb_out[o] = c.r;
                rgb_out[o + 1] = c.g;
                rgb_out[o + 2] = c.b;
            }
        }
    }
}

// FNV-1a-64 over discrete 4-dot RGB (560×192×3). profile=4dot — catches palette
// / phase / model drift that bit-identical vram-raw would miss if the table
// changed under the same bits. phase_offset=1 matches pngc / Comp generators.
inline uint64_t page_rgb_fnv64(const uint8_t *aux_8k, const uint8_t *main_8k,
                               int phase_offset = 1) {
    static std::vector<uint8_t> rgb;
    rgb.resize((size_t)560 * 192 * 3);
    render_page_rgb(aux_8k, main_8k, rgb.data(), phase_offset);
    uint64_t h = HOUSE_FNV_BASIS;
    for (size_t i = 0; i < rgb.size(); i++) {
        h ^= (uint64_t)rgb[i];
        h *= HOUSE_FNV_PRIME;
    }
    return h;
}

// Named golden profiles for DHGR page gates.
enum class GoldenProfile { VramRaw, Dot4 };

inline bool parse_golden_profile(const char *s, GoldenProfile *out) {
    if (!s || !s[0] || !out) return false;
    if (!strcmp(s, "vram-raw") || !strcmp(s, "vram") || !strcmp(s, "raw")) {
        *out = GoldenProfile::VramRaw;
        return true;
    }
    if (!strcmp(s, "4dot") || !strcmp(s, "4dot-rgb") || !strcmp(s, "a2engine") ||
        !strcmp(s, "gssquared")) {
        *out = GoldenProfile::Dot4;
        return true;
    }
    return false;
}

inline const char *golden_profile_name(GoldenProfile p) {
    return p == GoldenProfile::Dot4 ? "4dot" : "vram-raw";
}

inline uint64_t page_hash(const uint8_t *aux_8k, const uint8_t *main_8k,
                          GoldenProfile prof) {
    if (prof == GoldenProfile::Dot4)
        return page_rgb_fnv64(aux_8k, main_8k, /*phase_offset=*/1);
    return page_fnv64(aux_8k, main_8k);
}

// SPIKE / harness golden file: missing → bless write; corrupt → hard fail;
// present → MATCH/DIFF. Returns gate_rc contribution (0 pass, 1 fail).
// tag is the log prefix (e.g. "DHGR GOLDEN").
inline int golden_gate_file(const char *path, uint64_t cur_hash, const char *tag,
                            const char *profile = "vram-raw") {
    FILE *gp = fopen(path, "r");
    if (gp) {
        unsigned long long g = 0;
        if (fscanf(gp, "%llx", &g) == 1) {
            int match = (g == (unsigned long long)cur_hash);
            printf("%s: %s (cur=%016llX want=%016llX) profile=%s\n",
                   tag, match ? "MATCH" : "DIFF",
                   (unsigned long long)cur_hash, g, profile ? profile : "?");
            fclose(gp);
            return match ? 0 : 1;
        }
        printf("%s: ERROR — golden file '%s' exists but contains no parseable "
               "hash (cur=%016llX); failing gate.\n",
               tag, path, (unsigned long long)cur_hash);
        fclose(gp);
        return 1;
    }
    gp = fopen(path, "w");
    if (!gp) {
        printf("%s: ERROR — cannot write bless file '%s'\n", tag, path);
        return 1;
    }
    fprintf(gp, "%016llX\n", (unsigned long long)cur_hash);
    fclose(gp);
    printf("%s: blessed %s = %016llX profile=%s\n",
           tag, path, (unsigned long long)cur_hash, profile ? profile : "?");
    return 0;
}

inline bool export_4dot_bin(const char *path) {
    Discrete4 pal;
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite("GSDHGR4D", 1, 8, f);
    uint32_t ver = 1, depth = 4;
    fwrite(&ver, 4, 1, f);
    fwrite(&depth, 4, 1, f);
    for (int w = 0; w < 16; w++)
        for (int p = 0; p < 4; p++) {
            uint8_t rgb[3] = {pal.lut[w][p].r, pal.lut[w][p].g, pal.lut[w][p].b};
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
    return true;
}

// Export the live NTSC LUT (must call init_hgr_LUT first).
// g_hgr_LUT lives in global namespace (display/ntsc.cpp), not a2dhgr::.
inline bool export_ntsc_lut_bin(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fwrite("GSNTSC01", 1, 8, f);
    uint32_t ver = 1, taps = NUM_TAPS;
    fwrite(&ver, 4, 1, f);
    fwrite(&taps, 4, 1, f);
    const int nwin = 1 << ((NUM_TAPS * 2) + 1);
    for (int ph = 0; ph < 4; ph++) {
        for (int w = 0; w < nwin; w++) {
            RGBA_t c = g_hgr_LUT[ph][w];
            uint8_t rgba[4] = {c.r, c.g, c.b, c.a};
            fwrite(rgba, 1, 4, f);
        }
    }
    fclose(f);
    return true;
}

// Self-test: solid fields round-trip at phase-aligned samples.
inline int calibrate_solid_roundtrip() {
    Discrete4 pal;
    int fails = 0;
    for (int c = 0; c < 16; c++) {
        bool dots[32];
        solid_dots((uint8_t)c, dots, 32);
        Decoder4 dec(&pal, 0);
        RGBA_t samples[8];
        int ns = 0;
        for (int x = 0; x < 32; x++) {
            RGBA_t rgb = dec.push(dots[x]);
            if (x >= 16 && (x & 3) == 3 && ns < 8)
                samples[ns++] = rgb;
        }
        // nearest LORES
        for (int i = 0; i < ns; i++) {
            int best = 0;
            int best_d = 1 << 30;
            for (int j = 0; j < 16; j++) {
                int dr = (int)samples[i].r - LORES_RGB[j][0];
                int dg = (int)samples[i].g - LORES_RGB[j][1];
                int db = (int)samples[i].b - LORES_RGB[j][2];
                int d = dr * dr + dg * dg + db * db;
                if (d < best_d) {
                    best_d = d;
                    best = j;
                }
            }
            if (best != c) {
                fails++;
                break;
            }
        }
    }
    return fails; // 0 = PASS
}

} // namespace a2dhgr
