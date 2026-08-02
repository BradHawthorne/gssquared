#pragma once
// ============================================================================
// generic_tap.hpp — A2GSPU_TAP: ONE title-AGNOSTIC PC-hit / session tap.
//
// This is the generalized mechanism distilled from the Wizardry-specific
// wiz5_tap.hpp + wiz5_session.hpp RE fork. Where those hard-coded the Sir-Tech
// UCSD p-code interpreter's PCs and zero-page pointers, this header takes the
// SAME two mechanisms (a repeating PC-hit call tap, and a periodic memory-
// capture session) and drives them entirely from environment variables, so a
// title-specific reverse-engineering run becomes a CONFIGURATION of the one
// oracle rather than a divergent code fork.
//
// All additive + env-gated. Nothing here runs unless an A2GSPU_TAP* var is set;
// with none set the emulator is byte-for-byte its stock self (each hook is one
// untaken branch). Kept in a standalone header so it stays trivially separable
// for upstream merges — it touches no existing type and adds no member.
//
// ---- (1) PC-hit call tap -----------------------------------------------------
//   A2GSPU_TAP=<file>            NDJSON sink; its presence arms the tap.
//   A2GSPU_TAP_PCS=<spec>        watch list, comma-separated tokens of
//                                  HEXPC[:LABEL][/N@HEXZP]
//                                where /N@HEXZP means: form a 16-bit pointer P
//                                from zero page (lo=[ZP], hi=[ZP+1]) and read N
//                                operand bytes at P+1..P+N (the p-code operand
//                                convention: the handler entry PC sees IPC in a
//                                ZP word, operands follow the opcode byte).
//   A2GSPU_TAP_PTRS=<spec>       extra 16-bit ZP-pointer fields emitted on every
//                                line, comma tokens NAME@HEXZP.
//   A2GSPU_TAP_MAX=<n>           cap events (0 = unlimited, default).
//   A2GSPU_TAP_CYC_PER_MS=<f>    ts_ms basis (default 1020.484 = 1.0205 MHz).
//
//   Wizardry-V config that reproduces the old wiz5_tap CXP/CLP/CGP/CSP/SEGLD tap:
//     A2GSPU_TAP=calls.ndjson
//     A2GSPU_TAP_PCS=D5A3:CXP/2@9E,D576:CLP/1@9E,D567:CGP/1@9E,D681:CSP/1@9E,\
//                    BEAA:SEGLD,BEB4:SEGLD,BEB9:SEGLD,BEBE:SEGLD,BED7:SEGLD
//     A2GSPU_TAP_PTRS=ipc@9E,mp@B2,mp2@BE,np@B4
//
// ---- (2) periodic capture session -------------------------------------------
//   A2GSPU_TAP_SESSION=<dir>     arms the session; writes into <dir>:
//     zp_stream.ndjson           {"ts_ms":N,"zp":"<512 hex>"} every ZP_EVERY s
//     ram_dump_periodic_tNNN.bin $0400-$BFFF every DUMP_EVERY s
//     hgr_dump_periodic_tNNN.bin $2000-$5FFF (+ .meta soft-switch sidecar)
//     text_pages.ndjson          40x24 text page snapshot each dump
//     ram_full_<tag>.bin         on-demand $0000-$BFFF (skips $C0xx I/O)
//   A2GSPU_TAP_DUMP_EVERY=<sec>  (60)   A2GSPU_TAP_ZP_EVERY=<sec> (5)
//   A2GSPU_TAP_DUMP_ON=<LABEL>:<min>    first time a tap hit with op LABEL has
//                                operand[0] >= min (per distinct value), request
//                                a tagged ram_full dump on the next frame — the
//                                generic form of wiz5's "CXP into a scenario
//                                segment >=16 => capture it resident, once".
//
// Ready-to-call, un-wired event hooks (call from keyboard/floppy if a title RE
// wants them — kept out of the default wiring to stay minimal + separable):
//   generic_tap_keypress(cpu, key)      -> keypress_events.ndjson
//   generic_tap_disk_write(qtrack)      -> disk_writes.ndjson
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include "cpu.hpp"

// ---- watch table ------------------------------------------------------------
struct GenericTapWatch {
    uint16_t pc;             // match low-16 of full_pc (bank-agnostic)
    char     label[16];
    int      nops;           // operand bytes to read (0 = none)
    uint8_t  zp;             // ZP addr holding the lo byte of the operand ptr
    uint8_t  dumpop_min;     // A2GSPU_TAP_DUMP_ON threshold for this label (0=off)
    bool     dumpop;         // this label participates in DUMP_ON
};
struct GenericTapPtr { char name[16]; uint8_t zp; };

