#pragma once
#include "a2record.hpp"   // the record contract: one writer, one shape, one global sink
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
//     zp_stream.ndjson           {"event":"tap.zp",...,"ts_ms":N,"zp":"<512 hex>"} every ZP_EVERY s
//     ram_dump_periodic_tNNN.bin $0400-$BFFF every DUMP_EVERY s
//     hgr_dump_periodic_tNNN.bin $2000-$5FFF (+ .meta soft-switch sidecar)
//     text_pages.ndjson          40x24 text page snapshot each dump
//     ram_full_<tag>.bin         on-demand full RAM: a IIe/IIc's 128K RAM array as the
//                                MMU stores it (main, then aux at +$10000; the language
//                                card's bank-1 $D000 page at $C000); elsewhere $0000-$BFFF
//                                as the CPU sees it. Each with a .meta.json sidecar (M6)
//     ram_full_periodic_tNNN.bin the same, every DUMP_EVERY s
//     keypress_events.ndjson     tap.key on each key that reaches the keyboard latch (host,
//                                paste buffer, KEYSCHED, CTRL keys; II family and IIgs ADB),
//                                with ts_ms and the text page before it; tap.keypost with the
//                                text page KEY_POST_FRAMES later
//     keypress_NNNN_{pre,post}_hgr.bin   $2000-$5FFF at the key and KEY_POST_FRAMES later --
//                                the fork's per-key picture: memory, not a host rendering
//     disk_writes.ndjson         tap.diskwr per quarter-track per emulated second a Disk II
//                                lays bits on a mounted 5.25 disk
//   A2GSPU_TAP_DUMP_EVERY=<sec>  (60)   A2GSPU_TAP_ZP_EVERY=<sec> (5)
//   A2GSPU_TAP_KEY_POST_FRAMES=<n> (30) frames between a key and its post capture
//   A2GSPU_TAP_DUMP_ON=<LABEL>:<min>    first time a tap hit with op LABEL has
//                                operand[0] >= min (per distinct value), request
//                                a tagged ram_full dump on the next frame — the
//                                generic form of wiz5's "CXP into a scenario
//                                segment >=16 => capture it resident, once".
//
// Event hooks: the keyboard latches (keyboard.cpp, ADB_Micro.hpp) and the Disk II
// write path (diskii_controller.hpp) call through a2gspu_progress_taps.hpp's
// function pointers, which the session binds when it opens the event files.
// Until 2026-09-16 these writers had no callers, and the two files were created
// empty on every session.
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include "cpu.hpp"
#include "a2gspu_observe.hpp"   // a2obs_peek: observer reads never take the CPU path (M11)
#include "a2gspu_progress_taps.hpp"   // g_tap_key_event / g_tap_disk_write_event
#include "a2gspu_artifact_meta.hpp"   // sidecars for the RAM and HGR dumps (M6)
#include "mmus/mmu_iie.hpp"           // the IIe's 128K RAM array
#include "a2gspu_rail_setters.hpp"    // the launch applies each tap rail through its setter

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
    uint8_t  disk_seen[2][160] = {{0}};
    int      disk_seen_sec = -1;
    // keypress captures: the CPU the session last served, and post captures owed
    cpu_state *cpu = nullptr;
    int      key_post_frames = 30;
    int      post_seq[64] = {0};
    uint64_t post_due[64] = {0};
    int      npost = 0;
    char     dump_on[32] = {0};           // A2GSPU_TAP_DUMP_ON, re-applied when the PC list changes
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
            uint8_t b = a2obs_peek(cpu->mmu, base + c);
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

