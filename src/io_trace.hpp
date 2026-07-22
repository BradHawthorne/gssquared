#pragma once
// ============================================================================
// io_trace.hpp — general $C0xx soft-switch access ring (A2GSPU gap #6)
//
// Records every read and write that lands in the $C0xx I/O page as an ordered
// {seq, addr, data, rw} record. Answers "which soft switches did this code
// actually touch, in what order" directly, instead of inferring it from side
// effects (the live pain point: slot-6 was deduced from X=$60 rather than seen).
//
// Relationship to the other traces:
//   bus_trace.hpp  — SHR display writes only ($E1:$2000-$9FFF), cycle-stamped.
//   iolog (old)    — cumulative COUNTS for five keyboard switches, no ordering.
//   this           — every $C0xx touch, ordered, read AND write, all machines.
//
// Sequence, not cycle: the tap lives in MMU_II::read/write (mmu.hpp), which is
// machine-generic and has no clock reference — only MMU_IIgs exposes
// get_cycle_count(). Tapping there would cover the IIgs and silently miss the
// IIe. Ordering is what the gap asks for, and a harness can bracket a region
// with `step` to attribute records to code. Sequence also makes two runs
// trivially diffable.
//
// RING, not unbounded: a boot touches $C0xx constantly, so this keeps the most
// recent N records and counts total touches. Overflow is reported rather than
// silently dropping the oldest without notice.
//
// Header-only (C++17 inline variables) so it adds zero build-graph surface,
// matching bus_trace.hpp. Disabled by default: the hot-path cost is one
// predictable branch on a bool.
// ============================================================================
#include <vector>
#include <cstdint>
#include <cstdio>

struct IoTraceRecord {
    uint32_t seq;    // monotonic access index (ordering key)
    uint16_t addr;   // $C000-$C0FF (low 16 bits; the I/O page mirrors per bank)
    uint8_t  data;   // value read or written
    uint8_t  rw;     // 0 = read, 1 = write
};

inline bool                      g_io_trace_enabled = false;
inline std::vector<IoTraceRecord> g_io_trace;
inline uint32_t                  g_io_trace_seq   = 0;   // total touches seen
inline size_t                    g_io_trace_cap   = 16384;
inline uint32_t                  g_io_trace_dropped = 0;

inline void io_trace_reset() {
    g_io_trace.clear();
    g_io_trace_seq = 0;
    g_io_trace_dropped = 0;
}

// Tap. Callers pre-check g_io_trace_enabled so the disabled path is one branch.
inline void io_trace_note(uint32_t address, uint8_t data, uint8_t rw) {
    if ((address & 0xFF00) != 0xC000) return;   // $xx:C0xx only
    uint32_t s = g_io_trace_seq++;
    if (g_io_trace.size() >= g_io_trace_cap) {
        g_io_trace_dropped++;
        g_io_trace.erase(g_io_trace.begin());    // ring: keep the most recent
    }
    g_io_trace.push_back({s, (uint16_t)(address & 0xFFFF), data, rw});
}

// Text dump, newest-last: "seq rw addr data".
inline bool io_trace_dump(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    fprintf(f, "# io_trace: %u total touches, %zu retained, %u dropped\n",
            g_io_trace_seq, g_io_trace.size(), g_io_trace_dropped);
    fprintf(f, "# seq rw addr data\n");
    for (const auto &r : g_io_trace)
        fprintf(f, "%u %c %04X %02X\n", r.seq, r.rw ? 'W' : 'R',
                (unsigned)r.addr, (unsigned)r.data);
    fclose(f);
    return true;
}
