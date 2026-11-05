#pragma once
// a2gspu_buslog.hpp -- the bus access log: one record per CPU cycle, with the
// bus side the cycle ran on and what the timing model charged for it.
//
// "Did this access go to the 1 MHz side, and what did it cost?" had no direct
// answer. The CPU cycle count is one per cycle whatever the cycle cost; the
// cost lives in the 14M (14.318 MHz master clock) ticks NClockIIgs charges per
// cycle -- 5 for a fast cycle at 2.8 MHz, 14 plus the wait for the next 1 MHz
// edge for a Mega II (slow-side) cycle, 5 more for a RAM refresh, 2 more for
// the long cycle at the end of each scan line -- and that cost was a local
// variable the clock threw away. This log keeps it, per cycle, next to the
// access that caused it.
//
// Format version 1 (kFormatVersion). A dump starts with
//     # buslog v1 fields: seq cycle t14m addr kind value side c14m base wait refresh stretch reasons
// and then has one line per CPU cycle, fields separated by one space:
//   seq      0-based record number since the mark
//   cycle    CPU cycle number of this cycle (NClock::get_cycles before it ends)
//   t14m     14M tick count when the cycle started (`-` without a timing model)
//   addr     24-bit address, 6 hex digits; `------` for an internal cycle
//   kind     read | write | fetch (program stream) | vector (vector pull) |
//            phantom-read | phantom-write (dummy accesses the core performs) |
//            skipped-read (a dummy read the core counts but does not perform) |
//            internal (a cycle with no access)
//   value    the byte read or written, 2 hex digits; `--` when there is none
//   side     fast | slow | `-` (the clock has no bus-side model: flat runner, II/IIe)
//   c14m     14M ticks this cycle cost = base + wait + refresh + stretch
//   base     5 (fast cycle at 2.8 MHz) or 14 (one 1 MHz cycle); 0 when the
//            free-run (ludicrous) clock folded the cycle into a shared 14M tick
//   wait     14M ticks spent waiting for the 1 MHz edge before a slow cycle
//   refresh  5 when the fast-RAM refresh landed on this cycle, else 0
//   stretch  14M ticks of the scan-line long cycle added at the end of this cycle
//   reasons  why the cycle cost what it did, comma-joined, or `-`:
//              megaii   a Mega II RAM/ROM cycle ($E0/$E1, or $C100-$CFFF via IOLC)
//              io       a $C0xx I/O cycle (any bank that reaches the Mega II's I/O)
//              shadow   a write the FPI also shadowed into the Mega II
//              slowmode the whole machine runs at 1 MHz ($C036 bit 7 clear, or a
//                       disk motor with its speed bit set, or the 1 MHz clock mode)
//              refresh  the fast-RAM refresh cycle landed here
//              stretch  the scan-line long cycle landed here
//              fastrom  the MMU's fast-without-refresh cycle type: ROM ($F0-$FF, the
//                       IOLC ROM) and the FPI registers it models that way (reads of
//                       $C02D and $C068; reads and writes of $C035-$C037)
// `megaii`, `io` and `shadow` name the MMU's reason for a 1 MHz cycle; they are
// read from the address and the access kind of a cycle the MMU asked to run
// slow, using the same decode the MMU uses (see note_cycle below).
//
// The summary (`buslog`, and img816's per-case footer) counts records, CPU
// cycles (always equal to records), the total 14M ticks, fast and slow cycles
// and their 14M ticks, and how many cycles carry each reason.
//
// Since protocol minor 5 (`features buslog`):
//   buslog mark [cap]   clear the log and record every CPU cycle from now on
//                       (default cap 1048576 records; later cycles are counted
//                       as dropped, not stored)
//   buslog              one-line summary (see format_report)
//   buslog dump <file>  the records above, in order
//   buslog off          stop recording; the log is kept for report/dump
//
// Off => one branch per bus access in the CPU core and one per cycle in the
// clock. Header-only so apps/img816 can use it without a running machine.

#include <cstdint>
#include <cstdio>
#include <vector>