// A bank-$00 range as the CPU sees it now, through the observer read, with its sidecar.
inline void gtap_write_cpu_view(cpu_state *cpu, const char *path, uint32_t lo, uint32_t hi, const char *what) {
    try {
    uint64_t h = HOUSE_FNV_BASIS;
    std::vector<uint8_t> bytes;
    for (uint32_t a = lo; a <= hi; a++) { const uint8_t v = a2obs_peek(cpu->mmu, a); bytes.push_back(v); h ^= v; h *= HOUSE_FNV_PRIME; }
    a2artifact::write_file(path,bytes.data(),bytes.size());
    char layout[160];
    snprintf(layout, sizeof layout, "u8 x %u: $%04X-$%04X as the CPU sees it (observer read; $C000-$CFFF from the probe), %s",
             (unsigned)(hi - lo + 1), (unsigned)lo, (unsigned)hi, what);
    a2gspu_artifact_require_meta(path, "cpu-view-v1", 1, hi - lo + 1, layout, "house-FNV-1a-64 over the file bytes", h);
    } catch(const a2artifact::Error &error) {a2artifact::fatal(error);}
}

// Full RAM. A IIe or IIc keeps main and aux RAM in one 128K array; that array is
// the whole of what the machine remembers, whichever soft switches are showing
// which part to the CPU, so it is written as stored. A 48K II and a IIgs write
// $0000-$BFFF as the CPU sees it (a IIgs has megabytes; the tap is a II-family tool).
inline void gtap_write_full_ram(cpu_state *cpu, const char *tag) {
    if (!g_gtap.sess_on) return;
    char p[600];
    snprintf(p, sizeof(p), "%s/ram_full_%s.bin", g_gtap.dir.c_str(), tag);
    MMU_IIe *iie = a2obs_is_iigs(cpu->mmu) ? nullptr : dynamic_cast<MMU_IIe *>(cpu->mmu);
    if (iie && iie->get_memory_base()) {
        try {
            const uint8_t *ram = iie->get_memory_base();
            a2artifact::write_file(p,ram,0x20000);
            uint64_t h = HOUSE_FNV_BASIS;
            for (size_t i = 0; i < 0x20000; i++) { h ^= ram[i]; h *= HOUSE_FNV_PRIME; }
            a2gspu_artifact_require_meta(p, "iie-ram-array-v1", 1, 0x20000,
                                 "u8 x 131072: the IIe MMU's RAM array as stored -- main $0000-$BFFF; language card bank-1 $D000-$DFFF at $C000, bank-2 at $D000, $E000-$FFFF; aux the same at +$10000",
                                 "house-FNV-1a-64 over the file bytes", h);
            printf("A2GSPU_TAP: full RAM dump (128K, main+aux) -> %s\n", p);
        } catch(const a2artifact::Error &error) {a2artifact::fatal(error);}
        return;
    }
    gtap_write_cpu_view(cpu, p, 0x0000, 0xBFFF, "a full dump on a machine without a IIe RAM array");
    printf("A2GSPU_TAP: full RAM dump (48K as seen) -> %s\n", p);
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
    a2record::Record rec("tap", "hit", a2record::frame_now(), (unsigned long long)cycles);
    rec.f0("ts_ms", ts_ms).i("ts_wall", (long long)time(nullptr)).str("op", w->label).u("pc", pc)
       .u("a", cpu->a & 0xFF).u("x", cpu->x & 0xFF).u("y", cpu->y & 0xFF).u("sp", cpu->sp & 0xFFFF);
    // extra named ZP-pointer fields (mp / mp2 / ipc / np ...)
    for (int i = 0; i < g_gtap.nptr; i++) {
        uint8_t z = g_gtap.ptrs[i].zp;
        uint16_t v = (uint16_t)(a2obs_peek(cpu->mmu, z) | (a2obs_peek(cpu->mmu, (uint8_t)(z + 1)) << 8));
        rec.u(g_gtap.ptrs[i].name, v);
    }
    // operand bytes following the p-code IPC pointer
    uint8_t op0 = 0;
    if (w->nops > 0) {
        uint16_t ipc = (uint16_t)(a2obs_peek(cpu->mmu, w->zp) |
                                  (a2obs_peek(cpu->mmu, (uint8_t)(w->zp + 1)) << 8));
        std::string ops = "[";
        for (int k = 1; k <= w->nops; k++) {
            uint8_t b = a2obs_peek(cpu->mmu, (uint16_t)(ipc + k));
            if (k == 1) op0 = b;
            char d[8]; snprintf(d, sizeof d, "%s%u", k > 1 ? "," : "", (unsigned)b); ops += d;
        }
        ops += ']';
        rec.raw("operands", ops.c_str());
    }
    rec.write(g_gtap.out, false);
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

    g_gtap.cpu = cpu;
    if (g_gtap.full_dump_req) {
        g_gtap.full_dump_req = false;
        char tag[64]; snprintf(tag, sizeof(tag), "t%03d_%s", sec, g_gtap.full_dump_tag);
        gtap_write_full_ram(cpu, tag);
    }

    // Post-key captures that have come due.
    for (int i = 0; i < g_gtap.npost;) {
        if (g_gtap.frame < g_gtap.post_due[i]) { i++; continue; }
        const int seq = g_gtap.post_seq[i];
        char p[600];
        snprintf(p, sizeof(p), "%s/keypress_%04d_post_hgr.bin", g_gtap.dir.c_str(), seq);
        gtap_write_cpu_view(cpu, p, 0x2000, 0x5FFF, "HGR pages 1 and 2, KEY_POST_FRAMES after the key");
        if (g_gtap.keys_f) {
            std::string page; gtap_page_json(cpu, page);
            a2record::Record("tap", "keypost", a2record::frame_now(), (unsigned long long)cycles)
                .i("seq", seq).f0("ts_ms", gtap_ts_ms(cycles)).i("frames_after", g_gtap.key_post_frames)
                .raw("text_page_post", ("\"" + page + "\"").c_str())
                .write(g_gtap.keys_f, true);
        }
        for (int j = i + 1; j < g_gtap.npost; j++) {             // keep key order
            g_gtap.post_seq[j - 1] = g_gtap.post_seq[j];
            g_gtap.post_due[j - 1] = g_gtap.post_due[j];
        }
        g_gtap.npost--;
    }

    if (g_gtap.zp_f && sec >= g_gtap.next_zp_s) {
        g_gtap.next_zp_s = sec + g_gtap.zp_every_s;
        char hex[513];
        for (int i = 0; i < 256; i++) snprintf(hex + i * 2, 3, "%02x", a2obs_peek(cpu->mmu, (uint16_t)i));
        a2record::Record("tap", "zp", a2record::frame_now(), (unsigned long long)cycles)
            .f0("ts_ms", gtap_ts_ms(cycles)).str("zp", hex).write(g_gtap.zp_f, true);
    }

    if (sec >= g_gtap.next_dump_s) {
        g_gtap.next_dump_s = sec + g_gtap.dump_every_s;
        char p[600];
        snprintf(p, sizeof(p), "%s/ram_dump_periodic_t%03d.bin", g_gtap.dir.c_str(), sec);
        gtap_write_cpu_view(cpu, p, 0x0400, 0xBFFF, "every DUMP_EVERY s");
        snprintf(p, sizeof(p), "%s/hgr_dump_periodic_t%03d.bin", g_gtap.dir.c_str(), sec);
        gtap_write_cpu_view(cpu, p, 0x2000, 0x5FFF, "HGR pages 1 and 2, every DUMP_EVERY s");
        char tag[32]; snprintf(tag, sizeof(tag), "periodic_t%03d", sec);
        gtap_write_full_ram(cpu, tag);
        snprintf(p, sizeof(p), "%s/hgr_dump_periodic_t%03d.bin.meta", g_gtap.dir.c_str(), sec);
        {
            a2artifact::Output output(p);FILE *f=output.get();
            a2record::Record("tap", "dumpmeta", a2record::frame_now(), (unsigned long long)cycles)
                .i("emu_t", sec)
                // Display switches from the video scanner (what `vid` reads), not by
                // reading $C01A-$C01D through the CPU path -- a soft-switch READ is a
                // bus access with handlers behind it. -1 = no scanner: say so.
                .u("page2_active", (g_a2obs_video_flags && g_a2obs_video_flags() >= 0) ? ((g_a2obs_video_flags() & 4) ? 1 : 0) : 0)
                .u("hires_on",     (g_a2obs_video_flags && g_a2obs_video_flags() >= 0) ? ((g_a2obs_video_flags() & 8) ? 1 : 0) : 0)
                .u("text_on",      (g_a2obs_video_flags && g_a2obs_video_flags() >= 0) ? ((g_a2obs_video_flags() & 1) ? 1 : 0) : 0)
                .u("mixed_on",     (g_a2obs_video_flags && g_a2obs_video_flags() >= 0) ? ((g_a2obs_video_flags() & 2) ? 1 : 0) : 0)
                .str("video_src",  (g_a2obs_video_flags && g_a2obs_video_flags() >= 0) ? "scanner" : "unavailable")
                .write(f, false);
            output.finish();
        }
        if (g_gtap.pages_f) {
            std::string page; gtap_page_json(cpu, page);
            a2record::Record("tap", "page", a2record::frame_now(), (unsigned long long)cycles)
                .f0("ts_ms", gtap_ts_ms(cycles)).raw("text_page_ascii", ("\"" + page + "\"").c_str())
                .write(g_gtap.pages_f, true);
        }
    }
}

