// a2record.hpp -- the record contract (Docs/INSTRUMENTATION_SHAPE.md, M1).
//
// Every rail that emits machine-readable output writes it through this one
// writer, so every record has one shape:
//
//   {"event":"<rail>.<kind>","frame":N,"cycles":N,<the rail's own fields>}
//
// one line, closed, newline-terminated, never printed in pieces. A rail's
// fields keep their names -- a reader keyed on them keeps working -- and gain
// the envelope in front of them.
//
// Sinks. A rail's own sink wins (A2GSPU_TRAP_OUT and a spec's out=,
// A2GSPU_LIVENESS_OUT, A2GSPU_WATCH_OUT, A2GSPU_CALLSTREAM, A2GSPU_TAP and its
// session files). A rail without one writes to A2GSPU_RECORDS=<file>, the
// global sink, when that is set. With neither, the rail prints the one stdout
// line it always printed, so a run that sets nothing is byte-identical.
//
// Rules, enforced by review and by tools/a2rail/railparity.ps1, which fails a
// JSON literal printed outside this file:
//   * a record is one whole line, written by Record::write;
//   * a human summary is one line, prefixed RAILNAME:;
//   * device chatter goes to stderr, never into a rail's stdout.
//
// `rail read A2GSPU_RECORDS` reports the sink, and per rail how many records
// reached THAT sink and how many went to the rail's own sink or to stdout.
#pragma once
#include "a2gspu_rails_registry.hpp"   // a2rails::Registrar: this rail registers its own reader (M8)
#include "a2gspu_artifact_io.hpp"
#include "a2gspu_outputs.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace a2record {
// Optional fan-out observer. Installed explicitly by an agent subscription;
// absent by default. Both file and stdout records take the same path.
inline void (*g_observer)(const char *,const std::string &) = nullptr;

inline FILE       *g_global = nullptr;      // A2GSPU_RECORDS
inline std::string g_global_path;
inline bool        g_armed = false;

// The envelope's clock. The frame is the headless/CTRL frame counter every
// rail already stamps; the cycle count comes from the machine's clock through
// a function main() registers at arm, so a rail that only has a cpu_state in
// hand (WATCH, CALLSTREAM) can still stamp cycles. Before registration: 0.
inline unsigned long long (*g_cycles_fn)() = nullptr;
inline unsigned long long cycles_now() { return g_cycles_fn ? g_cycles_fn() : 0ULL; }
inline int (*g_frame_fn)() = nullptr;
inline int frame_now() { return g_frame_fn ? g_frame_fn() : -1; }   // -1: no frame counter registered

// TWO NUMBERS, BECAUSE THERE ARE TWO DESTINATIONS. This was one counter,
// incremented at the end of both Record::write and Record::done with no
// reference to WHERE the line went, and format_status printed it immediately
// after `sink=<path>`. So a session with A2GSPU_RECORDS set plus per-rail
// sinks answered `sink=...\global.ndjson rails=2 watch=257 callstream=46`
// while global.ndjson was zero bytes long: every one of those records had gone
// to the rail's own file. And with no sink at all it answered `sink=none
// rails=1 trap=50`, a count beside the word none. A readback that says how
// many records a sink holds must not be counting records that never reached it.
struct Count { const char *rail; unsigned long long to_sink, elsewhere; };
inline Count g_counts[24] = {};
inline int   g_ncounts = 0;
inline void close();
inline void flush() {
    if(!g_global)return;
    errno=0;
    if(fflush(g_global)!=0 || ferror(g_global))throw a2artifact::Error("record-flush",g_global_path.c_str());
}

inline void count(const char *rail, bool to_sink) {
    for (int i = 0; i < g_ncounts; i++)
        if (!strcmp(g_counts[i].rail, rail)) {
            if (to_sink) g_counts[i].to_sink++; else g_counts[i].elsewhere++;
            return;
        }
    if (g_ncounts < (int)(sizeof g_counts / sizeof g_counts[0])) {
        g_counts[g_ncounts].rail = rail;
        g_counts[g_ncounts].to_sink  = to_sink ? 1 : 0;
        g_counts[g_ncounts].elsewhere = to_sink ? 0 : 1;
        g_ncounts++;
    }
}

// Open the global sink. Fatal if it cannot be opened: a sink that quietly
// falls back to stdout is a record nobody will find (M3).
inline void arm() {
    if (g_armed) return;
    g_armed = true;
    const char *p = getenv("A2GSPU_RECORDS");
    if (!p || !*p) return;
    g_global = fopen(p, "wb");
    if (!g_global) { fprintf(stderr, "A2GSPU_RECORDS: FAIL cannot open '%s' -- aborting (exit 2)\n", p); exit(2); }
    g_global_path = p;
    a2outputs::add("records",close);
    atexit([] {try {close();} catch(const a2artifact::Error &error){a2artifact::fatal_on_exit(error);} });
}

