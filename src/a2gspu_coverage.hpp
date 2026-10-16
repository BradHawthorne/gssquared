// a2gspu_coverage.hpp -- execution-coverage bitmap (A2GSPU_COVERAGE)
//
// WHY THIS EXISTS
// ---------------
// The hardest problem in disassembling a 40-year-old binary is the code/data
// split.  A static disassembler must GUESS: it follows flow, applies heuristics,
// and where it cannot decide it either invents an instruction inside a data table
// or gives up and calls real code "data".  Both failures are silent, and both
// poison everything downstream -- a wrong split produces a confidently wrong
// architecture document.
//
// The emulator already knows the answer.  Whatever the CPU executed is code, by
// definition.  This records exactly that: one bit per byte, set when the byte was
// part of an executed instruction.  Fed back into deasmiigs (--coverage), it
// turns the central heuristic into a MEASUREMENT.
//
// Coverage is a lower bound, never an upper one: a byte that never executed in a
// given run is "not proven code", not "proven data".  Consumers must treat it as
// positive evidence only.  Union several runs (different game paths) to widen it.
//
// Design notes:
//  * Marks consumed instruction bytes in the CPU's program-fetch helpers. The
//    selected core already applies the preinstruction M/X/E operand widths.
//    Phantom reads, data reads and vector reads are excluded; short branches
//    cannot prove skipped bytes. No next instruction is needed to finish a span.
//  * The 65C02's reserved NOPs consume their encoded operands without bus reads;
//    their existing decoded byte count supplies that span explicitly.
//  * All CPU variants share the hooks. Program bytes wrap within their bank.
//  * Off by default: a predictable untaken branch per program-fetch helper.

#pragma once
#include "house_fnv.hpp"   // the artifact hash uses the house basis, like every golden
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <algorithm>
#include <map>

// ---- state -------------------------------------------------------------------
// C++17 inline variables (the idiom already used by iigs_toolbox.hpp's g_* rail
// state).  These MUST NOT be `extern` with the definition in gs2.cpp: the CPU
// core (libgs2_cpu_new) references them, so every target that links the CPU
// library without gs2.cpp -- cycletest, cycletest816, cputest, mmutest -- would
// fail to link with "undefined reference to g_cov_*".
inline bool      g_cov_on      = false;
inline uint32_t  g_cov_lo      = 0;
inline uint32_t  g_cov_hi      = 0;
inline uint8_t  *g_cov_bits    = nullptr;
inline size_t    g_cov_nbytes  = 0;
inline uint64_t  g_cov_marked  = 0;   // distinct bytes marked (stats only)
struct A2PCCount { uint64_t instructions = 0, cycles = 0; };
inline bool g_cov_counts_on = false;
inline std::map<uint32_t, A2PCCount> g_cov_counts;
inline bool a2gspu_cov_counts_on() {
    if (!g_cov_on) return false;
    g_cov_counts.clear(); g_cov_counts_on = true; return true;
}

struct a2cov_map {
    uint32_t index, base;
    std::vector<uint8_t> expected, bits, starts;
};
inline std::vector<a2cov_map> g_cov_maps;
inline std::vector<uint8_t> g_cov_plan;
inline std::vector<size_t> g_cov_map_order;
inline uint64_t g_cov_generation = 0;
inline uint64_t g_cov_run_id = 0;
inline uint32_t g_cov_start_pc = 0;
inline uint8_t g_cov_start_mode = 0;
inline size_t g_cov_current_map = (size_t)-1, g_cov_current_offset = 0, g_cov_current_bytes = 0;
inline char g_cov_map_error[160] = {};

// What the last successful write actually produced. There are TWO coverage
// formats and the CTRL `cov write` ack named neither, so an agent that asked for
// coverage was told "status=OK cov wrote <file>" and had to guess which reader
// to point at it -- and a harness that guessed A2COV1 for a mapped run got an
// exception, or worse popcounted a 32-byte header plus the whole embedded plan
// as though it were coverage (Event E of INSTRUMENTATION_SHAPE.md, again). The
// hash is the same house-FNV the file carries, so a consumer can prove it read
// what was written without re-deriving the format's layout first (P4).
inline const char *g_cov_last_format = "-";
inline uint64_t    g_cov_last_hash   = 0;

// Mapped coverage measures source bytes, excluding address-space gaps between
// independently loaded segments. Raw range coverage keeps its original span.
static inline uint32_t a2gspu_cov_span() {
    if(!g_cov_on)return 0;
    if(g_cov_maps.empty())return g_cov_hi-g_cov_lo+1;
    uint32_t span=0;
    for(const auto &m:g_cov_maps)span+=(uint32_t)m.expected.size();
    return span;
}