struct GenericTap {
    bool     on = false;
    FILE    *out = nullptr;
    GenericTapWatch watch[32]; int nwatch = 0;
    GenericTapPtr   ptrs[16];  int nptr = 0;
    uint64_t count = 0, cap = 0;   // cap 0 = unlimited
    double   cyc_per_ms = 1020.484;

    // session
    bool     sess_on = false;
    std::string dir;
    FILE    *zp_f = nullptr, *pages_f = nullptr, *keys_f = nullptr, *disk_f = nullptr;
    double   cyc_per_sec = 1020484.0;
    int      dump_every_s = 60, zp_every_s = 5;
    int      next_dump_s = 60, next_zp_s = 5;
    uint64_t frame = 0;
    int      key_seq = 0;
    uint8_t  seg_dumped[256] = {0};      // DUMP_ON dedup, by operand value
    bool     full_dump_req = false;
    char     full_dump_tag[32] = {0};
    // disk-write dedup: one event per quarter-track per emulated second
    uint8_t  disk_seen[160] = {0};
    int      disk_seen_sec = -1;
};
inline GenericTap g_gtap;

// Optional title-config per-instruction extension hook. When non-null it runs
// on every instruction the tap gate (`if (g_gtap.on) generic_tap_check(...)` in
// base_6502.cpp) already visits — so a title-specific config (wiz5_config.hpp's
// EC19/DISKWR/CSP6 traps) can piggyback the EXISTING CPU-loop call site without
// touching any cpu/device file. Null = stock (nothing extra runs). Whoever sets
// it is responsible for forcing g_gtap.on so the gate is actually taken.
inline void (*g_gtap_ext)(cpu_state *cpu, uint64_t cycles) = nullptr;

inline double gtap_ts_ms(uint64_t cycles) { return cycles / g_gtap.cyc_per_ms; }

// ---- text page helpers (title-agnostic 40-col decode) -----------------------
inline void gtap_read_text_page(cpu_state *cpu, char out[24][41]) {
    for (int r = 0; r < 24; r++) {
        uint16_t base = 0x400 + (r % 8) * 0x80 + (r / 8) * 0x28;
        for (int c = 0; c < 40; c++) {
            uint8_t b = cpu->mmu->read(base + c);
            char ch;
            if (b < 0x20)                    ch = (char)('@' + b);
            else if (b <= 0x7E)              ch = (char)b;
            else if (b >= 0xA0 && b <= 0xFE) ch = (char)(b & 0x7F);
            else                             ch = '?';
            out[r][c] = ch;
        }
        out[r][40] = 0;
    }
}
inline void gtap_page_json(cpu_state *cpu, std::string &s) {
    char page[24][41]; gtap_read_text_page(cpu, page);
    s.clear();
    for (int r = 0; r < 24; r++) {
        if (r) s += "\\n";
        for (int c = 0; c < 40; c++) {
            char ch = page[r][c];
            if (ch == '"' || ch == '\\') s += '\\';
            s += ch;
        }
    }
}

inline void gtap_write_full_ram(cpu_state *cpu, const char *tag) {
    if (!g_gtap.sess_on) return;
    char p[600];
    snprintf(p, sizeof(p), "%s/ram_full_%s.bin", g_gtap.dir.c_str(), tag);
    if (FILE *f = fopen(p, "wb")) {
        for (uint32_t a = 0x0000; a < 0xC000; a++) {
            uint8_t v = (a >= 0xC000 && a <= 0xC0FF) ? 0 : cpu->mmu->read((uint16_t)a);
            fputc(v, f);
        }
        fclose(f);
        printf("A2GSPU_TAP: full RAM dump (48K) -> %s\n", p);
    }
}

