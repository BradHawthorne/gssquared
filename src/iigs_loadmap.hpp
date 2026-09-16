#pragma once
// ============================================================================
// iigs_loadmap.hpp -- what the System Loader placed where (SHAPE M7).
//
// The ROM's tool sets are named from the ROM, the RAM tool sets from the live
// tool pointer table, and GS/OS's kernel from its own file (iigs_gsos_names.hpp).
// Everything else GS/OS brings in -- the device drivers, the FSTs, the
// SYSTEM.SETUP files, desk accessories, START, the Finder, the application --
// arrives through the System Loader at an address the Memory Manager picks on
// that boot, so no file can hold it. The call tracker sees each load happen:
//
//   InitialLoad2 ($2011) / InitialLoad ($0911)   the caller's pathname (read at
//                                                the call, as the caller passed it)
//     NewHandle ($0902), nested inside the load  a handle the Loader allocated
//   the load's return, carry clear               userID at S+1, start at S+3
//
// At the return every handle still owned by the load's userID (ignoring the
// auxiliary ID nibble) is a block of that load: its master pointer record
// (+0 address, +4 attributes, +6 owner, +8 size) says where it is now and how
// big. A PC is named "Load.<file>+$off" -- "Load.<file>#<n>+$off" for the nth
// block of a multi-block load -- only while that handle's record still says
// the same owner and size and a non-nil address: the Memory Manager's own
// bookkeeping is the evidence, read live through the probe, so a block that
// was purged, disposed, or handed to another owner is unnamed rather than
// misnamed, and a block the Memory Manager moved is named where it now is.
// A load from memory (inputType 2 and 4 carry no pathname) is "Load.uid$XXXX".
//
// Loads are seen only while the call tracker runs: when TRAP, BLAME, TBTRACE
// or CALLSTREAM is armed, or A2GSPU_LOADMAP, which arms it from launch so a
// session that arms a trap later still knows what booted, and which gives the
// loads their own records (loadmap.load / loadmap.fail). Without A2GSPU_LOADMAP
// nothing is printed: a TRAP run's output is unchanged apart from better names.
//
// Included from iigs_symbols.hpp (which declares g_a2gspu_cpu_for_names).
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "a2gspu_observe.hpp"          // a2obs_peek (rule 8)
#include "a2gspu_rails_registry.hpp"   // a2rails::Registrar: the rail registers its reader (M8)
#include "a2record.hpp"                // loadmap.* records
#include "cpu.hpp"

struct IigsLoadBlock {
    uint32_t handle = 0;      // address of the master pointer record
    uint32_t addr = 0;        // where the block was when the load returned
    uint32_t size = 0;
    uint16_t owner = 0;
};
struct IigsLoad {
    int      id = 0;
    uint16_t call = 0;        // $2011 InitialLoad2 | $0911 InitialLoad
    uint16_t input_type = 0;  // InitialLoad2: 0 Pascal string, 1 class-1 string, else not a pathname
    uint32_t caller = 0;
    std::string path;         // as passed; empty for a load from memory
    std::string name;         // "Load.<file>" | "Load.uid$XXXX"
    uint16_t uid = 0;
    uint32_t start = 0;
    int      start_block = -1;   // which block holds start; -1 is a record-layout surprise, reported
    int      frame = 0;
    std::vector<uint32_t> handles;       // NewHandle results while in progress
    std::vector<IigsLoadBlock> blocks;   // what the load owns, at its return
};

inline bool        g_iigs_loadmap_armed = false;   // A2GSPU_LOADMAP: records on, tracker from launch
inline FILE       *g_iigs_loadmap_out = nullptr;
inline std::string g_iigs_loadmap_path;
inline std::vector<IigsLoad> g_iigs_loads;         // completed, oldest first
inline std::vector<IigsLoad> g_iigs_loading;       // in progress, innermost last
inline int         g_iigs_load_seq = 0;
inline int         g_iigs_loadmap_armed_frame = -1;  // the frame A2GSPU_LOADMAP was armed at (loads before it were seen only if another rail ran the tracker)
inline unsigned    g_iigs_loads_failed = 0;