static inline void a2gspu_cov_begin(uint32_t pc, uint8_t mode) {
    g_cov_start_pc=pc; g_cov_start_mode=mode;
    g_cov_current_map=(size_t)-1;g_cov_current_bytes=0;
    auto at=std::upper_bound(g_cov_map_order.begin(),g_cov_map_order.end(),pc,
        [](uint32_t address,size_t index){return address<g_cov_maps[index].base;});
    if(at!=g_cov_map_order.begin()) {
        size_t index=*--at;const auto &m=g_cov_maps[index];
        if(pc>=m.base && pc-m.base<m.expected.size()) {
            g_cov_current_map=index;g_cov_current_offset=pc-m.base;
        }
    }
}
static inline bool a2gspu_cov_mapped_byte(uint32_t pc, uint8_t value) {
    if(g_cov_map_error[0]) return false;
    // A mapped instruction must have its whole extent in this source segment.
    // Never attach the tail of an instruction that began in a different image.
    if(g_cov_current_map==(size_t)-1)return false;
    auto &m=g_cov_maps[g_cov_current_map];
    size_t off=g_cov_current_offset+g_cov_current_bytes++;
    if(off>=m.expected.size() || pc!=m.base+off) {
        snprintf(g_cov_map_error,sizeof(g_cov_map_error),"instruction at $%06X crosses mapping/program-bank boundary",g_cov_start_pc);
        return false;
    }
        if(value!=m.expected[off]) {
            snprintf(g_cov_map_error,sizeof(g_cov_map_error),
                     "executed byte mismatch at $%06X: expected=$%02X observed=$%02X; rebind overlay",pc,m.expected[off],value);
            return false;
        }
        if(pc==g_cov_start_pc) {
            if(m.starts[off] && m.starts[off]!=g_cov_start_mode) {
                snprintf(g_cov_map_error,sizeof(g_cov_map_error),"ambiguous execution widths at $%06X; collect separate generations",pc);
                return false;
            }
            m.starts[off]=g_cov_start_mode;
        }
        m.bits[off/8]|=(uint8_t)(1u<<(off%8));
        return true;
}
static inline bool a2gspu_cov_map_write(const char *path);

static inline void a2gspu_cov_mark(uint32_t a) {
    if (a < g_cov_lo || a > g_cov_hi) return;
    uint32_t i = a - g_cov_lo;
    uint8_t m = (uint8_t)(1u << (i & 7));
    uint8_t *p = &g_cov_bits[i >> 3];
    if (!(*p & m)) { *p |= m; g_cov_marked++; }
}

// Called only for bytes consumed as opcode/operand, before PC advances. This
// observes the existing execution path; it never performs an extra memory read.
static inline void a2gspu_cov_fetch(uint32_t pc, uint32_t bytes = 1, const uint8_t *values = nullptr) {
    const uint32_t bank = pc & 0xFF0000u;
    for (uint32_t k = 0; k < bytes; ++k) {
        uint32_t addr=bank | ((pc + k) & 0xFFFFu);
        if(!g_cov_maps.empty() && (!values || !a2gspu_cov_mapped_byte(addr,values[k]))) continue;
        a2gspu_cov_mark(addr);
    }
}