// ---- (1) per-instruction PC-hit tap -----------------------------------------
// Called from the CPU execute loop guarded by g_generic_tap_on (one branch off).
inline void generic_tap_check(cpu_state *cpu, uint64_t cycles) {
    // Title-config extension (wiz5 EC19/DISKWR/CSP6 traps) rides this same gate.
    if (g_gtap_ext) g_gtap_ext(cpu, cycles);
    const uint16_t pc = (uint16_t)(cpu->full_pc & 0xFFFF);
    const GenericTapWatch *w = nullptr;
    for (int i = 0; i < g_gtap.nwatch; i++)
        if (g_gtap.watch[i].pc == pc) { w = &g_gtap.watch[i]; break; }
    if (!w) return;
    if (g_gtap.cap && g_gtap.count >= g_gtap.cap) return;
    if (!g_gtap.out) return;

    const double ts_ms = gtap_ts_ms(cycles);
    fprintf(g_gtap.out,
            "{\"ts_ms\":%.0f,\"ts_wall\":%lld,\"op\":\"%s\",\"pc\":%u,"
            "\"a\":%u,\"x\":%u,\"y\":%u,\"sp\":%u",
            ts_ms, (long long)time(nullptr), w->label, (unsigned)pc,
            (unsigned)(cpu->a & 0xFF), (unsigned)(cpu->x & 0xFF),
            (unsigned)(cpu->y & 0xFF), (unsigned)(cpu->sp & 0xFFFF));
    // extra named ZP-pointer fields (mp / mp2 / ipc / np ...)
    for (int i = 0; i < g_gtap.nptr; i++) {
        uint8_t z = g_gtap.ptrs[i].zp;
        uint16_t v = (uint16_t)(cpu->mmu->read(z) | (cpu->mmu->read((uint8_t)(z + 1)) << 8));
        fprintf(g_gtap.out, ",\"%s\":%u", g_gtap.ptrs[i].name, (unsigned)v);
    }
    // operand bytes following the p-code IPC pointer
    uint8_t op0 = 0;
    if (w->nops > 0) {
        uint16_t ipc = (uint16_t)(cpu->mmu->read(w->zp) |
                                  (cpu->mmu->read((uint8_t)(w->zp + 1)) << 8));
        fprintf(g_gtap.out, ",\"operands\":[");
        for (int k = 1; k <= w->nops; k++) {
            uint8_t b = cpu->mmu->read((uint16_t)(ipc + k));
            if (k == 1) op0 = b;
            fprintf(g_gtap.out, "%s%u", k > 1 ? "," : "", (unsigned)b);
        }
        fprintf(g_gtap.out, "]");
    }
    fprintf(g_gtap.out, "}\n");
    g_gtap.count++;

    // DUMP_ON: capture a resident full-RAM image the first time this label's
    // first operand crosses the threshold (generic segment-load capture).
    if (w->dumpop && w->nops > 0 && op0 >= w->dumpop_min &&
        g_gtap.sess_on && !g_gtap.seg_dumped[op0]) {
        g_gtap.seg_dumped[op0] = 1;
        g_gtap.full_dump_req = true;
        snprintf(g_gtap.full_dump_tag, sizeof(g_gtap.full_dump_tag),
                 "%s%u", w->label, (unsigned)op0);
    }
}

// ---- (2) per-frame session service ------------------------------------------
// Called once per emulated frame from run_one_frame (env-gated by the caller).
inline void generic_tap_session_frame(cpu_state *cpu, uint64_t cycles) {
    if (!g_gtap.sess_on) return;
    g_gtap.frame++;
    int sec = (int)(cycles / g_gtap.cyc_per_sec);

    if (g_gtap.full_dump_req) {
        g_gtap.full_dump_req = false;
        char tag[64]; snprintf(tag, sizeof(tag), "t%03d_%s", sec, g_gtap.full_dump_tag);
        gtap_write_full_ram(cpu, tag);
    }

    if (g_gtap.zp_f && sec >= g_gtap.next_zp_s) {
        g_gtap.next_zp_s = sec + g_gtap.zp_every_s;
        char hex[513];
        for (int i = 0; i < 256; i++) snprintf(hex + i * 2, 3, "%02x", cpu->mmu->read((uint16_t)i));
        fprintf(g_gtap.zp_f, "{\"ts_ms\":%.0f,\"zp\":\"%s\"}\n", gtap_ts_ms(cycles), hex);
        fflush(g_gtap.zp_f);
    }

    if (sec >= g_gtap.next_dump_s) {
        g_gtap.next_dump_s = sec + g_gtap.dump_every_s;
        char p[600];
        snprintf(p, sizeof(p), "%s/ram_dump_periodic_t%03d.bin", g_gtap.dir.c_str(), sec);
        if (FILE *f = fopen(p, "wb")) {
            for (uint32_t a = 0x0400; a <= 0xBFFF; a++) fputc(cpu->mmu->read(a), f);
            fclose(f);
        }
        snprintf(p, sizeof(p), "%s/hgr_dump_periodic_t%03d.bin", g_gtap.dir.c_str(), sec);
        if (FILE *f = fopen(p, "wb")) {
            for (uint32_t a = 0x2000; a <= 0x5FFF; a++) fputc(cpu->mmu->read(a), f);
            fclose(f);
        }
        snprintf(p, sizeof(p), "%s/hgr_dump_periodic_t%03d.bin.meta", g_gtap.dir.c_str(), sec);
        if (FILE *f = fopen(p, "wb")) {
            fprintf(f, "{\"emu_t\":%d,\"page2_active\":%d,\"hires_on\":%d,"
                       "\"text_on\":%d,\"mixed_on\":%d}\n", sec,
                    (cpu->mmu->read(0xC01C) & 0x80) ? 1 : 0,
                    (cpu->mmu->read(0xC01D) & 0x80) ? 1 : 0,
                    (cpu->mmu->read(0xC01A) & 0x80) ? 1 : 0,
                    (cpu->mmu->read(0xC01B) & 0x80) ? 1 : 0);
            fclose(f);
        }
        if (g_gtap.pages_f) {
            std::string page; gtap_page_json(cpu, page);
            fprintf(g_gtap.pages_f, "{\"ts_ms\":%.0f,\"text_page_ascii\":\"%s\"}\n",
                    gtap_ts_ms(cycles), page.c_str());
            fflush(g_gtap.pages_f);
        }
    }
}