namespace a2buslog {

constexpr int kFormatVersion = 1;

enum Kind : uint8_t {
    K_READ = 0, K_WRITE, K_FETCH, K_VECTOR, K_PHANTOM_READ, K_PHANTOM_WRITE, K_SKIPPED_READ, K_INTERNAL,
};
inline const char *kind_name(uint8_t k) {
    static const char *n[] = {"read", "write", "fetch", "vector", "phantom-read", "phantom-write", "skipped-read", "internal"};
    return k <= K_INTERNAL ? n[k] : "?";
}

enum Side : uint8_t { S_NONE = 0, S_FAST = 1, S_SLOW = 2 };

enum Reason : uint8_t {
    R_MEGAII = 1, R_IO = 2, R_SHADOW = 4, R_SLOWMODE = 8, R_REFRESH = 16, R_STRETCH = 32, R_FASTROM = 64,
};
constexpr int kReasons = 7;
inline const char *reason_name(int bit) {
    static const char *n[] = {"megaii", "io", "shadow", "slowmode", "refresh", "stretch", "fastrom"};
    return n[bit];
}

// Cycle types, as NClock.hpp's cycle_type_t (kept numeric here so this header
// needs no emulator headers).
constexpr uint8_t kTypeSync = 0, kTypeFastRom = 1;

struct Entry {
    uint64_t cycle;
    uint64_t t14m;
    uint32_t addr;
    uint8_t  value;
    uint8_t  kind;
    uint8_t  side;
    uint8_t  reasons;
    uint16_t c14m;
    uint8_t  base, wait, refresh, stretch;
};

inline bool               g_on      = false;
inline std::vector<Entry> g_log;
inline size_t             g_cap     = 1u << 20;
inline uint64_t           g_dropped = 0;

constexpr size_t kDefaultCap = 1u << 20;
constexpr size_t kMaxCap     = 1u << 26;

// The access the core made in the cycle now ending (set by the core's bus
// helpers, consumed by end_cycle).
inline bool     g_pend     = false;
inline uint32_t g_pend_addr = 0;
inline uint8_t  g_pend_val  = 0;
inline uint8_t  g_pend_kind = K_INTERNAL;

// What the clock charged for the cycle now ending (set by note_cycle).
struct ClockNote {
    bool     valid;
    uint8_t  requested;   // cycle type the MMU asked for (before the 1 MHz override)
    uint8_t  type;        // cycle type charged
    bool     slowmode;    // the machine-wide 1 MHz override was in force
    uint8_t  base, wait, refresh, stretch;
    uint64_t t14m;        // 14M count when the cycle started
};
inline ClockNote g_clock = {};

inline void pend(uint32_t addr, uint8_t value, uint8_t kind) {
    g_pend = true; g_pend_addr = addr & 0xFFFFFF; g_pend_val = value; g_pend_kind = kind;
}

// The MMU's reason for a 1 MHz cycle, from the address and kind. This is the
// MMU_IIgs decode: banks $E0/$E1 are Mega II cycles ($C0xx there is I/O); in
// any other bank only the IOLC window ($C000-$CFFF, I/O at $C0xx) and the
// shadowed write (the FPI's copy into the Mega II) run at 1 MHz.
inline uint8_t mmu_reason(uint32_t addr, uint8_t kind) {
    const uint32_t bank = (addr >> 16) & 0xFF;
    const uint32_t a16 = addr & 0xFFFF;
    if ((a16 & 0xFF00) == 0xC000) return R_IO;
    if (bank == 0xE0 || bank == 0xE1) return R_MEGAII;
    if ((a16 & 0xF000) == 0xC000) return R_MEGAII;
    if (kind == K_WRITE || kind == K_PHANTOM_WRITE) return R_SHADOW;
    return R_MEGAII;
}

// Writes the record for the cycle the clock just counted. A clock without a
// bus-side model (NClockII; img816's flat clock) calls this directly;
// NClockIIgs calls note_cycle below. The plain NClock writes no records. The record point is the clock, not the CPU core, so the
// log costs nothing per cycle in the core and covers every counted cycle.
inline void end_cycle(uint64_t cycle) {
    Entry e;
    e.cycle = cycle;
    e.kind = g_pend ? g_pend_kind : (uint8_t)K_INTERNAL;
    e.addr = g_pend ? g_pend_addr : 0;
    e.value = g_pend ? g_pend_val : 0;
    e.reasons = 0;
    if (g_clock.valid) {
        e.t14m = g_clock.t14m;
        e.base = g_clock.base; e.wait = g_clock.wait; e.refresh = g_clock.refresh; e.stretch = g_clock.stretch;
        e.c14m = (uint16_t)(e.base + e.wait + e.refresh + e.stretch);
        e.side = g_clock.type == kTypeSync ? S_SLOW : S_FAST;
        if (g_clock.requested == kTypeSync && g_pend && e.kind != K_SKIPPED_READ) e.reasons |= mmu_reason(e.addr, e.kind);
        if (g_clock.slowmode) e.reasons |= R_SLOWMODE;
        if (e.refresh) e.reasons |= R_REFRESH;
        if (e.stretch) e.reasons |= R_STRETCH;
        if (g_clock.type == kTypeFastRom) e.reasons |= R_FASTROM;
    } else {
        e.t14m = 0; e.base = e.wait = e.refresh = e.stretch = 0; e.c14m = 0;
        e.side = S_NONE;
    }
    g_pend = false;
    g_clock.valid = false;
    if (g_log.size() < g_cap) g_log.push_back(e);
    else g_dropped++;
}

// Called by NClockIIgs at the end of its per-cycle accounting.
inline void note_cycle(uint64_t cycle, uint8_t requested, uint8_t type, bool slowmode, uint32_t base,
                       uint32_t wait, uint32_t refresh, uint32_t stretch, uint64_t t14m) {
    g_clock.valid = true;
    g_clock.requested = requested; g_clock.type = type; g_clock.slowmode = slowmode;
    g_clock.base = (uint8_t)base; g_clock.wait = (uint8_t)wait;
    g_clock.refresh = (uint8_t)refresh; g_clock.stretch = (uint8_t)stretch;
    g_clock.t14m = t14m;
    end_cycle(cycle);
}

inline void mark(size_t cap = kDefaultCap) {
    g_log.clear();
    g_dropped = 0;
    g_cap = cap ? (cap > kMaxCap ? kMaxCap : cap) : kDefaultCap;
    g_log.reserve(g_cap < 65536 ? g_cap : 65536);
    g_pend = false;
    g_clock.valid = false;
    g_on = true;
}

inline void off() { g_on = false; g_pend = false; g_clock.valid = false; }

struct Summary {
    uint64_t n = 0, c14m = 0, fast = 0, slow = 0, fast_c14m = 0, slow_c14m = 0, timed = 0;
    uint64_t reason[kReasons] = {};
};

inline Summary summarize(size_t from = 0, size_t to = (size_t)-1) {
    Summary s;
    if (to > g_log.size()) to = g_log.size();
    for (size_t i = from; i < to; i++) {
        const Entry &e = g_log[i];
        s.n++;
        if (e.side != S_NONE) s.timed++;
        s.c14m += e.c14m;
        if (e.side == S_FAST) { s.fast++; s.fast_c14m += e.c14m; }
        if (e.side == S_SLOW) { s.slow++; s.slow_c14m += e.c14m; }
        for (int b = 0; b < kReasons; b++) if (e.reasons & (1u << b)) s.reason[b]++;
    }
    return s;
}

// "n=6 cycles=6 c14m=56 fast=4 fast_c14m=20 slow=2 slow_c14m=36 megaii=2 io=0 shadow=0 slowmode=0 refresh=0 stretch=0 fastrom=0"
// (c14m and the side counts are `-` when no record carries a timing model.)
inline int format_summary(char *buf, size_t bufsz, const Summary &s) {
    int w;
    if (s.timed)
        w = std::snprintf(buf, bufsz, "n=%llu cycles=%llu c14m=%llu fast=%llu fast_c14m=%llu slow=%llu slow_c14m=%llu",
                          (unsigned long long)s.n, (unsigned long long)s.n, (unsigned long long)s.c14m,
                          (unsigned long long)s.fast, (unsigned long long)s.fast_c14m,
                          (unsigned long long)s.slow, (unsigned long long)s.slow_c14m);
    else
        w = std::snprintf(buf, bufsz, "n=%llu cycles=%llu c14m=- fast=- fast_c14m=- slow=- slow_c14m=-",
                          (unsigned long long)s.n, (unsigned long long)s.n);
    if (w < 0 || (size_t)w >= bufsz) return w;
    size_t used = (size_t)w;
    for (int b = 0; b < kReasons; b++) {
        w = std::snprintf(buf + used, bufsz - used, " %s=%llu", reason_name(b), (unsigned long long)s.reason[b]);
        if (w < 0 || (size_t)w >= bufsz - used) return (int)used;
        used += (size_t)w;
    }
    return (int)used;
}

// "status=OK buslog v=1 on=1 dropped=0 n=... (format_summary)"
inline void format_report(char *buf, size_t bufsz) {
    if (!bufsz) return;
    int w = std::snprintf(buf, bufsz, "status=OK buslog v=%d on=%d dropped=%llu ", kFormatVersion, g_on ? 1 : 0,
                          (unsigned long long)g_dropped);
    if (w < 0 || (size_t)w >= bufsz) return;
    format_summary(buf + w, bufsz - (size_t)w, summarize());
}

inline void header(FILE *f) {
    std::fprintf(f, "# buslog v%d fields: seq cycle t14m addr kind value side c14m base wait refresh stretch reasons\n",
                 kFormatVersion);
}

inline void format_entry(char *buf, size_t bufsz, size_t seq, const Entry &e) {
    char addr[8], val[4], reasons[64];
    if (e.kind == K_INTERNAL) std::snprintf(addr, sizeof addr, "------");
    else std::snprintf(addr, sizeof addr, "%06X", (unsigned)e.addr);
    if (e.kind == K_INTERNAL || e.kind == K_SKIPPED_READ) std::snprintf(val, sizeof val, "--");
    else std::snprintf(val, sizeof val, "%02X", (unsigned)e.value);
    size_t used = 0;
    reasons[0] = 0;
    for (int b = 0; b < kReasons; b++) {
        if (!(e.reasons & (1u << b))) continue;
        int w = std::snprintf(reasons + used, sizeof reasons - used, "%s%s", used ? "," : "", reason_name(b));
        if (w > 0) used += (size_t)w;
    }
    if (!used) std::snprintf(reasons, sizeof reasons, "-");
    if (e.side == S_NONE)
        std::snprintf(buf, bufsz, "%zu %llu - %s %s %s - - - - - - %s", seq, (unsigned long long)e.cycle, addr,
                      kind_name(e.kind), val, reasons);
    else
        std::snprintf(buf, bufsz, "%zu %llu %llu %s %s %s %s %u %u %u %u %u %s", seq, (unsigned long long)e.cycle,
                      (unsigned long long)e.t14m, addr, kind_name(e.kind), val, e.side == S_FAST ? "fast" : "slow",
                      (unsigned)e.c14m, (unsigned)e.base, (unsigned)e.wait, (unsigned)e.refresh, (unsigned)e.stretch,
                      reasons);
}

// Records [from, to) with seq numbered from 0 at `from`.
inline void dump_range(FILE *f, size_t from, size_t to) {
    char line[160];
    if (to > g_log.size()) to = g_log.size();
    for (size_t i = from; i < to; i++) {
        format_entry(line, sizeof line, i - from, g_log[i]);
        std::fprintf(f, "%s\n", line);
    }
}

inline void dump(FILE *f) {
    header(f);
    dump_range(f, 0, g_log.size());
}

} // namespace a2buslog