// A2GSPU_COVERAGE="LO-HI" or "BANK:LO-HI" (hex).  Returns true if enabled.
static inline bool a2gspu_cov_init(const char *spec) {
    if (!spec || !spec[0]) return false;
    const char *p = strchr(spec, ':');
    uint32_t bank = 0;
    bool banked = false;
    if (p) { bank = (uint32_t)strtoul(spec, nullptr, 16) & 0xFF; p++; banked = true; }
    else p = spec;
    const char *dash = strchr(p, '-');
    if (!dash) {
        fprintf(stderr, "A2GSPU_COVERAGE: bad range '%s' (want LO-HI or BANK:LO-HI)\n", spec);
        return false;
    }
    uint32_t lo = (uint32_t)strtoul(p, nullptr, 16);
    uint32_t hi = (uint32_t)strtoul(dash + 1, nullptr, 16);
    // Validate what we are actually going to USE, not the raw text.  Silently
    // masking the offsets to 16 bits is how "E0:E000-E0FFF" armed $E0E000-$E00FFF:
    // raw hi > lo so the old check passed, the mask then inverted the pair, and
    // the (hi - lo) underflow asked calloc for half a gigabyte -- and answered
    // status=OK, having written a 512 MB file of nothing.
    if (banked && (lo > 0xFFFF || hi > 0xFFFF)) {
        fprintf(stderr, "A2GSPU_COVERAGE: offset out of range in '%s' -- with a "
                        "BANK: prefix both ends must be $0000-$FFFF\n", spec);
        return false;
    }
    uint32_t flo = banked ? ((bank << 16) | lo) : (lo & 0xFFFFFF);
    uint32_t fhi = banked ? ((bank << 16) | hi) : (hi & 0xFFFFFF);
    if (fhi < flo) {
        fprintf(stderr, "A2GSPU_COVERAGE: HI < LO ($%06X-$%06X) in '%s'\n", flo, fhi, spec);
        return false;
    }
    // Re-arming must not leak the previous bitmap, and a failed re-arm must not
    // leave the old range live and collecting: disarm first, then commit.
    g_cov_maps.clear();g_cov_plan.clear();g_cov_map_order.clear();g_cov_map_error[0]=0;
    g_cov_counts.clear(); g_cov_counts_on = false;
    free(g_cov_bits);
    g_cov_bits = nullptr;
    g_cov_on = false;
    g_cov_lo = flo;
    g_cov_hi = fhi;
    g_cov_nbytes = ((size_t)(g_cov_hi - g_cov_lo) >> 3) + 1;
    g_cov_bits = (uint8_t *)calloc(g_cov_nbytes, 1);
    if (!g_cov_bits) { fprintf(stderr, "A2GSPU_COVERAGE: out of memory\n"); return false; }
    g_cov_marked = 0;
    g_cov_on = true;
    ++g_cov_generation;
    fprintf(stderr, "A2GSPU COVERAGE: enabled $%06X-$%06X (%zu bytes of bitmap)\n",
            g_cov_lo, g_cov_hi, g_cov_nbytes);
    return true;
}

// Clear the accumulated bits without disturbing the range.  This is what makes
// coverage *scopable*: reset at a decision point, perform one activity, dump, and
// the bitmap describes THAT activity alone (e.g. "the code a shop conversation
// touches") instead of everything since boot.
inline void a2gspu_cov_reset() {
    if (!g_cov_on || !g_cov_bits) return;
    memset(g_cov_bits, 0, g_cov_nbytes);
    g_cov_marked = 0;
    g_cov_counts.clear();
    for(auto &m:g_cov_maps) { std::fill(m.bits.begin(),m.bits.end(),0);std::fill(m.starts.begin(),m.starts.end(),0); }
    g_cov_map_error[0]=0;
    ++g_cov_generation;
}

// Disarm and release the bitmap.  Symmetric with cov_init so that arm/disarm
// cycles across a long session don't leak one bitmap per arm.
inline void a2gspu_cov_off() {
    g_cov_counts.clear(); g_cov_counts_on = false;
    g_cov_maps.clear();g_cov_plan.clear();g_cov_map_order.clear();g_cov_map_error[0]=0;
    free(g_cov_bits);
    g_cov_bits = nullptr;
    g_cov_nbytes = 0;
    g_cov_marked = 0;
    g_cov_on = false;
}

// Separate JSON artifact: existing A2COV1/A2COV2 bytes stay compatible.
static inline bool a2gspu_cov_counts_write(const char *path) {
    if (!g_cov_on || !g_cov_counts_on || g_cov_map_error[0] || !path || !*path) return false;
    uint64_t total_instructions=0,total_cycles=0;
    for (const auto &item : g_cov_counts) {
        if (UINT64_MAX-total_instructions < item.second.instructions || UINT64_MAX-total_cycles < item.second.cycles) return false;
        total_instructions+=item.second.instructions; total_cycles+=item.second.cycles;
    }
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    uint64_t map_hash = g_cov_plan.empty() ? 0 : HOUSE_FNV_BASIS;
    for (uint8_t b : g_cov_plan) { map_hash ^= b; map_hash *= HOUSE_FNV_PRIME; }
    bool ok = fprintf(f,"{\"schema\":\"a2profile-v1\",\"model\":\"instruction-cycles\",\"lo\":%u,\"hi\":%u,\"generation\":%llu,\"map_hash\":\"%016llX\",\"total_instructions\":%llu,\"total_cycles\":%llu,\"pcs\":[",
        g_cov_lo,g_cov_hi,(unsigned long long)g_cov_generation,(unsigned long long)map_hash,
        (unsigned long long)total_instructions,(unsigned long long)total_cycles) >= 0;
    bool first = true;
    for (const auto &item : g_cov_counts) {
        ok = (fprintf(f,"%s{\"pc\":%u,\"instructions\":%llu,\"cycles\":%llu}",first?"":",",item.first,
            (unsigned long long)item.second.instructions,(unsigned long long)item.second.cycles) >= 0) && ok;
        first = false;
    }
    ok = (fprintf(f,"]}\n") >= 0) && ok;
    return fclose(f) == 0 && ok;
}