// ---- ready-to-call, un-wired event hooks ------------------------------------
inline void generic_tap_keypress(cpu_state *cpu, uint8_t key) {
    if (!g_gtap.keys_f) return;
    g_gtap.key_seq++;
    std::string page; gtap_page_json(cpu, page);
    char chr = (key >= 32 && key < 127) ? (char)key : '.';
    fprintf(g_gtap.keys_f,
            "{\"seq\":%d,\"ascii\":%u,\"chr\":\"%c\",\"ts_wall\":%lld,"
            "\"text_page_pre\":\"%s\"}\n",
            g_gtap.key_seq, (unsigned)key, chr, (long long)time(nullptr), page.c_str());
    fflush(g_gtap.keys_f);
}
inline void generic_tap_disk_write(int qtrack) {
    if (!g_gtap.disk_f) return;
    int sec = (int)(g_gtap.frame / 60);
    if (sec != g_gtap.disk_seen_sec) { memset(g_gtap.disk_seen, 0, sizeof(g_gtap.disk_seen)); g_gtap.disk_seen_sec = sec; }
    if (qtrack < 0 || qtrack >= 160 || g_gtap.disk_seen[qtrack]) return;
    g_gtap.disk_seen[qtrack] = 1;
    fprintf(g_gtap.disk_f, "{\"op\":\"DISKWR\",\"qtrack\":%d,\"track\":%.2f}\n", qtrack, qtrack / 4.0);
    fflush(g_gtap.disk_f);
}

// ---- env parsing / arm / close ----------------------------------------------
inline void generic_tap_close() {
    if (g_gtap.out)     { printf("A2GSPU_TAP: %llu events captured\n", (unsigned long long)g_gtap.count); fclose(g_gtap.out);   g_gtap.out = nullptr; }
    for (FILE **f : { &g_gtap.zp_f, &g_gtap.pages_f, &g_gtap.keys_f, &g_gtap.disk_f })
        if (*f) { fclose(*f); *f = nullptr; }
    g_gtap.on = false; g_gtap.sess_on = false;
}

// Parse "HEXPC[:LABEL][/N@HEXZP]" tokens into the watch table.
inline void gtap_parse_pcs(const char *spec) {
    char buf[1024]; snprintf(buf, sizeof(buf), "%s", spec);
    for (char *tok = strtok(buf, ","); tok && g_gtap.nwatch < 32; tok = strtok(nullptr, ",")) {
        GenericTapWatch w{}; w.nops = 0; w.zp = 0;
        char *slash = strchr(tok, '/');
        if (slash) {                            // /N@HEXZP
            *slash = 0;
            int n = 0; unsigned zp = 0;
            if (sscanf(slash + 1, "%d@%x", &n, &zp) == 2) { w.nops = n; w.zp = (uint8_t)zp; }
        }
        char *colon = strchr(tok, ':');
        if (colon) { *colon = 0; snprintf(w.label, sizeof(w.label), "%s", colon + 1); }
        else       { snprintf(w.label, sizeof(w.label), "PC%s", tok); }
        w.pc = (uint16_t)strtoul(tok, nullptr, 16);
        g_gtap.watch[g_gtap.nwatch++] = w;
    }
}
inline void gtap_parse_ptrs(const char *spec) {
    char buf[512]; snprintf(buf, sizeof(buf), "%s", spec);
    for (char *tok = strtok(buf, ","); tok && g_gtap.nptr < 16; tok = strtok(nullptr, ",")) {
        char name[16]; unsigned zp = 0;
        if (sscanf(tok, "%15[^@]@%x", name, &zp) == 2) {
            GenericTapPtr p{}; snprintf(p.name, sizeof(p.name), "%s", name); p.zp = (uint8_t)zp;
            g_gtap.ptrs[g_gtap.nptr++] = p;
        }
    }
}