// The sink a rail writes to: its own if it has one, else the global one, else
// none -- and none means "print your stdout line instead".
inline FILE *sink(FILE *own) { return own ? own : g_global; }

inline void close() {
    a2artifact::finish(g_global,g_global_path.c_str());
}

inline void escape(std::string &s, const char *v) {
    for (const unsigned char *p = (const unsigned char *)v; *p; p++) {
        switch (*p) {
        case '"':  s += "\\\""; break;
        case '\\': s += "\\\\"; break;
        case '\n': s += "\\n";  break;
        case '\r': s += "\\r";  break;
        case '\t': s += "\\t";  break;
        default:
            if (*p < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04X", (unsigned)*p); s += b; }
            else s += (char)*p;
        }
    }
}

struct Record {
    std::string s;
    const char *rail;

    Record(const char *rail_, const char *kind, int frame, unsigned long long cycles) : rail(rail_) {
        s.reserve(256);
        s += "{\"event\":\""; s += rail; s += '.'; s += kind;
        char b[64]; snprintf(b, sizeof b, "\",\"frame\":%d,\"cycles\":%llu", frame, cycles);
        s += b;
    }
    Record &key(const char *k) { s += ",\""; s += k; s += "\":"; return *this; }
    Record &u(const char *k, unsigned long long v)  { char b[32]; snprintf(b, sizeof b, "%llu", v); key(k); s += b; return *this; }
    Record &i(const char *k, long long v)           { char b[32]; snprintf(b, sizeof b, "%lld", v); key(k); s += b; return *this; }
    Record &f0(const char *k, double v)             { char b[48]; snprintf(b, sizeof b, "%.0f", v); key(k); s += b; return *this; }
    Record &f2(const char *k, double v)             { char b[48]; snprintf(b, sizeof b, "%.2f", v); key(k); s += b; return *this; }
    Record &str(const char *k, const char *v)       { key(k); s += '"'; escape(s, v); s += '"'; return *this; }
    Record &hex16(const char *k, unsigned long long v) { char b[24]; snprintf(b, sizeof b, "\"%016llX\"", v); key(k); s += b; return *this; }
    Record &raw(const char *k, const char *json)    { key(k); s += json; return *this; }   // a value already in JSON
    Record &null(const char *k)                     { key(k); s += "null"; return *this; }
    Record &pc(const char *k, uint32_t fpc) {
        char b[16]; snprintf(b, sizeof b, "\"%02X:%04X\"", (unsigned)((fpc >> 16) & 0xFF), (unsigned)(fpc & 0xFFFF));
        key(k); s += b; return *this;
    }
    // ",\"k\":v" fragments a rail builds itself (the trap's payloads).
    Record &frag(const char *f)        { if (f) s += f; return *this; }
    Record &frag(const std::string &f) { s += f; return *this; }

    // The closed body, for a rail that prints it on stdout behind its prefix.
    // By definition it never reached the global sink, so it counts as "own".
    std::string done() { std::string r = s; r += '}'; count(rail, false); if(g_observer)g_observer(rail,r); return r; }

    // One whole line to the sink. flush for low-rate rails whose reader may be
    // tailing the file; not for streams.
    bool write_checked(FILE *f, bool flush = true) {
        s += "}\n";
        errno=0;
        if (!f || fputs(s.c_str(), f)==EOF || ferror(f)) return false;
        if (flush && (fflush(f)!=0 || ferror(f))) return false;
        // WHICH file, not just that a write happened: sink(own) returns the
        // rail's own FILE* when it has one, so f == g_global is the only
        // evidence that this line landed in the global sink.
        count(rail, f == g_global);
        if(g_observer)g_observer(rail,s);
        return true;
    }
    void write(FILE *f, bool flush = true) {
        if(!write_checked(f,flush))throw a2artifact::Error("record-write",rail);
    }
};

// The two numbers can never be read as one: <rail>=Nsink+Mown when a global
// sink exists, <rail>=Mown when it does not. A bare count beside sink= was
// read as the sink's, which was the whole defect.
inline void format_status(char *buf, size_t n) {
    try {flush();} catch(const a2artifact::Error &error){snprintf(buf,n,"status=FAIL records-incomplete %s",error.what());return;}
    int m = snprintf(buf, n, "status=OK sink=%s rails=%d",
                     g_global ? g_global_path.c_str() : "none", g_ncounts);
    for (int i = 0; i < g_ncounts && m > 0 && (size_t)m < n; i++) {
        if (g_global)
            m += snprintf(buf + m, n - m, " %s=%llusink+%lluown",
                          g_counts[i].rail, g_counts[i].to_sink, g_counts[i].elsewhere);
        else
            m += snprintf(buf + m, n - m, " %s=%lluown",
                          g_counts[i].rail, g_counts[i].elsewhere);
    }
}

} // namespace a2record

// The sink's readback, supplied with the rail (M8): `rail read A2GSPU_RECORDS`.
inline a2rails::Registrar g_reader_records{"A2GSPU_RECORDS", a2record::format_status};
