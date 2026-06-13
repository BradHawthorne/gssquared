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
#include <cstdio>

struct BusTraceRecord {
    uint64_t cycle;    // CPU cycle count at the write (NClock::get_cycles)
    uint32_t addr17;   // 17-bit Mega II buffer index; bit 16 = bank $E1
    uint8_t  data;     // byte written
    uint8_t  rw;       // 1 = write (reads reserved for a later pass)
    uint16_t _pad;     // keep the on-disk record a stable 16 bytes
};

// SHR display window in Mega II buffer-index space (bank $E1 $2000-$9FFF).
// Covers pixel data ($2000-$9CFF), SCBs ($9D00-$9DFF) and palettes ($9E00-$9FFF).
static constexpr uint32_t BUS_TRACE_SHR_LO = 0x12000;
static constexpr uint32_t BUS_TRACE_SHR_HI = 0x19FFF;

inline bool                        g_bus_trace_enabled = false;
inline std::vector<BusTraceRecord> g_bus_trace;

// Tap: call from every Mega II $E1 buffer-store site. Filters to the SHR window
// so the trace matches the spike's $E1 SHR oracle exactly.
inline void bus_trace_note(uint64_t cycle, uint32_t addr17, uint8_t data) {
    if (!g_bus_trace_enabled) return;
    if (addr17 < BUS_TRACE_SHR_LO || addr17 > BUS_TRACE_SHR_HI) return;
    g_bus_trace.push_back(BusTraceRecord{cycle, addr17, data, 1, 0});
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
    uint64_t hash = 1469598103934665603ULL; // FNV-1a 64 offset basis
    for (const BusTraceRecord &r : g_bus_trace) {
        uint8_t b[5] = {
            (uint8_t)(r.addr17 & 0xFF),
            (uint8_t)((r.addr17 >> 8) & 0xFF),
            (uint8_t)((r.addr17 >> 16) & 0xFF),
            r.data,
            r.rw,
        };
        for (uint8_t x : b) hash = (hash ^ x) * 1099511628211ULL;
    }
    if (out_count) *out_count = (uint64_t)g_bus_trace.size();
    return hash;
}
