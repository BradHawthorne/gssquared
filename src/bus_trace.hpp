#pragma once
// ============================================================================
// bus_trace.hpp — deterministic SHR-write trace ("the bus-trace oracle")
//
// Records every write that lands in the Mega II bank-$E1 Super Hi-Res window
// ($E1:$2000-$9FFF, i.e. Mega II buffer index 0x12000-0x19FFF) as an ordered,
// cycle-stamped {cycle, addr17, data, rw} record. This is the renderer-free
// ground truth of "which display bytes changed, in what order" during a known
// boot. Two runs at the same frame count produce a byte-identical record
// stream, so the FNV-1a hash of that stream is a golden a snooping observer
// (a slot-bus write tap) must reproduce to prove it sees ALL SHR writes.
//
// Header-only (C++17 inline variables) so it adds zero build-graph surface.
// Naming is deliberately generic: this models a real Apple IIgs slot snoop, no
// board-specific terms.
// ============================================================================
#include <vector>
#include <cstdint>
#include "house_fnv.hpp"
#include <cstdio>

struct BusTraceRecord {
    uint64_t cycle;    // CPU cycle count at the write (NClock::get_cycles)
    uint32_t addr17;   // 17-bit Mega II buffer index; bit 16 = bank $E1
    uint8_t  data;     // byte written
    uint8_t  rw;       // 1 = write (reads reserved for a later pass)
    uint8_t  src;      // write provenance: 0 = direct bank-$E1 store, 1 = $C035-shadowed store.
                       // A single M2B0 bus bit cannot recover this distinction; the two MMU taps can.
    uint8_t  _pad;     // keep the on-disk record a stable 16 bytes (src lives in the old _pad)
};

// SHR display window in Mega II buffer-index space (bank $E1 $2000-$9FFF).
// Covers pixel data ($2000-$9CFF), SCBs ($9D00-$9DFF) and palettes ($9E00-$9FFF).
static constexpr uint32_t BUS_TRACE_SHR_LO = 0x12000;
static constexpr uint32_t BUS_TRACE_SHR_HI = 0x19FFF;

inline bool                        g_bus_trace_enabled = false;
inline std::vector<BusTraceRecord> g_bus_trace;

// Tap: call from every Mega II $E1 buffer-store site. Filters to the SHR window
// so the trace matches the spike's $E1 SHR oracle exactly.
inline void bus_trace_note(uint64_t cycle, uint32_t addr17, uint8_t data, uint8_t src = 0) {
    if (!g_bus_trace_enabled) return;
    if (addr17 < BUS_TRACE_SHR_LO || addr17 > BUS_TRACE_SHR_HI) return;
    g_bus_trace.push_back(BusTraceRecord{cycle, addr17, data, 1, src, 0});
}

inline void bus_trace_reset() { g_bus_trace.clear(); }

// Dump the trace to a flat binary file and return its FNV-1a 64 content hash.
// The hash covers (addr17, data, rw) of every record in order — the snoop
// invariant. Cycle is written to the file (for hardware-timing correlation)
// but excluded from the hash so it stays valid across clock-rate changes
// (e.g. a down-clock reclock writes the same bytes at different cycles).
inline uint64_t bus_trace_dump(const char *path, uint64_t *out_count) {
    FILE *f = fopen(path, "wb");
    if (f) {
        if (!g_bus_trace.empty())
            fwrite(g_bus_trace.data(), sizeof(BusTraceRecord), g_bus_trace.size(), f);
        fclose(f);
    }
    uint64_t hash = HOUSE_FNV_BASIS; // FNV-1a 64 offset basis
    for (const BusTraceRecord &r : g_bus_trace) {
        uint8_t b[5] = {
            (uint8_t)(r.addr17 & 0xFF),
            (uint8_t)((r.addr17 >> 8) & 0xFF),
            (uint8_t)((r.addr17 >> 16) & 0xFF),
            r.data,
            r.rw,
        };
        for (uint8_t x : b) hash = (hash ^ x) * HOUSE_FNV_PRIME;
    }
    if (out_count) *out_count = (uint64_t)g_bus_trace.size();
    return hash;
}

// Bracket the shadow-mirror slot-visibility question on the stand-in BEFORE any hardware exists.
// A real slot card observes one of two things for a $C035-shadowed SHR store:
//   (A) it appears as a distinct M2B0=1 (bank-$E1) cycle -> a naked M2B0 snoop sees ALL writes
//       -> its reconstructed $E1 == the full image -> the "all-writes" hash, miss-set {};
//   (B) it appears only as the originating $00/$01 cycle (M2B0=0) -> a naked M2B0 snoop sees ONLY
//       the direct bank-$E1 writes -> the "direct-only" hash, and it MISSES every shadowed write.
// The emulator knows each write's provenance (the two taps); the bus's single M2B0 bit cannot.
// So we emit BOTH brackets + the shadowed miss-set size: silicon must land on one of them, and a
// defensive snoop that reconstructs the shadowed writes is correct regardless of which.
inline void bus_trace_brackets(uint64_t *all_hash, uint64_t *direct_hash,
                               uint64_t *direct_count, uint64_t *shadowed_count) {
    uint64_t ha = HOUSE_FNV_BASIS, hd = HOUSE_FNV_BASIS, nd = 0, ns = 0;
    for (const BusTraceRecord &r : g_bus_trace) {
        uint8_t b[5] = { (uint8_t)(r.addr17 & 0xFF), (uint8_t)((r.addr17 >> 8) & 0xFF),
                         (uint8_t)((r.addr17 >> 16) & 0xFF), r.data, r.rw };
        for (uint8_t x : b) ha = (ha ^ x) * HOUSE_FNV_PRIME;        // bracket A: all writes
        if (r.src == 0) {                                            // bracket B: direct $E1 only
            for (uint8_t x : b) hd = (hd ^ x) * HOUSE_FNV_PRIME;
            nd++;
        } else ns++;
    }
    if (all_hash)       *all_hash = ha;
    if (direct_hash)    *direct_hash = hd;
    if (direct_count)   *direct_count = nd;
    if (shadowed_count) *shadowed_count = ns;
}
