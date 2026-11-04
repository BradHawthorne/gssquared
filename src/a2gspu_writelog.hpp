#pragma once
// a2gspu_writelog.hpp -- the CPU write log behind the CTRL `writes` verb.
//
// "Which addresses did this routine write?" had no direct answer on the rail.
// `watch bus` reports hits inside up to eight ranges, to stdout or an NDJSON
// sink, and the 65816's phantom (dummy) writes -- the extra write of the old
// value that a read-modify-write instruction makes in emulation mode -- went
// past it entirely: the CPU performed them (a soft switch toggled), the watch
// never saw them. `context mark`/`changes` answers with an observation id, not
// a memory write set.
//
// Since protocol minor 4 (`features writes`):
//   writes mark [cap]   clear the log and record every CPU write from now on
//                       (default cap 65536 entries; writes beyond it are
//                       counted as dropped, not stored)
//   writes              one line: on=0|1 n=<writes> phantom=<of which phantom>
//                       unique=<addresses> dropped=<n> wr=<sorted unique
//                       24-bit addresses, AAAAAA,...>
//   writes dump <file>  every recorded write in order, one per line:
//                       <seq> <AAAAAA> <data> write|phantom-write
//   writes off          stop recording; the log is kept for `writes`/`dump`
//
// Every write the CPU core puts on the bus is recorded: ordinary writes
// (bus_write) and phantom writes (phantom_write and, when it performs the
// access, phantom_write_ign). Writes made by the rail itself (`poke`, `load`)
// do not go through the CPU and are not recorded. When the list does not fit
// in the reply, the reply says `wr=truncated` and `writes dump` has it all.
//
// Off => one branch per write in the core. Header-only so apps/writelogtest
// can check it against the CPU core without a running machine.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace a2writelog {

struct Entry { uint32_t addr; uint8_t data; uint8_t phantom; };

inline bool               g_on      = false;
inline std::vector<Entry> g_log;
inline size_t             g_cap     = 65536;
inline uint64_t           g_dropped = 0;

constexpr size_t kDefaultCap = 65536;
constexpr size_t kMaxCap     = 1u << 24;

inline void note(uint32_t addr, uint8_t data, bool phantom) {
    if (g_log.size() < g_cap) g_log.push_back({addr & 0xFFFFFF, data, (uint8_t)(phantom ? 1 : 0)});
    else g_dropped++;
}

inline void mark(size_t cap = kDefaultCap) {
    g_log.clear();
    g_dropped = 0;
    g_cap = cap ? (cap > kMaxCap ? kMaxCap : cap) : kDefaultCap;
    g_log.reserve(g_cap < 4096 ? g_cap : 4096);
    g_on = true;
}

inline void off() { g_on = false; }

inline std::vector<uint32_t> unique_addresses() {
    std::vector<uint32_t> ws;
    ws.reserve(g_log.size());
    for (const Entry &e : g_log) ws.push_back(e.addr);
    std::sort(ws.begin(), ws.end());
    ws.erase(std::unique(ws.begin(), ws.end()), ws.end());
    return ws;
}

// "status=OK writes on=1 n=3 phantom=1 unique=2 dropped=0 wr=007000,0001FF"
inline void format_report(char *buf, size_t bufsz) {
    if (!bufsz) return;
    size_t phantom = 0;
    for (const Entry &e : g_log) phantom += e.phantom;
    std::vector<uint32_t> ws = unique_addresses();
    int w = std::snprintf(buf, bufsz, "status=OK writes on=%d n=%zu phantom=%zu unique=%zu dropped=%llu wr=",
                          g_on ? 1 : 0, g_log.size(), phantom, ws.size(), (unsigned long long)g_dropped);
    if (w < 0 || (size_t)w >= bufsz) return;
    size_t used = (size_t)w;
    // 7 bytes per address ("AAAAAA,"); refuse to cut the list in the middle.
    const char *trunc = "truncated (writes dump <file> has every write)";
    if (ws.size() * 7 + 1 > bufsz - used) {
        std::snprintf(buf + used, bufsz - used, "%s", trunc);
        return;
    }
    for (size_t i = 0; i < ws.size(); i++) {
        w = std::snprintf(buf + used, bufsz - used, i ? ",%06X" : "%06X", (unsigned)ws[i]);
        if (w < 0) return;
        used += (size_t)w;
    }
}

inline void dump(FILE *f) {
    for (size_t i = 0; i < g_log.size(); i++)
        std::fprintf(f, "%zu %06X %02X %s\n", i, (unsigned)g_log[i].addr, (unsigned)g_log[i].data,
                     g_log[i].phantom ? "phantom-write" : "write");
}

} // namespace a2writelog