// ---- event hooks (bound in generic_tap_arm) -----------------------------------
// A key reached the keyboard latch. The text page and the HGR pages are taken
// now, before the program has read the key; the post capture is owed
// KEY_POST_FRAMES frames later.
inline void generic_tap_keypress(uint8_t key) {
    if (!g_gtap.keys_f) return;
    g_gtap.key_seq++;
    const unsigned long long cycles = a2record::cycles_now();
    char chr = (key >= 32 && key < 127) ? (char)key : '.';
    const char chrs[2] = { chr, 0 };
    a2record::Record rec("tap", "key", a2record::frame_now(), cycles);
    rec.i("seq", g_gtap.key_seq).u("ascii", key).str("chr", chrs).f0("ts_ms", gtap_ts_ms(cycles))
       .i("ts_wall", (long long)time(nullptr));
    if (g_gtap.cpu) {
        std::string page; gtap_page_json(g_gtap.cpu, page);
        rec.raw("text_page_pre", ("\"" + page + "\"").c_str());
        char p[600];
        snprintf(p, sizeof(p), "%s/keypress_%04d_pre_hgr.bin", g_gtap.dir.c_str(), g_gtap.key_seq);
        gtap_write_cpu_view(g_gtap.cpu, p, 0x2000, 0x5FFF, "HGR pages 1 and 2 when the key reached the latch");
        if (g_gtap.npost < 64) {
            g_gtap.post_seq[g_gtap.npost] = g_gtap.key_seq;
            g_gtap.post_due[g_gtap.npost] = g_gtap.frame + (uint64_t)g_gtap.key_post_frames;
            g_gtap.npost++;
        }
    } else {
        rec.null("text_page_pre");   // a key before the session served its first frame
    }
    rec.write(g_gtap.keys_f, true);
}
// A Disk II laid bits on a mounted 5.25 disk: one record per drive and
// quarter-track per emulated second.
inline void generic_tap_disk_write(int qtrack, int drive) {
    if (!g_gtap.disk_f) return;
    int sec = (int)(g_gtap.frame / 60);
    if (sec != g_gtap.disk_seen_sec) { memset(g_gtap.disk_seen, 0, sizeof(g_gtap.disk_seen)); g_gtap.disk_seen_sec = sec; }
    const int d = drive ? 1 : 0;
    if (qtrack < 0 || qtrack >= 160 || g_gtap.disk_seen[d][qtrack]) return;
    g_gtap.disk_seen[d][qtrack] = 1;
    const unsigned long long cycles = a2record::cycles_now();
    a2record::Record("tap", "diskwr", a2record::frame_now(), cycles)
        .str("op", "DISKWR").i("drive", d + 1).i("qtrack", qtrack).f2("track", qtrack / 4.0)
        .f0("ts_ms", gtap_ts_ms(cycles)).write(g_gtap.disk_f, true);
}