// On-disk format (little-endian), deliberately trivial so any consumer can read it:
//   0  : "A2COV1\0"   8 bytes (7 chars + NUL)
//   8  : lo           uint32
//   12 : hi           uint32
//   16 : bitmap       ceil((hi-lo+1)/8) bytes, bit i = byte (lo+i) executed
// Returns true only if a file actually landed on disk.  Callers MUST propagate
// this: a caller that answers "wrote" unconditionally turns a silent no-op into
// a reported success, which is the worst failure mode instrumentation has.
static inline bool a2gspu_cov_write(const char *path) {
    if (!g_cov_on || !g_cov_bits || !path || !path[0]) return false;
    if(!g_cov_maps.empty()) return a2gspu_cov_map_write(path);
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "A2GSPU COVERAGE: cannot write '%s'\n", path); return false; }
    fwrite("A2COV1\0", 1, 8, f);
    uint32_t lo = g_cov_lo, hi = g_cov_hi;
    fwrite(&lo, 4, 1, f); fwrite(&hi, 4, 1, f);
    fwrite(g_cov_bits, 1, g_cov_nbytes, f);
    fclose(f);
    uint32_t span = g_cov_hi - g_cov_lo + 1;
    // The body's house-FNV hash goes on the line, so a reader can prove it read
    // what was written (tools/a2rail/artifacts.py recomputes it). A reader once
    // counted this file's header as coverage for a week; nothing compared.
    uint64_t h = HOUSE_FNV_BASIS;
    for (size_t i = 0; i < g_cov_nbytes; i++) { h ^= g_cov_bits[i]; h *= HOUSE_FNV_PRIME; }
    g_cov_last_format = "A2COV1"; g_cov_last_hash = h;
    fprintf(stderr, "A2GSPU COVERAGE: wrote %s -- %llu/%u bytes executed (%.1f%%) range=$%06X-$%06X hash=%016llX\n",
            path, (unsigned long long)g_cov_marked, span,
            span ? (100.0 * (double)g_cov_marked / (double)span) : 0.0,
            g_cov_lo, g_cov_hi, (unsigned long long)h);
    return true;
}

