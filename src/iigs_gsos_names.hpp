#pragma once
// ============================================================================
// iigs_gsos_names.hpp -- GS/OS kernel names by default (SHAPE M7).
//
// The ROM's tool sets and the RAM tool sets on the live pointer table were
// named; GS/OS itself was not, so a PC in the SCM, the Dispatcher or the System
// Loader printed as unnamed. tools/a2rail/gsosmap.py reads the boot volume's own
// SYSTEM/GS.OS and writes assets/symbols/apple2gs/gsos.segs: each segment's load
// address, length and name, and the house-FNV of each 256-byte page -- no kernel
// code. A vector table (the System Dispatch Table) is kept as the opcode of each
// 4-byte slot instead, since GS/OS installs the targets at run time. Here a PC
// inside a segment is named "GSOS.<Name>+$off" only while the live bytes of its
// page hash-match the file (or the live slot opcodes match the table's):
//   * segments overlap (the Init Manager parts load over others during boot),
//   * language-card banking takes some of them out of view,
//   * a ProDOS 8 program replaces the kernel entirely,
// so an address alone is not evidence, and a page that does not match stays
// unnamed rather than misnamed. Reads go through the MMU probe; a verdict is
// cached per page for the frame it was taken in.
//
// Included from iigs_symbols.hpp, which declares g_a2gspu_cpu_for_names and
// g_a2gspu_base_path.
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>

struct IigsGsosSeg {
    uint32_t addr = 0, len = 0;
    uint16_t flags = 0;
    std::string name;
    std::vector<uint64_t> pages;
    std::vector<uint8_t> slotops;   // a vector table: the opcode of each 4-byte slot (targets are live data)
};
inline std::vector<IigsGsosSeg> g_iigs_gsos_segs;
inline bool g_iigs_gsos_tried = false;
struct IigsGsosVerdict { int frame; bool ok; };
inline std::unordered_map<uint64_t, IigsGsosVerdict> g_iigs_gsos_cache;   // (segment, page) -> verdict

inline void iigs_gsos_segs_load() {
    if (g_iigs_gsos_tried) return;
    g_iigs_gsos_tried = true;
    char path[1024];
    snprintf(path, sizeof path, "%ssymbols/apple2gs/gsos.segs", g_a2gspu_base_path.c_str());
    FILE *f = fopen(path, "rb");
    if (!f) f = fopen("assets/symbols/apple2gs/gsos.segs", "rb");
    if (!f) return;                                 // no map: GS/OS PCs stay unnamed, as before
    char line[1024];
    IigsGsosSeg *cur = nullptr;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "SEG ", 4)) {
            unsigned a = 0, l = 0, fl = 0; char nm[64] = {0};
            if (sscanf(line + 4, "%x %u %x %63s", &a, &l, &fl, nm) == 4) {
                g_iigs_gsos_segs.push_back({});
                cur = &g_iigs_gsos_segs.back();
                cur->addr = a & 0xFFFFFF; cur->len = l; cur->flags = (uint16_t)fl; cur->name = nm;
            } else cur = nullptr;
        } else if (!strncmp(line, "SLOTOPS ", 8) && cur) {
            for (const char *p = line + 8; p[0] && p[1] && p[0] != '\n' && p[0] != '\r'; p += 2) {
                char hx[3] = { p[0], p[1], 0 };
                cur->slotops.push_back((uint8_t)strtoul(hx, nullptr, 16));
            }
        } else if (!strncmp(line, "PAGES ", 6) && cur) {
            char *p = line + 6;
            while (*p) {
                while (*p == ' ') p++;
                if (!*p || *p == '\n' || *p == '\r') break;
                char *e = nullptr;
                const uint64_t h = strtoull(p, &e, 16);
                if (e == p) break;
                cur->pages.push_back(h);
                p = e;
            }
        } else if (!strncmp(line, "END", 3) || !strncmp(line, "MAP ", 4)) {
            cur = nullptr;
        }
    }
    fclose(f);
}

// "GSOS.<Name>+$off" when the PC's page is the kernel's, else "".
inline const char *iigs_gsos_name_pc(uint32_t pc, char *buf, size_t n) {
    buf[0] = 0;
    iigs_gsos_segs_load();
    if (g_iigs_gsos_segs.empty() || !g_a2gspu_cpu_for_names) return buf;
    pc &= 0xFFFFFF;
    const int frame = a2record::frame_now();
    for (size_t si = 0; si < g_iigs_gsos_segs.size(); si++) {
        const IigsGsosSeg &s = g_iigs_gsos_segs[si];
        if (pc < s.addr || pc >= s.addr + s.len) continue;
        const uint32_t off = pc - s.addr, page = off >> 8;
        if (!s.slotops.empty()) {
            // A vector table: every slot's opcode must be the file's; the jump
            // targets GS/OS installed are not compared.
            const uint64_t tkey = ((uint64_t)si << 32) | 0xFFFFFFFFu;
            auto tt = g_iigs_gsos_cache.find(tkey);
            bool tok;
            if (tt != g_iigs_gsos_cache.end() && tt->second.frame == frame && frame >= 0) tok = tt->second.ok;
            else {
                MMU *mmu = g_a2gspu_cpu_for_names->mmu;
                tok = true;
                for (size_t k = 0; k < s.slotops.size() && tok; k++) {
                    const uint32_t a = s.addr + (uint32_t)(4 * k);
                    tok = mmu->probe_readable(a) && mmu->probe_peek(a) == s.slotops[k];
                }
                g_iigs_gsos_cache[tkey] = { frame, tok };
            }
            if (!tok) continue;
            snprintf(buf, n, "GSOS.%s+$%X", s.name.c_str(), off);
            return buf;
        }
        if (page >= s.pages.size()) continue;
        const uint64_t key = ((uint64_t)si << 32) | page;
        auto it = g_iigs_gsos_cache.find(key);
        bool ok;
        if (it != g_iigs_gsos_cache.end() && it->second.frame == frame && frame >= 0) {
            ok = it->second.ok;
        } else {
            const uint32_t base = s.addr + (page << 8);
            const uint32_t plen = (s.len - (page << 8)) < 256 ? (s.len - (page << 8)) : 256;
            uint64_t h = 1469598103934665603ULL;                           // house FNV basis
            MMU *mmu = g_a2gspu_cpu_for_names->mmu;
            ok = true;
            for (uint32_t k = 0; k < plen; k++) {
                if (!mmu->probe_readable(base + k)) { ok = false; break; }
                h ^= mmu->probe_peek(base + k); h *= 1099511628211ULL;
            }
            ok = ok && h == s.pages[page];
            g_iigs_gsos_cache[key] = { frame, ok };
        }
        if (!ok) continue;                          // an overlapping segment may still own this page
        if (off == 0) snprintf(buf, n, "GSOS.%s", s.name.c_str());
        else snprintf(buf, n, "GSOS.%s+$%X", s.name.c_str(), off);
        return buf;
    }
    return buf;
}