// ---- env parsing / arm / close ----------------------------------------------
inline void gtap_close_files(bool include_out) {
    std::string failure;
    g_tap_key_event = nullptr; g_tap_disk_write_event = nullptr;   // before the files they write close
    g_gtap.sess_on=false;g_gtap.npost=0;
    auto close=[&](FILE *&file,const std::string &path){try {a2artifact::finish(file,path.c_str());}catch(const a2artifact::Error &error){if(failure.empty())failure=error.what();}};
    close(g_gtap.zp_f,g_gtap.dir+"/zp_stream.ndjson");
    close(g_gtap.pages_f,g_gtap.dir+"/text_pages.ndjson");
    close(g_gtap.keys_f,g_gtap.dir+"/keypress_events.ndjson");
    close(g_gtap.disk_f,g_gtap.dir+"/disk_writes.ndjson");
    if(include_out){g_gtap.on=false;const bool had_out=g_gtap.out!=nullptr;close(g_gtap.out,"A2GSPU_TAP");if(had_out && failure.empty())printf("A2GSPU_TAP: %llu events captured\n",(unsigned long long)g_gtap.count);}
    if(!failure.empty())throw a2artifact::Error("tap-close",failure.c_str(),0);
}
inline void generic_tap_close() {gtap_close_files(true);}
inline void generic_tap_exit_close() {try {generic_tap_close();}catch(const a2artifact::Error &error){a2artifact::fatal_on_exit(error);}}

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