static inline uint32_t a2cov_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void a2cov_put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));
}
static inline void a2cov_put64(uint8_t *p,uint64_t n) {
    a2cov_put32(p,(uint32_t)n);a2cov_put32(p+4,(uint32_t)(n>>32));
}
// A2MAP1 is emitted by deasmiigs --coverage-plan. Rebinding starts a distinct
// generation; no union with prior occupants of these runtime addresses. Only
// executed bytes are compared, so ordinary mutable data is permitted.
static inline bool a2gspu_cov_map_load(const char *path) {
    a2gspu_cov_off(); // A failed binding must not leave the old overlay armed.
    FILE *f=fopen(path,"rb");
    if(!f){snprintf(g_cov_map_error,sizeof(g_cov_map_error),"cannot open mapping plan");return false;}
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return false;}
    long end=ftell(f);
    if(end<56 || end>0x1000000+12*65536+56){fclose(f);snprintf(g_cov_map_error,sizeof(g_cov_map_error),"plan size invalid");return false;}
    rewind(f);std::vector<uint8_t> plan((size_t)end);
    bool read_ok=fread(plan.data(),1,plan.size(),f)==plan.size();fclose(f);
    uint32_t count=a2cov_u32(plan.data()+8),form=a2cov_u32(plan.data()+12);
    if(!read_ok || memcmp(plan.data(),"A2MAP1\0",8) || !count || count>65536 || (form!=1 && form!=2)) {
        snprintf(g_cov_map_error,sizeof(g_cov_map_error),"invalid A2MAP1 header");return false;
    }
    std::vector<a2cov_map> maps;size_t p=56,total=0;uint32_t lo=0xFFFFFF,hi=0;
    for(uint32_t i=0;i<count;i++) {
        if(p>plan.size() || plan.size()-p<12){snprintf(g_cov_map_error,sizeof(g_cov_map_error),"truncated map");return false;}
        a2cov_map m;m.index=a2cov_u32(plan.data()+p);m.base=a2cov_u32(plan.data()+p+4);
        uint32_t len=a2cov_u32(plan.data()+p+8);p+=12;
        if(!len || m.base>=0x1000000 || len>0x1000000-m.base || len>plan.size()-p || total>0x1000000-len ||
           (form==1 && (count!=1 || m.index!=0)) || (form==2 && !m.index)) {
            snprintf(g_cov_map_error,sizeof(g_cov_map_error),"invalid mapped segment range");return false;
        }
        m.expected.assign(plan.begin()+p,plan.begin()+p+len);p+=len;total+=len;
        m.bits.assign((len+7)/8,0);m.starts.assign(len,0);
        if(m.base<lo)lo=m.base;if(m.base+len-1>hi)hi=m.base+len-1;
        maps.push_back(std::move(m));
    }
    if(p!=plan.size()){snprintf(g_cov_map_error,sizeof(g_cov_map_error),"trailing plan data");return false;}
    std::vector<size_t> order(maps.size());for(size_t i=0;i<maps.size();i++)order[i]=i;
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return maps[a].index<maps[b].index;});
    for(size_t i=1;i<order.size();i++)if(maps[order[i]].index==maps[order[i-1]].index) {
        snprintf(g_cov_map_error,sizeof(g_cov_map_error),"duplicate segment map");return false;
    }
    std::sort(order.begin(),order.end(),[&](size_t a,size_t b){return maps[a].base<maps[b].base;});
    for(size_t i=1;i<order.size();i++)if(maps[order[i]].base<maps[order[i-1]].base+maps[order[i-1]].expected.size()) {
        snprintf(g_cov_map_error,sizeof(g_cov_map_error),"overlapping maps; bind overlays separately");return false;
    }
    char range[32];snprintf(range,sizeof(range),"%06X-%06X",lo,hi);
    if(!a2gspu_cov_init(range))return false;
    if(!g_cov_run_id) g_cov_run_id=(uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count() | 1u;
    g_cov_maps=std::move(maps);g_cov_plan=std::move(plan);
    g_cov_map_order=std::move(order);
    return true;
}

static inline bool a2gspu_cov_map_write(const char *path) {
    if(g_cov_map_error[0]) {fprintf(stderr,"A2GSPU COVERAGE: refusing stale/ambiguous evidence: %s\n",g_cov_map_error);return false;}
    FILE *f=fopen(path,"wb");if(!f)return false;
    uint8_t header[32]={0};memcpy(header,"A2COV2\0",8);
    a2cov_put32(header+8,(uint32_t)g_cov_plan.size());
    a2cov_put64(header+16,g_cov_run_id);a2cov_put64(header+24,g_cov_generation);
    uint64_t hash=HOUSE_FNV_BASIS;
    auto write=[&](const uint8_t *bytes,size_t size) {
        for(size_t i=0;i<size;i++){hash^=bytes[i];hash*=HOUSE_FNV_PRIME;}
        return fwrite(bytes,1,size,f)==size;
    };
    bool ok=write(header,sizeof(header)) && write(g_cov_plan.data(),g_cov_plan.size());
    for(const auto &m:g_cov_maps) if(ok) ok=
        write(m.bits.data(),m.bits.size()) && write(m.starts.data(),m.starts.size());
    uint8_t sum[8];a2cov_put64(sum,hash);
    if(ok)ok=fwrite(sum,1,8,f)==8;
    if(fclose(f))ok=false;
    // hash= APPENDED, at the end of the line. The A2COV1 line has carried the
    // body's house-FNV since the week a reader counted the header as coverage;
    // this line computed the same hash two statements above, wrote it into the
    // file's trailer, and then did not print it -- so the newer of the two
    // formats, the one that feeds the deasmiigs code/data split, was the one a
    // reader could not check its read against. P4 says every artifact is
    // self-describing AND every reader checks; half a mechanism checks nothing.
    // At end of line so an existing prefix match on this string still matches.
    if(ok) {
        g_cov_last_format="A2COV2"; g_cov_last_hash=hash;
        fprintf(stderr,"A2GSPU COVERAGE: A2COV2 wrote %s maps=%zu run=%llu generation=%llu marked=%llu hash=%016llX\n",
            path,g_cov_maps.size(),(unsigned long long)g_cov_run_id,(unsigned long long)g_cov_generation,
            (unsigned long long)g_cov_marked,(unsigned long long)hash);
    }
    return ok;
}
