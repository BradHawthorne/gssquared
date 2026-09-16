#pragma once
/*
 * u2_rail.hpp -- the Uthernet II's instrumentation surface (fork-owned).
 *
 * A2GSPU_U2_EVENTS=<file>|1 writes one record per guest-visible event on the card:
 *   u2.cmd    a socket command the guest wrote to Sn_CR, with the status before and
 *             after, the interrupt bits it raised, and what happened
 *             (done | ignored-state | refused | not-emulated);
 *   u2.state  a status change the host network caused (connected, remote close,
 *             reset, failed), stamped at the frame boundary where it entered the
 *             machine -- the only place host network timing crosses into emulated time;
 *   u2.drop   data the chip had no room for (a datagram or frame larger than the free
 *             receive buffer), or a stale host event for a socket the guest had closed.
 * Sink: the file, else A2GSPU_RECORDS, else stdout prefixed "A2GSPU U2:".
 * `rail read A2GSPU_U2_EVENTS` and `devstate uthernet2` give the counters. Nothing here
 * changes the machine.
 */
#include <cstdio>
#include <cstdint>
#include <string>

#include "a2record.hpp"

inline FILE       *g_u2_events_out = nullptr;
inline bool        g_u2_events_on  = false;
inline std::string g_u2_events_path;

struct U2Counters {
    uint64_t cmds = 0, cmds_ignored = 0, cmds_refused = 0, cmds_not_emulated = 0;
    uint64_t state_changes = 0, stale_events = 0;
    uint64_t rx_dropped = 0, rx_dropped_bytes = 0;
    uint64_t send_ok = 0, send_timeout = 0;
};
inline U2Counters g_u2_counts;

inline void u2_emit(a2record::Record &rec) {
    if (FILE *sink = a2record::sink(g_u2_events_out)) rec.write(sink, true);
    else printf("A2GSPU U2: %s\n", rec.done().c_str());
}

inline const char *u2_status_name(uint8_t sr) {
    switch (sr) {
    case 0x00: return "CLOSED";
    case 0x13: return "INIT";
    case 0x14: return "LISTEN";
    case 0x15: return "SYNSENT";
    case 0x17: return "ESTABLISHED";
    case 0x1C: return "CLOSE_WAIT";
    case 0x22: return "UDP";
    case 0x32: return "IPRAW";
    case 0x42: return "MACRAW";
    default:   return "?";
    }
}

inline const char *u2_cmd_name(uint8_t cmd) {
    switch (cmd) {
    case 0x01: return "OPEN";
    case 0x02: return "LISTEN";
    case 0x04: return "CONNECT";
    case 0x08: return "DISCON";
    case 0x10: return "CLOSE";
    case 0x20: return "SEND";
    case 0x21: return "SEND_MAC";
    case 0x22: return "SEND_KEEP";
    case 0x40: return "RECV";
    default:   return "UNKNOWN";
    }
}

inline void u2_record_cmd(int slot, int sock, uint8_t cmd, uint8_t sr_before, uint8_t sr_after,
                          uint8_t ir_raised, const char *result) {
    g_u2_counts.cmds++;
    if (!g_u2_events_on) return;
    a2record::Record rec("u2", "cmd", a2record::frame_now(), a2record::cycles_now());
    rec.u("slot", (unsigned)slot).u("sock", (unsigned)sock).u("cmd", cmd).str("name", u2_cmd_name(cmd))
       .str("from", u2_status_name(sr_before)).str("to", u2_status_name(sr_after))
       .u("ir", ir_raised).str("result", result);
    u2_emit(rec);
}

inline void u2_record_state(int slot, int sock, uint8_t from, uint8_t to, const char *cause,
                            uint8_t ir_raised, int host_err) {
    g_u2_counts.state_changes++;
    if (!g_u2_events_on) return;
    a2record::Record rec("u2", "state", a2record::frame_now(), a2record::cycles_now());
    rec.u("slot", (unsigned)slot).u("sock", (unsigned)sock).str("from", u2_status_name(from))
       .str("to", u2_status_name(to)).str("cause", cause).u("ir", ir_raised)
       .u("host_err", (unsigned)(host_err < 0 ? 0 : host_err));
    u2_emit(rec);
}

inline void u2_record_drop(int slot, int sock, const char *kind, uint64_t bytes) {
    if (!g_u2_events_on) return;
    a2record::Record rec("u2", "drop", a2record::frame_now(), a2record::cycles_now());
    rec.u("slot", (unsigned)slot).u("sock", (unsigned)sock).str("kind", kind).u("bytes", bytes);
    u2_emit(rec);
}

inline void u2_format_counters(char *buf, size_t n) {
    const U2Counters &c = g_u2_counts;
    snprintf(buf, n,
             "status=OK A2GSPU_U2_EVENTS armed=%d sink=%s cmds=%llu ignored=%llu refused=%llu "
             "not_emulated=%llu state_changes=%llu stale_events=%llu rx_dropped=%llu "
             "rx_dropped_bytes=%llu send_ok=%llu send_timeout=%llu",
             g_u2_events_on ? 1 : 0,
             g_u2_events_out ? g_u2_events_path.c_str() : (a2record::g_global ? "A2GSPU_RECORDS" : "stdout"),
             (unsigned long long)c.cmds, (unsigned long long)c.cmds_ignored, (unsigned long long)c.cmds_refused,
             (unsigned long long)c.cmds_not_emulated, (unsigned long long)c.state_changes,
             (unsigned long long)c.stale_events, (unsigned long long)c.rx_dropped,
             (unsigned long long)c.rx_dropped_bytes, (unsigned long long)c.send_ok,
             (unsigned long long)c.send_timeout);
}