// ---- apply functions: the setters in a2gspu_rail_setters.cpp call these, and the
// launch reaches them through those setters, so `rail set` cannot parse a value
// differently from the environment. -------------------------------------------------
inline void gtap_apply_dump_on() {
    for (int i = 0; i < g_gtap.nwatch; i++) { g_gtap.watch[i].dumpop = false; g_gtap.watch[i].dumpop_min = 0; }
    if (!g_gtap.dump_on[0]) return;
    char lbl[16]; int mn = 0;
    if (sscanf(g_gtap.dump_on, "%15[^:]:%d", lbl, &mn) >= 1)
        for (int i = 0; i < g_gtap.nwatch; i++)
            if (!strcmp(g_gtap.watch[i].label, lbl)) { g_gtap.watch[i].dumpop = true; g_gtap.watch[i].dumpop_min = (uint8_t)mn; }
}
inline void gtap_set_pcs(const char *spec) {
    g_gtap.nwatch = 0;
    if (spec) gtap_parse_pcs(spec);
    gtap_apply_dump_on();
}
inline void gtap_set_ptrs(const char *spec) {
    g_gtap.nptr = 0;
    if (spec) gtap_parse_ptrs(spec);
}
// The call tap's record file. Closing it leaves the gate on for a title config's
// extension hook (wiz5_config.hpp), which rides the same per-instruction visit.
inline bool gtap_set_out(const char *path) {
    g_gtap.on = (g_gtap_ext != nullptr);
    if(g_gtap.out){a2artifact::finish(g_gtap.out,"A2GSPU_TAP");printf("A2GSPU_TAP: %llu events captured\n",(unsigned long long)g_gtap.count);}
    if (!path) return true;
    g_gtap.out = fopen(path, "w");
    if (!g_gtap.out) return false;
    a2outputs::add("tap",generic_tap_close);
    g_gtap.on = true; g_gtap.count = 0;
    return true;
}
inline void gtap_close_session() {
    gtap_close_files(false);
}
inline bool gtap_set_session(const char *dir) {
    gtap_close_session();
    if (!dir) return true;
    FILE *files[4]={};const char *names[]={"zp_stream.ndjson","text_pages.ndjson","keypress_events.ndjson","disk_writes.ndjson"};
    for(int i=0;i<4;++i) {
        const std::string path=std::string(dir)+"/"+names[i];files[i]=fopen(path.c_str(),"w");
        if(!files[i]) {
            for(auto &file:files)try {a2artifact::finish(file,dir);}catch(const a2artifact::Error &error){fprintf(stderr,"A2GSPU_TAP: FAIL rollback %s\n",error.what());}
            return false;
        }
    }
    g_gtap.zp_f=files[0];g_gtap.pages_f=files[1];g_gtap.keys_f=files[2];g_gtap.disk_f=files[3];
    a2outputs::add("tap",generic_tap_close);
    g_gtap.dir = dir; g_gtap.sess_on = true;
    g_gtap.frame = 0;
    g_gtap.next_dump_s = g_gtap.dump_every_s; g_gtap.next_zp_s = g_gtap.zp_every_s;
    if (g_gtap.keys_f) g_tap_key_event = generic_tap_keypress;
    if (g_gtap.disk_f) g_tap_disk_write_event = generic_tap_disk_write;
    return true;
}