// Read every A2GSPU_TAP* var and open the sinks. No-op (leaves the emulator
// stock) when neither A2GSPU_TAP nor A2GSPU_TAP_SESSION is set. Idempotent-safe
// to call once from gs2.cpp; registers atexit(generic_tap_close).
inline bool g_gtap_armed = false;   // idempotency: safe to call from >1 driver path
inline void generic_tap_arm() {
    if (g_gtap_armed) return;
    g_gtap_armed = true;
    // WIZ5_* aliases (wiz5_clean project keeps its own env names for the SAME
    // generic tap): accept either name, A2GSPU_TAP* stays canonical. This lets
    // the one merged tool serve the wiz5/pascal projects with no script change.
    const char *tap  = getenv("A2GSPU_TAP");         if (!tap)  tap  = getenv("WIZ5_TAP");
    const char *sess = getenv("A2GSPU_TAP_SESSION"); if (!sess) sess = getenv("WIZ5_SESSION");
    if (!tap && !sess) return;

    if (const char *cpm = getenv("A2GSPU_TAP_CYC_PER_MS")) { double v = atof(cpm); if (v > 0) g_gtap.cyc_per_ms = v; }
    g_gtap.cyc_per_sec = g_gtap.cyc_per_ms * 1000.0;
    if (const char *cps = getenv("A2GSPU_TAP_CYC_PER_SEC")) { double v = atof(cps); if (v > 0) g_gtap.cyc_per_sec = v; }

    if (tap) {
        g_gtap.out = fopen(tap, "w");
        if (g_gtap.out) {
            g_gtap.on = true; g_gtap.count = 0;
            if (const char *pcs = getenv("A2GSPU_TAP_PCS")) gtap_parse_pcs(pcs);
            if (const char *pts = getenv("A2GSPU_TAP_PTRS")) gtap_parse_ptrs(pts);
            if (const char *mx = getenv("A2GSPU_TAP_MAX")) { long v = atol(mx); if (v > 0) g_gtap.cap = (uint64_t)v; }
            if (const char *don = getenv("A2GSPU_TAP_DUMP_ON")) {
                char lbl[16]; int mn = 0;
                if (sscanf(don, "%15[^:]:%d", lbl, &mn) >= 1)
                    for (int i = 0; i < g_gtap.nwatch; i++)
                        if (!strcmp(g_gtap.watch[i].label, lbl)) { g_gtap.watch[i].dumpop = true; g_gtap.watch[i].dumpop_min = (uint8_t)mn; }
            }
            printf("A2GSPU_TAP: armed -> %s (%d watch PCs, %d ptr fields)\n", tap, g_gtap.nwatch, g_gtap.nptr);
            if (g_gtap.nwatch == 0)
                printf("A2GSPU_TAP: WARNING no A2GSPU_TAP_PCS set — the tap will never fire\n");
        } else {
            printf("A2GSPU_TAP: could not open '%s' for write\n", tap);
        }
    }
    if (sess) {
        g_gtap.dir = sess; g_gtap.sess_on = true;
        const char *de = getenv("A2GSPU_TAP_DUMP_EVERY"); if (!de) de = getenv("WIZ5_DUMP_EVERY");
        const char *ze = getenv("A2GSPU_TAP_ZP_EVERY");   if (!ze) ze = getenv("WIZ5_ZP_EVERY");
        if (de) { int v = atoi(de); if (v > 0) g_gtap.dump_every_s = v; }
        if (ze) { int v = atoi(ze); if (v > 0) g_gtap.zp_every_s = v; }
        g_gtap.next_dump_s = g_gtap.dump_every_s; g_gtap.next_zp_s = g_gtap.zp_every_s;
        char p[600];
        snprintf(p, sizeof(p), "%s/zp_stream.ndjson", sess);    g_gtap.zp_f    = fopen(p, "w");
        snprintf(p, sizeof(p), "%s/text_pages.ndjson", sess);   g_gtap.pages_f = fopen(p, "w");
        snprintf(p, sizeof(p), "%s/keypress_events.ndjson", sess); g_gtap.keys_f = fopen(p, "w");
        snprintf(p, sizeof(p), "%s/disk_writes.ndjson", sess);  g_gtap.disk_f  = fopen(p, "w");
        printf("A2GSPU_TAP_SESSION: armed -> %s (dump=%ds zp=%ds)\n", sess, g_gtap.dump_every_s, g_gtap.zp_every_s);
    }
    atexit(generic_tap_close);
}
