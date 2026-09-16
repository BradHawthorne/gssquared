#pragma once
/*
 *   A2GSPU card model -- the host's command register.
 *
 *   The card watches the slot bus; it never drives the data lines. So a read
 *   of any of its addresses floats, like an empty slot's, and the host detects
 *   the card from its own configuration, not from an ident byte. The host
 *   talks to it through ONE write-only register, $C0nF ($C0BF in slot 3), a
 *   byte per write:
 *
 *       [SYNC $A2] [seq u16 LE] [op u8] [len u8] [payload x len]
 *
 *   assembled by these rules:
 *     - a byte that is not SYNC while waiting for one is skipped, and counted;
 *     - a len above the payload cap (32) drops the frame, counted, and the
 *       assembler resyncs at the header -- a len byte of $A2 IS a new SYNC;
 *     - len 0 completes the frame at once; a partial frame waits for its bytes;
 *     - seq is 16-bit and wraps: a forward jump of d adds d-1 to the gap count,
 *       and zero or backward counts as a duplicate;
 *     - at most 16 commands are forwarded per video frame; the rest count as
 *       overflow.
 *
 *   Op $00 is NOP and op $01 is HELLO (payload: the host's protocol version,
 *   u16). Every other op is recorded by number with its payload: the emulator
 *   gives it no meaning, and a record says exactly what the host sent.
 *
 *   Nothing here changes the machine. The records (A2GSPU_CARD_CMDLOG) and
 *   the counters (`devstate A2GSPU`, `rail read A2GSPU_CARD_CMDLOG`) are the
 *   whole of what the model does.
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "a2record.hpp"

inline FILE       *g_a2gspu_cmdlog_out = nullptr;   // A2GSPU_CARD_CMDLOG=<file>
inline bool        g_a2gspu_cmdlog_on = false;      // A2GSPU_CARD_CMDLOG set (a file, or 1)
inline std::string g_a2gspu_cmdlog_path;

struct A2gspuCmdReg {
    static constexpr uint8_t SYNC = 0xA2;
    int payload_cap = 32;
    int max_per_frame = 16;
    uint8_t reg_lo = 0xBF;                          // the register's low address byte ($C0nF)

    enum St : uint8_t { WAIT_SYNC, SEQ_LO, SEQ_HI, OP, LEN, PAYLOAD };
    St st = WAIT_SYNC;
    uint16_t seq = 0;
    uint8_t op = 0, len = 0, got = 0;
    uint8_t payload[256] = {};
    uint64_t skip_run = 0;                           // bytes skipped since the last SYNC

    bool have_last = false;
    uint16_t last_seq = 0;
    int frame_cmds = 0;                              // commands completed in this video frame

    uint64_t writes = 0, frames = 0, presync = 0, oversize = 0, gaps = 0, dups = 0, overflow = 0;
    uint64_t nops = 0, hellos = 0, other_ops = 0;
    bool hello_seen = false;
    uint16_t hello_version = 0;
    bool last_valid = false;
    uint8_t last_op = 0, last_len = 0;
    uint16_t last_frame_seq = 0;
};

inline void a2gspu_cmdreg_record_drop(const A2gspuCmdReg &r, const char *reason, uint64_t bytes, int len_byte) {
    if (!g_a2gspu_cmdlog_on) return;
    a2record::Record rec("card", "drop", a2record::frame_now(), a2record::cycles_now());
    rec.str("reason", reason).u("bytes", bytes);
    if (len_byte >= 0) rec.u("len", (unsigned)len_byte).u("seq", r.seq).u("op", r.op);
    if (FILE *sink = a2record::sink(g_a2gspu_cmdlog_out)) rec.write(sink, true);
    else printf("A2GSPU CARD: %s\n", rec.done().c_str());
}

inline void a2gspu_cmdreg_complete(A2gspuCmdReg &r) {
    r.frames++;
    bool dup = false;
    unsigned gap = 0;
    if (r.have_last) {
        const uint16_t d = (uint16_t)(r.seq - r.last_seq);
        if (d == 0 || d >= 0x8000) { dup = true; r.dups++; }
        else { gap = d - 1u; r.gaps += gap; r.last_seq = r.seq; }
    } else {
        r.have_last = true; r.last_seq = r.seq;
    }
    const bool over = (++r.frame_cmds > r.max_per_frame);
    if (over) r.overflow++;
    if (r.op == 0x00) r.nops++;
    else if (r.op == 0x01) {
        r.hellos++;
        if (r.len >= 2) { r.hello_seen = true; r.hello_version = (uint16_t)(r.payload[0] | (r.payload[1] << 8)); }
    } else r.other_ops++;
    r.last_valid = true; r.last_op = r.op; r.last_len = r.len; r.last_frame_seq = r.seq;

    if (g_a2gspu_cmdlog_on) {
        char hex[2 * 255 + 1];
        for (unsigned i = 0; i < r.len; i++) snprintf(hex + 2 * i, 3, "%02X", r.payload[i]);
        hex[2 * r.len] = 0;
        a2record::Record rec("card", "cmd", a2record::frame_now(), a2record::cycles_now());
        rec.u("seq", r.seq).u("op", r.op).u("len", r.len).str("payload", hex)
           .str("name", r.op == 0x00 ? "NOP" : r.op == 0x01 ? "HELLO" : "")
           .u("gap", gap).u("dup", dup ? 1 : 0).u("overflow", over ? 1 : 0);
        if (FILE *sink = a2record::sink(g_a2gspu_cmdlog_out)) rec.write(sink, true);
        else printf("A2GSPU CARD: %s\n", rec.done().c_str());
    }
    r.st = A2gspuCmdReg::WAIT_SYNC;
}

// One host write to $C0nF.
inline void a2gspu_cmdreg_write(A2gspuCmdReg &r, uint8_t b) {
    r.writes++;
    switch (r.st) {
    case A2gspuCmdReg::WAIT_SYNC:
        if (b == A2gspuCmdReg::SYNC) {
            if (r.skip_run) { a2gspu_cmdreg_record_drop(r, "presync", r.skip_run, -1); r.skip_run = 0; }
            r.st = A2gspuCmdReg::SEQ_LO;
        } else { r.presync++; r.skip_run++; }
        break;
    case A2gspuCmdReg::SEQ_LO:  r.seq = b; r.st = A2gspuCmdReg::SEQ_HI; break;
    case A2gspuCmdReg::SEQ_HI:  r.seq = (uint16_t)(r.seq | (b << 8)); r.st = A2gspuCmdReg::OP; break;
    case A2gspuCmdReg::OP:      r.op = b; r.st = A2gspuCmdReg::LEN; break;
    case A2gspuCmdReg::LEN:
        if ((int)b > r.payload_cap) {
            r.oversize++;
            a2gspu_cmdreg_record_drop(r, "oversize", 5, b);
            r.st = (b == A2gspuCmdReg::SYNC) ? A2gspuCmdReg::SEQ_LO : A2gspuCmdReg::WAIT_SYNC;
            break;
        }
        r.len = b; r.got = 0;
        if (b == 0) a2gspu_cmdreg_complete(r);
        else r.st = A2gspuCmdReg::PAYLOAD;
        break;
    case A2gspuCmdReg::PAYLOAD:
        r.payload[r.got++] = b;
        if (r.got == r.len) a2gspu_cmdreg_complete(r);
        break;
    }
}

// Once per video frame: the forwarding budget starts again.
inline void a2gspu_cmdreg_frame(A2gspuCmdReg &r) { r.frame_cmds = 0; }

inline const char *a2gspu_cmdreg_state_name(const A2gspuCmdReg &r) {
    switch (r.st) {
    case A2gspuCmdReg::WAIT_SYNC: return "idle";
    case A2gspuCmdReg::SEQ_LO: case A2gspuCmdReg::SEQ_HI: case A2gspuCmdReg::OP: case A2gspuCmdReg::LEN: return "header";
    default: return "payload";
    }
}

// The machine's card, for readbacks outside the device (null when there is none).
inline A2gspuCmdReg *g_a2gspu_cmdreg = nullptr;

inline void a2gspu_cmdreg_format(char *buf, size_t n) {
    const A2gspuCmdReg *r = g_a2gspu_cmdreg;
    const char *sink = g_a2gspu_cmdlog_out ? g_a2gspu_cmdlog_path.c_str() : (g_a2gspu_cmdlog_on ? (a2record::g_global ? "records" : "stdout") : "none");
    if (!r) { snprintf(buf, n, "status=OK A2GSPU_CARD_CMDLOG armed=%d sink=%s card=absent -- no A2GSPU card in the card model in this machine", g_a2gspu_cmdlog_on ? 1 : 0, sink); return; }
    snprintf(buf, n, "status=OK A2GSPU_CARD_CMDLOG armed=%d sink=%s register=$C0%02X assembler=%s writes=%llu frames=%llu "
                     "presync_skipped=%llu oversize=%llu gaps=%llu duplicates=%llu overflow=%llu nop=%llu hello=%llu other=%llu "
                     "hello_version=%s last=%s",
             g_a2gspu_cmdlog_on ? 1 : 0, sink, r->reg_lo, a2gspu_cmdreg_state_name(*r),
             (unsigned long long)r->writes, (unsigned long long)r->frames, (unsigned long long)r->presync,
             (unsigned long long)r->oversize, (unsigned long long)r->gaps, (unsigned long long)r->dups,
             (unsigned long long)r->overflow, (unsigned long long)r->nops, (unsigned long long)r->hellos,
             (unsigned long long)r->other_ops,
             r->hello_seen ? std::to_string(r->hello_version).c_str() : "none",
             r->last_valid ? (std::string("op$") + [&]{ char b[16]; snprintf(b, sizeof b, "%02X/len%u/seq%u", r->last_op, r->last_len, r->last_frame_seq); return std::string(b); }()).c_str() : "none");
}