// The launch: every A2GSPU_TAP* rail from the environment, through its setter, in
// the order the old block applied them (configuration before the sinks). No-op
// (the emulator stock) when neither A2GSPU_TAP nor A2GSPU_TAP_SESSION is set.
// Called from the spike and from run_one_frame's lazy arm; runs once.
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
    // Each name spelled out, so railparity.ps1 sees every rail read here. A legacy
    // alias applies only when its canonical name is unset.
    a2gspu_rail_arm_env("A2GSPU_TAP_CYC_PER_MS");
    a2gspu_rail_arm_env("A2GSPU_TAP_CYC_PER_SEC");
    if (tap) {
        a2gspu_rail_arm_env("A2GSPU_TAP_PCS");
        a2gspu_rail_arm_env("A2GSPU_TAP_PTRS");
        a2gspu_rail_arm_env("A2GSPU_TAP_MAX");
        a2gspu_rail_arm_env("A2GSPU_TAP_DUMP_ON");
        if (getenv("A2GSPU_TAP")) a2gspu_rail_arm_env("A2GSPU_TAP");
        else a2gspu_rail_arm_value("A2GSPU_TAP", getenv("WIZ5_TAP"), "WIZ5_TAP");
    }
    if (sess) {
        if (getenv("A2GSPU_TAP_DUMP_EVERY")) a2gspu_rail_arm_env("A2GSPU_TAP_DUMP_EVERY");
        else a2gspu_rail_arm_value("A2GSPU_TAP_DUMP_EVERY", getenv("WIZ5_DUMP_EVERY"), "WIZ5_DUMP_EVERY");
        if (getenv("A2GSPU_TAP_ZP_EVERY")) a2gspu_rail_arm_env("A2GSPU_TAP_ZP_EVERY");
        else a2gspu_rail_arm_value("A2GSPU_TAP_ZP_EVERY", getenv("WIZ5_ZP_EVERY"), "WIZ5_ZP_EVERY");
        a2gspu_rail_arm_env("A2GSPU_TAP_KEY_POST_FRAMES");
        if (getenv("A2GSPU_TAP_SESSION")) a2gspu_rail_arm_env("A2GSPU_TAP_SESSION");
        else a2gspu_rail_arm_value("A2GSPU_TAP_SESSION", getenv("WIZ5_SESSION"), "WIZ5_SESSION");
    }
    atexit(generic_tap_exit_close);
}