// Pending-call tags the call tracker keeps beside a call it hands us.
enum : uint8_t { IIGS_LOADTAG_NONE = 0, IIGS_LOADTAG_LOAD = 1, IIGS_LOADTAG_NEWHANDLE = 2 };

inline uint16_t iigs_lm_rd16(MMU *m, uint32_t a) {
    return (uint16_t)(a2obs_peek(m, a & 0xFFFFFF) | (a2obs_peek(m, (a + 1) & 0xFFFFFF) << 8));
}
inline uint32_t iigs_lm_rd32(MMU *m, uint32_t a) {
    return (uint32_t)iigs_lm_rd16(m, a) | ((uint32_t)iigs_lm_rd16(m, a + 2) << 16);
}
// Bank-0 stack address S+k (native mode: the full 16-bit S).
inline uint32_t iigs_lm_s(cpu_state *cpu, int k) { return (uint32_t)((cpu->sp + k) & 0xFFFF); }

inline std::string iigs_lm_string(MMU *m, uint32_t ptr, bool class1) {
    ptr &= 0xFFFFFF;
    unsigned len = class1 ? iigs_lm_rd16(m, ptr) : a2obs_peek(m, ptr);
    const uint32_t at = ptr + (class1 ? 2 : 1);
    if (len > 255) len = 255;
    std::string s;
    for (unsigned i = 0; i < len; i++) {
        const uint8_t c = a2obs_peek(m, (at + i) & 0xFFFFFF) & 0x7F;
        s += (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    return s;
}

// A call the tracker detected. Returns the tag to keep with the pending call.
inline uint8_t iigs_loadmap_call(cpu_state *cpu, uint16_t callword, uint32_t caller) {
    MMU *m = cpu->mmu;
    if (callword == 0x2011 || callword == 0x0911) {
        // Parameters, top of stack first, above the 3-byte return address:
        //   InitialLoad2: inputType(2) flagWord(2) pathname(4) userID(2)
        //   InitialLoad:  spMemFlag(2) pathname(4) userID(2)
        IigsLoad l;
        l.id = ++g_iigs_load_seq;
        l.call = callword;
        l.caller = caller & 0xFFFFFF;
        l.frame = a2record::frame_now();
        if (callword == 0x2011) {
            l.input_type = iigs_lm_rd16(m, iigs_lm_s(cpu, 4));
            if (l.input_type <= 1) l.path = iigs_lm_string(m, iigs_lm_rd32(m, iigs_lm_s(cpu, 8)), l.input_type == 1);
        } else {
            l.path = iigs_lm_string(m, iigs_lm_rd32(m, iigs_lm_s(cpu, 6)), false);
        }
        g_iigs_loading.push_back(std::move(l));
        return IIGS_LOADTAG_LOAD;
    }
    if (callword == 0x0902 && !g_iigs_loading.empty()) return IIGS_LOADTAG_NEWHANDLE;
    return IIGS_LOADTAG_NONE;
}

inline void iigs_loadmap_write(const IigsLoad &l, bool ok, uint16_t err) {
    if (!g_iigs_loadmap_armed) return;
    a2record::Record rec("loadmap", ok ? "load" : "fail", a2record::frame_now(), a2record::cycles_now());
    rec.i("id", l.id).str("call", l.call == 0x2011 ? "InitialLoad2" : "InitialLoad").pc("caller", l.caller);
    if (l.call == 0x2011) rec.u("input_type", l.input_type);
    if (l.path.empty()) rec.null("path"); else rec.str("path", l.path.c_str());
    if (!ok) {
        rec.u("err", err);
    } else {
        rec.str("name", l.name.c_str()).u("uid", l.uid).pc("start", l.start).i("start_block", l.start_block);
        std::string arr = "[";
        char b[160];
        for (size_t i = 0; i < l.blocks.size(); i++) {
            const IigsLoadBlock &k = l.blocks[i];
            snprintf(b, sizeof b, "%s{\"handle\":\"%02X:%04X\",\"addr\":\"%02X:%04X\",\"size\":%u,\"owner\":%u}",
                     i ? "," : "", (unsigned)(k.handle >> 16), (unsigned)(k.handle & 0xFFFF),
                     (unsigned)(k.addr >> 16), (unsigned)(k.addr & 0xFFFF), (unsigned)k.size, (unsigned)k.owner);
            arr += b;
        }
        arr += "]";
        rec.raw("blocks", arr.c_str());
    }
    if (FILE *sink = a2record::sink(g_iigs_loadmap_out)) rec.write(sink, true);
    else printf("A2GSPU LOADMAP: %s\n", rec.done().c_str());
}

// The matching return. carry/a are the call's result.
inline void iigs_loadmap_ret(cpu_state *cpu, uint8_t tag, int load_id, bool carry, uint16_t a) {
    MMU *m = cpu->mmu;
    if (tag == IIGS_LOADTAG_NEWHANDLE) {
        if (!carry && !g_iigs_loading.empty()) g_iigs_loading.back().handles.push_back(iigs_lm_rd32(m, iigs_lm_s(cpu, 1)) & 0xFFFFFF);
        return;
    }
    if (tag != IIGS_LOADTAG_LOAD) return;
    int at = -1;
    for (int i = (int)g_iigs_loading.size() - 1; i >= 0; i--)
        if (g_iigs_loading[i].id == load_id) { at = i; break; }
    if (at < 0) return;
    IigsLoad l = std::move(g_iigs_loading[at]);
    g_iigs_loading.erase(g_iigs_loading.begin() + at);
    if (carry) { g_iigs_loads_failed++; iigs_loadmap_write(l, false, a); return; }
    l.uid = iigs_lm_rd16(m, iigs_lm_s(cpu, 1));
    l.start = iigs_lm_rd32(m, iigs_lm_s(cpu, 3)) & 0xFFFFFF;
    if (!l.path.empty()) {
        size_t cut = l.path.find_last_of(":/");
        l.name = "Load." + (cut == std::string::npos ? l.path : l.path.substr(cut + 1));
    } else {
        char b[24]; snprintf(b, sizeof b, "Load.uid$%04X", l.uid);
        l.name = b;
    }
    for (size_t hi = 0; hi < l.handles.size(); hi++) {
        const uint32_t h = l.handles[hi];
        bool seen = false;                          // a handle disposed and handed out again
        for (size_t j = 0; j < hi && !seen; j++) seen = l.handles[j] == h;
        if (seen) continue;
        IigsLoadBlock k;
        k.handle = h;
        k.addr  = iigs_lm_rd32(m, h) & 0xFFFFFF;
        k.owner = iigs_lm_rd16(m, h + 6);
        k.size  = iigs_lm_rd32(m, h + 8);
        if (!k.addr || !k.size || (k.owner & 0xF0FF) != (l.uid & 0xF0FF)) continue;   // the Loader's own buffers
        if (l.start_block < 0 && l.start >= k.addr && l.start < k.addr + k.size) l.start_block = (int)l.blocks.size();
        l.blocks.push_back(k);
    }
    l.handles.clear();
    iigs_loadmap_write(l, true, 0);
    g_iigs_loads.push_back(std::move(l));
    if (g_iigs_loads.size() > 512) g_iigs_loads.erase(g_iigs_loads.begin());
}

// The live block of a load that holds pc, as [lo, hi): true only while the
// Memory Manager's record still says owner, size and a non-nil address.
inline bool iigs_loadmap_find(uint32_t pc, const IigsLoad *&lout, size_t &bout, uint32_t &addr_out) {
    if (g_iigs_loads.empty() || !g_a2gspu_cpu_for_names) return false;
    MMU *m = g_a2gspu_cpu_for_names->mmu;
    pc &= 0xFFFFFF;
    for (size_t li = g_iigs_loads.size(); li-- > 0;) {
        const IigsLoad &l = g_iigs_loads[li];
        for (size_t bi = 0; bi < l.blocks.size(); bi++) {
            const IigsLoadBlock &k = l.blocks[bi];
            const uint32_t addr = iigs_lm_rd32(m, k.handle) & 0xFFFFFF;
            if (!addr || pc < addr || pc >= addr + k.size) continue;
            if (iigs_lm_rd16(m, k.handle + 6) != k.owner || iigs_lm_rd32(m, k.handle + 8) != k.size) continue;
            lout = &l; bout = bi; addr_out = addr;
            return true;
        }
    }
    return false;
}
inline bool iigs_loadmap_block_of(uint32_t pc, uint32_t &lo, uint32_t &hi) {
    const IigsLoad *l = nullptr; size_t bi = 0; uint32_t addr = 0;
    if (!iigs_loadmap_find(pc, l, bi, addr)) return false;
    lo = addr; hi = addr + l->blocks[bi].size;
    return true;
}

// "Load.<file>[#n]+$off" while the Memory Manager still says the block is the load's.
inline const char *iigs_loadmap_name_pc(uint32_t pc, char *buf, size_t n) {
    buf[0] = 0;
    const IigsLoad *l = nullptr; size_t bi = 0; uint32_t addr = 0;
    if (!iigs_loadmap_find(pc, l, bi, addr)) return buf;
    const uint32_t off = (pc & 0xFFFFFF) - addr;
    if (l->blocks.size() > 1) snprintf(buf, n, "%s#%u+$%X", l->name.c_str(), (unsigned)bi + 1, (unsigned)off);
    else if (off)             snprintf(buf, n, "%s+$%X", l->name.c_str(), (unsigned)off);
    else                      snprintf(buf, n, "%s", l->name.c_str());
    return buf;
}

// `rail read A2GSPU_LOADMAP`: what is known, and whether each load is still there.
inline void iigs_loadmap_format(char *out, size_t n) {
    size_t off = 0;
    int w = snprintf(out, n, "status=OK A2GSPU_LOADMAP armed=%d armed_frame=%d sink=%s loads=%u failed=%u in_progress=%u |",
                     g_iigs_loadmap_armed ? 1 : 0, g_iigs_loadmap_armed_frame,
                     g_iigs_loadmap_out ? g_iigs_loadmap_path.c_str() : (g_iigs_loadmap_armed ? (a2record::g_global ? "records" : "stdout") : "none"),
                     (unsigned)g_iigs_loads.size(), g_iigs_loads_failed, (unsigned)g_iigs_loading.size());
    if (w < 0) return;
    off = (size_t)w;
    MMU *m = g_a2gspu_cpu_for_names ? g_a2gspu_cpu_for_names->mmu : nullptr;
    for (const IigsLoad &l : g_iigs_loads) {
        if (off + 96 >= n) { snprintf(out + off, n - off, " ..."); return; }
        unsigned live = 0;
        for (const IigsLoadBlock &k : l.blocks)
            if (m && iigs_lm_rd32(m, k.handle) & 0xFFFFFF && iigs_lm_rd16(m, k.handle + 6) == k.owner && iigs_lm_rd32(m, k.handle + 8) == k.size) live++;
        w = snprintf(out + off, n - off, " %s@%02X:%04X blocks=%u live=%u;", l.name.c_str(),
                     (unsigned)(l.start >> 16), (unsigned)(l.start & 0xFFFF), (unsigned)l.blocks.size(), live);
        if (w < 0) return;
        off += (size_t)w;
    }
}
inline a2rails::Registrar g_reader_loadmap{"A2GSPU_LOADMAP", iigs_loadmap_format};
