#pragma once
// ============================================================================
// wiz5_config.hpp — WIZ5_* title/interp-specific CONFIG layer.
//
// The ONE-tool policy (no fork-per-title): the generic, title-AGNOSTIC oracle
// lives in generic_tap.hpp (A2GSPU_TAP*). THIS header carries the handful of
// Wizardry-V / Sir-Tech UCSD-p-code-interpreter-SPECIFIC rails that the
// wiz5_clean project drives gssquared with, KEEPING the WIZ5_ env names so no
// project script needs to change. It is deliberately quarantined in its own
// header (and its own "wiz5-config" section of gs2.cpp) so an upstream merge of
// gs2.cpp/generic_tap.hpp stays trivially reviewable — every hard-coded PC / ZP
// address the Sir-Tech interpreter needs is confined here.
//
// EVERYTHING here is ADDITIVE + env-gated OFF by default. With NO WIZ5_* var
// set: no file opens, g_gtap_ext stays null, the per-instruction dispatcher is
// never registered, and wiz5_keys_tick returns on its first branch — so the
// windowed emulator and the headless spike are byte-for-byte their stock
// selves (dual-mode invariant / baseline spike hash preserved).
//
// Rails carried here (see gs2.cpp wiz5-config block for arm/close wiring):
//   WIZ5_KEYS=<file>        scheduled keyboard injection (frame/WAIT entries)
//   WIZ5_KEY_DELAY=<frames> paced 1-key-per-N-frames drain (redraw-heavy screens)
//   WIZ5_KEYRAM             per-injected-keystroke $0800-$BFFF RAM dump
//   WIZ5_EC19_OUT=<file>    SCREENDEV native-driver ($EC19/$EC28) sub-code trap
//   WIZ5_DISKWR_OUT=<file>  UCSD UNITWRITE/UNITREAD block byte capture ($DF09/$DF04)
//   WIZ5_CSP6_OUT=<file>    CSP-6 (UNITWRITE) p-code eval-stack + 6502 stack/zp dump ($D681)
//
// AGENTS.md RULE 2 SAYS "no hard-coded product memory maps in the emulator",
// AND THIS FILE IS THE ONE STANDING EXCEPTION TO IT. That was true for a long
// time and written down everywhere except the file an agent is told to obey:
// INSTRUMENTATION_SHAPE.md section 1 named it, M10 set its retirement
// condition, railparity.ps1 granted it an exemption (for the record shape, not
// for the addresses) -- and AGENTS.md listed rule 2 as an absolute with no
// pointer here. An agent finding this file therefore had two wrong conclusions
// available: that it is a defect to delete (it is load-bearing for a live
// consumer), or that the rules are advisory (rule 4 is not, and AGENTS.md says
// so). The exception is now recorded in AGENTS.md under rule 2, with the
// retirement condition: these traps go when their consumers read the generic
// A2GSPU_TRAP records, and not before. Do not add a second file like this one.
//
// The three PC traps ride the generic tap's existing per-instruction call site
// (base_6502.cpp: `if (g_gtap.on) generic_tap_check(...)`) via the g_gtap_ext
// hook registered by wiz5_config_arm() — so NO cpu/device file is touched.
// (WIZ5_SESSION / WIZ5_TAP / WIZ5_DUMP_EVERY / WIZ5_ZP_EVERY are handled as
// aliases of the A2GSPU_TAP* names directly inside generic_tap.hpp.)
// ============================================================================
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "cpu.hpp"
#include "computer.hpp"
#include "Module_ID.hpp"
#include "devices/keyboard/keyboard.hpp"
#include "generic_tap.hpp"   // g_gtap (on/dir) + g_gtap_ext extension hook
#include "devices/adb/keygloo.hpp"   // the IIgs key path for schedules and PRESS
#include "iigs_diag.hpp"             // a2gspu_warn

// UCSD p-code interpreter time base (NClock cycles at ~1.0205 MHz), matching
// the schema the wiz5_clean Python analysis stack already consumes.
inline double wiz5_cfg_ts_ms(uint64_t cycles) { return cycles / 1020.484; }

// ---------------------------------------------------------------------------
// (A) SCREENDEV native-driver trap ($EC19). Sir-Tech's native UNITWRITE hook
// dispatches by sub-code ($32) through $EC19. Logs the sub-code for EVERY call
// and samples a heavy payload (compressed input window @ $30, live decode
// tables $0D00-$0FFF) a few times per distinct sub-code; at driver exit ($EC28)
// logs the decoded output window (@ $34) + output length ($14). Independent of
// the tap. Env: WIZ5_EC19_OUT=<path>. 65816 core (-p 4/5) only.
// ---------------------------------------------------------------------------
inline FILE *g_wiz5_ec19_out = nullptr;
inline int   g_wiz5_ec19_lines = 0, g_wiz5_ec19_maxlines = 20000;
inline int   g_wiz5_ec19_cur = -1;
inline bool  g_wiz5_ec19_cur_heavy = false;
inline unsigned char g_wiz5_ec19_subseen[256] = {0};

inline void wiz5_ec19_check(cpu_state *cpu) {
    if (!g_wiz5_ec19_out) return;
    const uint16_t pc = (uint16_t)(cpu->full_pc & 0xFFFF);
    auto rd = [&](uint16_t a) { return (unsigned)(cpu->mmu->read(a) & 0xFF); };
    if (pc == 0xEC19 && g_wiz5_ec19_lines < g_wiz5_ec19_maxlines) {
        const unsigned sub = rd(0x32);
        const uint16_t src = (uint16_t)(rd(0x30) | (rd(0x31) << 8));
        const uint16_t dst = (uint16_t)(rd(0x34) | (rd(0x35) << 8));
        const bool heavy = g_wiz5_ec19_subseen[sub] < 4;   // 4 samples per sub-code
        g_wiz5_ec19_cur = g_wiz5_ec19_lines;
        g_wiz5_ec19_cur_heavy = heavy;
        fprintf(g_wiz5_ec19_out, "{\"t\":\"e\",\"n\":%d,\"sub\":%u,\"src\":%u,\"dst\":%u",
                g_wiz5_ec19_lines, sub, src, dst);
        if (heavy) {
            fprintf(g_wiz5_ec19_out, ",\"srcw\":\"");
            for (int i = 0; i < 192; i++) fprintf(g_wiz5_ec19_out, "%02x", rd((uint16_t)(src + i)));
            fprintf(g_wiz5_ec19_out, "\",\"tbl\":\"");
            for (int i = 0; i < 768; i++) fprintf(g_wiz5_ec19_out, "%02x", rd((uint16_t)(0x0D00 + i)));
            fprintf(g_wiz5_ec19_out, "\"");
            g_wiz5_ec19_subseen[sub]++;
        }
        fprintf(g_wiz5_ec19_out, "}\n");
        fflush(g_wiz5_ec19_out);
        g_wiz5_ec19_lines++;
    } else if (pc == 0xEC28 && g_wiz5_ec19_cur >= 0) {
        const uint16_t dst = (uint16_t)(rd(0x34) | (rd(0x35) << 8));
        const unsigned outidx = rd(0x14);
        fprintf(g_wiz5_ec19_out, "{\"t\":\"x\",\"n\":%d,\"dst\":%u,\"outidx\":%u",
                g_wiz5_ec19_cur, dst, outidx);
        if (g_wiz5_ec19_cur_heavy) {
            fprintf(g_wiz5_ec19_out, ",\"dstw\":\"");
            for (int i = 0; i < 192; i++) fprintf(g_wiz5_ec19_out, "%02x", rd((uint16_t)(dst + i)));
            fprintf(g_wiz5_ec19_out, "\"");
        }
        fprintf(g_wiz5_ec19_out, "}\n");
        fflush(g_wiz5_ec19_out);
        g_wiz5_ec19_cur = -1;
    }
}

// ---------------------------------------------------------------------------
// (B) Disk block-write byte capture. The UCSD UNITWRITE CSP pops its params to
// zero page then JSRs the unit dispatcher $DF09 with $2D=unit $30/31=buffer
// $32/33=length $34/35=block X=1(write). Trapping $DF09 for a disk unit with
// X==1 logs every logical block write {ts,unit,block,len,data}; UNITREAD (X==0)
// data is only valid once the dispatch returns to $DF04, so the first read of
// each block is logged there (pre-write baseline for old->new diffing).
// Env: WIZ5_DISKWR_OUT=<path>. 65816 core (-p 4/5) only.
// ---------------------------------------------------------------------------
inline FILE *g_wiz5_diskwr_out = nullptr;
inline int   g_wiz5_diskwr_n = 0, g_wiz5_diskwr_max = 40000;
inline int      g_wiz5_diskrd_pblk = -1;
inline unsigned g_wiz5_diskrd_punit = 0, g_wiz5_diskrd_pbuf = 0, g_wiz5_diskrd_plen = 0;
inline bool     g_wiz5_diskrd_seen[2048] = {false};

inline void wiz5_diskwr_check(cpu_state *cpu, double ts_ms) {
    if (!g_wiz5_diskwr_out) return;
    const uint16_t pc = (uint16_t)(cpu->full_pc & 0xFFFF);
    auto rd = [&](uint16_t a) { return (unsigned)(cpu->mmu->read(a) & 0xFF); };
    if (pc == 0xDF09) {
        unsigned unit = rd(0x2D);
        if (!(unit == 4 || unit == 5 || (unit >= 9 && unit <= 12))) return;  // disk units
        unsigned buf = rd(0x30) | (rd(0x31) << 8);
        unsigned len = rd(0x32) | (rd(0x33) << 8);
        unsigned blk = rd(0x34) | (rd(0x35) << 8);
        if ((cpu->x & 0xFF) == 1) {                          // UNITWRITE
            if (g_wiz5_diskwr_n >= g_wiz5_diskwr_max) return;
            unsigned cap = len > 512 ? 512 : len;
            fprintf(g_wiz5_diskwr_out,
                    "{\"ts_ms\":%.0f,\"op\":\"BLKWR\",\"unit\":%u,\"block\":%u,\"len\":%u,\"data\":\"",
                    ts_ms, unit, blk, len);
            for (unsigned i = 0; i < cap; i++) fprintf(g_wiz5_diskwr_out, "%02x", rd((uint16_t)(buf + i)));
            fprintf(g_wiz5_diskwr_out, "\"}\n");
            fflush(g_wiz5_diskwr_out);
            g_wiz5_diskwr_n++;
        } else {                                             // UNITREAD -> log at $DF04
            g_wiz5_diskrd_pblk = (int)blk;
            g_wiz5_diskrd_punit = unit; g_wiz5_diskrd_pbuf = buf; g_wiz5_diskrd_plen = len;
        }
    } else if (pc == 0xDF04 && g_wiz5_diskrd_pblk >= 0) {
        unsigned blk = (unsigned)g_wiz5_diskrd_pblk;
        g_wiz5_diskrd_pblk = -1;
        if (blk < 2048 && !g_wiz5_diskrd_seen[blk] && g_wiz5_diskwr_n < g_wiz5_diskwr_max) {
            g_wiz5_diskrd_seen[blk] = true;
            unsigned cap = g_wiz5_diskrd_plen > 512 ? 512 : g_wiz5_diskrd_plen;
            fprintf(g_wiz5_diskwr_out,
                    "{\"ts_ms\":%.0f,\"op\":\"BLKRD\",\"unit\":%u,\"block\":%u,\"len\":%u,\"data\":\"",
                    ts_ms, g_wiz5_diskrd_punit, blk, g_wiz5_diskrd_plen);
            for (unsigned i = 0; i < cap; i++)
                fprintf(g_wiz5_diskwr_out, "%02x", rd((uint16_t)(g_wiz5_diskrd_pbuf + i)));
            fprintf(g_wiz5_diskwr_out, "\"}\n");
            fflush(g_wiz5_diskwr_out);
            g_wiz5_diskwr_n++;
        }
    }
}

// ---------------------------------------------------------------------------
// (C) CSP-6 (UNITWRITE) eval-stack trap. The CSP handler entry is $D681 with
// the sub-opcode at IPC+1 ($9E/$9F -> p-code opcode byte). When sub == 6 dump
// the 6502 hardware stack (page 1, the likely UCSD eval stack), zero page, and
// a 48-byte window of every 16-bit RAM pointer found in either — the native
// SCREENDEV=18 decrypt's enciphered INPUT is captured at the exact call.
// Env: WIZ5_CSP6_OUT=<path>. Shares the interpreter PC convention with the tap.
// ---------------------------------------------------------------------------
inline FILE *g_wiz5_csp6_out = nullptr;
inline int   g_wiz5_csp6_max = 4000, g_wiz5_csp6_n = 0;

inline void wiz5_csp6_check(cpu_state *cpu, double ts_ms) {
    if (!g_wiz5_csp6_out) return;
    if ((cpu->full_pc & 0xFFFF) != 0xD681) return;
    if (g_wiz5_csp6_n >= g_wiz5_csp6_max) return;
    const uint16_t ipc = (uint16_t)(cpu->mmu->read(0x9E) | (cpu->mmu->read(0x9F) << 8));
    const uint8_t sub = cpu->mmu->read((uint16_t)(ipc + 1));
    if (sub != 6) return;                                    // CSP-6 only
    const uint16_t sp1 = (uint16_t)(0x0100 | (cpu->sp & 0xFF));
    fprintf(g_wiz5_csp6_out, "{\"ipc\":%u,\"ts_ms\":%.0f,\"sp\":%u,\"page1\":\"", ipc, ts_ms, sp1);
    for (int k = 1; k <= 40; k++)
        fprintf(g_wiz5_csp6_out, "%02x", cpu->mmu->read((uint16_t)(0x0100 | ((cpu->sp + k) & 0xFF))));
    fprintf(g_wiz5_csp6_out, "\",\"zp\":\"");
    for (int k = 0; k < 0x60; k++) fprintf(g_wiz5_csp6_out, "%02x", cpu->mmu->read((uint16_t)k));
    fprintf(g_wiz5_csp6_out, "\",\"bufs\":[");
    int nb = 0;
    for (int src = 0; src < 2; src++) {
        int lo = src ? 0 : 1, hi = src ? 0x5E : 40;
        for (int k = lo; k <= hi; k++) {
            uint16_t base = src ? (uint16_t)k : (uint16_t)(0x0100 | ((cpu->sp + k) & 0xFF));
            uint16_t ptr = (uint16_t)(cpu->mmu->read(base) | (cpu->mmu->read((uint16_t)(base + 1)) << 8));
            if (ptr >= 0x0800 && ptr < 0xC000 && nb < 24) {
                fprintf(g_wiz5_csp6_out, "%s{\"at\":\"%s%d\",\"ptr\":%u,\"d\":\"",
                        nb ? "," : "", src ? "zp" : "s", k, ptr);
                for (int j = 0; j < 40; j++) fprintf(g_wiz5_csp6_out, "%02x", cpu->mmu->read((uint16_t)(ptr + j)));
                fprintf(g_wiz5_csp6_out, "\"}"); nb++;
            }
        }
    }
    fprintf(g_wiz5_csp6_out, "]}\n");
    g_wiz5_csp6_n++;
}

// ---- per-instruction dispatcher (registered into g_gtap_ext) ----------------
// Runs on every instruction the generic-tap gate already visits. Each sub-trap
// self-gates on its own output FILE*, so with none armed this is three untaken
// branches; and the dispatcher is only registered (and g_gtap.on forced) when
// at least one of the three is armed (see wiz5_config_arm).
inline void wiz5_config_step(cpu_state *cpu, uint64_t cycles) {
    const double ts_ms = wiz5_cfg_ts_ms(cycles);
    if (g_wiz5_ec19_out)   wiz5_ec19_check(cpu);
    if (g_wiz5_diskwr_out) wiz5_diskwr_check(cpu, ts_ms);
    if (g_wiz5_csp6_out)   wiz5_csp6_check(cpu, ts_ms);
}

// ---------------------------------------------------------------------------
// (D) WIZ5_KEYS / WIZ5_KEY_DELAY / WIZ5_KEYRAM — scheduled keyboard injection.
// Sequential entries; each is EITHER a frame trigger ("<frame> <keys>") or a
// text condition ("WAIT <substring>" matched against the live $0400 text page,
// polled 4x/sec). WIZ5_KEY_DELAY>0 paces one key per N frames (redraw-heavy
// screens). WIZ5_KEYRAM dumps $0800-$BFFF at each injected keystroke.
// Driven per-frame from run_one_frame (the single windowed+headless funnel).
// ---------------------------------------------------------------------------
struct Wiz5KeyEntry { int frame; std::string wait_text; std::string keys; int press_key = -1; uint8_t press_mods = 0; };
inline int g_wiz5_press_release_frame = -1;   // frame at which a PRESS entry's key/mods are released
inline uint8_t g_wiz5_press_mods_held = 0;
inline std::vector<Wiz5KeyEntry> g_wiz5_keys;
inline size_t g_wiz5_keys_next = 0;
inline int    g_wiz5_key_delay = 0;
inline std::string g_wiz5_key_pending;   // remaining chars of the current entry
inline int    g_wiz5_key_last = -100000;
inline bool   g_wiz5_keyram_on = false;
inline std::string g_wiz5_session_dir;   // KEYRAM sink dir (from WIZ5_SESSION)
inline int    g_wiz5_keyram_seq = 0;
inline int    g_wiz5_frame = 0;          // executed-frame counter (per run_one_frame)

// MAME tap ASCII convention: $00-$1F -> '@'+b, $20-$7E literal, hi-bit //e text.
inline void wiz5_cfg_read_text_page(cpu_state *cpu, char out[24][41]) {
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

// Per-injected-keystroke RAM dump ($0800-$BFFF) into the session dir.
inline void wiz5_keyram_dump(computer_t *computer) {
    if (!g_wiz5_keyram_on || g_wiz5_session_dir.empty()) return;
    cpu_state *cpu = computer->cpu;
    char rp[600];
    snprintf(rp, sizeof(rp), "%s/keypress_%04d_ram.bin",
             g_wiz5_session_dir.c_str(), ++g_wiz5_keyram_seq);
    if (FILE *rf = fopen(rp, "wb")) {
        for (uint32_t a = 0x0800; a < 0xC000; a++) fputc(cpu->mmu->read((uint16_t)a) & 0xFF, rf);
        fclose(rf);
    }
}

inline void wiz5_inject(computer_t *computer, const std::string &keys, int frame) {
    keyboard_state_t *kb = (keyboard_state_t *)computer->get_module_state(MODULE_KEYBOARD);
    keygloo_state_t *kg = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
    if (kb && !keys.empty()) {
        kb->paste_buffer += keys;
        printf("WIZ5_KEYS: frame %d injected %d chars\n", frame, (int)keys.size());
        wiz5_keyram_dump(computer);
    } else if (kg && kg->kg && !keys.empty()) {
        // A IIgs has no MODULE_KEYBOARD and no paste buffer: its keys arrive over
        // ADB. Same one-force_key-per-char sequence as SPIKE_KEYS and the CTRL
        // `keys` verb, so the three paths cannot diverge. Until this existed a
        // schedule on the IIgs did nothing and said nothing.
        for (char c : keys) { kg->kg->force_key((uint8_t)c); keygloo_update_interrupt_status(kg, kg->kg); }
        kg->kg->key_up();
        printf("WIZ5_KEYS: frame %d injected %d chars (ADB path)\n", frame, (int)keys.size());
        wiz5_keyram_dump(computer);
    } else if (!keys.empty()) {
        a2gspu_warn("WIZ5_KEYS", "no keyboard AND no KeyGloo -- schedule entry skipped");
    }
}

// PRESS <hex2> [mods]: one key with modifiers, through the ADB path (the IIgs
// Finder opens a selected icon on Open-Apple-O; typing 'o' at it starts a
// rename). Held two frames, then the modifiers are released.
inline void wiz5_press(computer_t *computer, int key, uint8_t mods, int frame) {
    keygloo_state_t *kg = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
    if (!(kg && kg->kg)) { a2gspu_warn("WIZ5_KEYS", "PRESS needs the IIgs KeyGloo path -- entry skipped"); return; }
    if (mods) kg->kg->force_key_mods((uint8_t)key, mods); else kg->kg->force_key((uint8_t)key);
    keygloo_update_interrupt_status(kg, kg->kg);
    g_wiz5_press_release_frame = frame + 2;
    g_wiz5_press_mods_held = mods;
    printf("WIZ5_KEYS: frame %d PRESS $%02X mods=$%02X (ADB path)\n", frame, key, mods);
}

// Called once per executed frame from run_one_frame (env-gated: no keys loaded
// => returns immediately). `frame` is the shared executed-frame counter.
inline void wiz5_keys_tick(computer_t *computer, int frame) {
    if (g_wiz5_press_release_frame >= 0 && frame >= g_wiz5_press_release_frame) {
        keygloo_state_t *kg = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
        if (kg && kg->kg) { kg->kg->key_up(); if (g_wiz5_press_mods_held) kg->kg->release_mods(g_wiz5_press_mods_held); }
        g_wiz5_press_release_frame = -1; g_wiz5_press_mods_held = 0;
    }
    if (g_wiz5_keys.empty() && g_wiz5_key_pending.empty()) return;
    // Paced-injection: drain the current entry one key per delay window.
    if (g_wiz5_key_delay > 0 && !g_wiz5_key_pending.empty()) {
        if (frame - g_wiz5_key_last < g_wiz5_key_delay) return;
        keyboard_state_t *kb = (keyboard_state_t *)computer->get_module_state(MODULE_KEYBOARD);
        if (kb) kb->paste_buffer += g_wiz5_key_pending[0];
        g_wiz5_key_pending.erase(0, 1);
        g_wiz5_key_last = frame;
        wiz5_keyram_dump(computer);
        return;                                        // one key per tick
    }
    while (g_wiz5_keys_next < g_wiz5_keys.size()) {
        Wiz5KeyEntry &e = g_wiz5_keys[g_wiz5_keys_next];
        if (!e.wait_text.empty()) {
            if (frame % 15 != 0) return;               // poll the page 4x/sec
            char page[24][41];
            wiz5_cfg_read_text_page(computer->cpu, page);
            bool found = false;
            for (int r = 0; r < 24 && !found; r++)
                if (strstr(page[r], e.wait_text.c_str())) found = true;
            if (!found) return;
            printf("WIZ5_KEYS: frame %d WAIT matched '%s'\n", frame, e.wait_text.c_str());
        } else if (e.frame > frame) {
            return;
        }
        if (g_wiz5_key_delay > 0) {                    // hand off to paced drain
            g_wiz5_key_pending = e.keys;
            g_wiz5_key_last = frame - g_wiz5_key_delay; // first key immediately
            g_wiz5_keys_next++;
            printf("WIZ5_KEYS: frame %d queued %d chars (paced %df/key)\n",
                   frame, (int)e.keys.size(), g_wiz5_key_delay);
            return;
        }
        if (e.press_key >= 0) wiz5_press(computer, e.press_key, e.press_mods, frame);
        else wiz5_inject(computer, e.keys, frame);
        g_wiz5_keys_next++;
    }
}

// ---- arm / close ------------------------------------------------------------
inline void wiz5_config_close() {
    if (g_wiz5_ec19_out)   { fclose(g_wiz5_ec19_out);   g_wiz5_ec19_out = nullptr; }
    if (g_wiz5_diskwr_out) { fclose(g_wiz5_diskwr_out); g_wiz5_diskwr_out = nullptr; }
    if (g_wiz5_csp6_out)   { fclose(g_wiz5_csp6_out);   g_wiz5_csp6_out = nullptr; }
}

// Read every WIZ5_* title rail and open its sinks / load the key schedule. A
// no-op (leaves the emulator stock) when none are set. Idempotent-guarded so
// the single lazy call from run_one_frame is safe on both driver paths.
inline bool g_wiz5_config_armed = false;
inline void wiz5_config_arm(computer_t *computer) {
    if (g_wiz5_config_armed) return;
    g_wiz5_config_armed = true;

    // --- KEYS / KEY_DELAY ---
    g_wiz5_keys.clear(); g_wiz5_keys_next = 0;
    g_wiz5_key_delay = 0; g_wiz5_key_pending.clear(); g_wiz5_key_last = -100000;
    /* Canonical A2GSPU_* names with the WIZ5_* originals kept as aliases, the
     * same way generic_tap.hpp already treats A2GSPU_TAP / WIZ5_TAP. Scheduled
     * keystroke injection and per-keystroke RAM dumps are not Pascal-specific;
     * the product name was history, not scope. Existing scripts keep working.
     *
     * KEYSCHED rather than KEYS: A2GSPU_SPIKE_KEYS already exists and is a
     * literal string injected at a frame, where this is a FILE of scheduled
     * entries. Two rails a letter apart meaning different things is how a
     * caller reaches for the wrong one. */
    const char *kf_env = getenv("A2GSPU_KEYSCHED");
    // Report the name the caller actually set. A diagnostic that names the other
    // spelling sends them looking at a variable they did not touch.
    const char *kf_label = "A2GSPU_KEYSCHED";
    if (!kf_env) { kf_env = getenv("WIZ5_KEYS"); kf_label = "WIZ5_KEYS"; }
    if (const char *kf = kf_env) {
        FILE *f = fopen(kf, "r");
        if (!f) {
            fprintf(stderr, "%s: FAIL cannot open '%s' -- aborting (exit 2)\n", kf_label, kf);
            exit(2);
        } else {
            auto decode = [](const char *p) {
                std::string text;
                for (; *p && *p != '\n' && *p != '\r'; p++) {
                    if (*p == '{') {
                        if (!strncmp(p, "{RETURN}", 8)) { text += '\r'; p += 7; }
                        else if (!strncmp(p, "{ESC}", 5)) { text += '\x1B'; p += 4; }
                        else if (!strncmp(p, "{SPACE}", 7)) { text += ' '; p += 6; }
                        else text += *p;
                    } else text += *p;
                }
                return text;
            };
            char line[512];
            while (fgets(line, sizeof(line), f)) {
                if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
                if (!strncmp(line, "WAIT ", 5)) {
                    std::string cond;
                    for (char *p = line + 5; *p && *p != '\n' && *p != '\r'; p++) cond += *p;
                    if (!cond.empty()) g_wiz5_keys.push_back({-1, cond, ""});
                    continue;
                }
                char *end = nullptr;
                long frame = strtol(line, &end, 10);
                if (end == line) continue;
                while (*end == ' ' || *end == '\t') end++;
                if (!strncmp(end, "PRESS ", 6)) {
                    // <frame> PRESS <hex2> [mods]   mods letters: o Open-Apple, c Closed-Apple, s shift, k control
                    unsigned key = 0; char modw[16] = {0}; uint8_t mods = 0;
                    int got = sscanf(end + 6, "%x %15s", &key, modw);
                    if (got < 1) { fprintf(stderr, "%s: FAIL bad PRESS line '%s' -- aborting (exit 2)\n", kf_label, line); exit(2); }
                    for (const char *p = modw; got == 2 && *p; p++) {
                        if (*p == 'o') mods |= 0x80; else if (*p == 'c') mods |= 0x40;
                        else if (*p == 's') mods |= 0x01; else if (*p == 'k') mods |= 0x02;
                        else if (*p != '\n' && *p != '\r') { fprintf(stderr, "%s: FAIL PRESS mods '%s' -- letters o c s k only (exit 2)\n", kf_label, modw); exit(2); }
                    }
                    Wiz5KeyEntry pe{(int)frame, "", ""}; pe.press_key = (int)(key & 0x7F); pe.press_mods = mods;
                    g_wiz5_keys.push_back(pe);
                    continue;
                }
                std::string text = decode(end);
                if (!text.empty()) g_wiz5_keys.push_back({(int)frame, "", text});
            }
            fclose(f);
            printf("%s: %d scheduled entries from %s\n",
                   kf_label, (int)g_wiz5_keys.size(), kf);
        }
    }
    const char *kd_env = getenv("A2GSPU_KEYSCHED_DELAY");
    if (!kd_env) kd_env = getenv("WIZ5_KEY_DELAY");
    if (const char *kd = kd_env) { int n = atoi(kd); if (n > 0) g_wiz5_key_delay = n; }

    // --- KEYRAM (per-keystroke RAM dump; needs a session dir to write into) ---
    g_wiz5_keyram_on = (getenv("A2GSPU_KEYRAM") != nullptr) ||
                       (getenv("WIZ5_KEYRAM") != nullptr);
    g_wiz5_session_dir.clear();
    if (const char *sd = getenv("WIZ5_SESSION"))          g_wiz5_session_dir = sd;
    else if (const char *sd2 = getenv("A2GSPU_TAP_SESSION")) g_wiz5_session_dir = sd2;
    if (g_wiz5_keyram_on)
        printf("WIZ5_KEYRAM: per-keystroke RAM dump ($0800-$BFFF) enabled -> %s\n",
               g_wiz5_session_dir.empty() ? "(no session dir!)" : g_wiz5_session_dir.c_str());

    // --- WIZ5_NO_WRITEBACK: recognized here (mutation-guard flag). Main writes
    // disk FILES back only on an explicit UI save-and-unmount, so the flag's
    // presence is honored by the WIZ5_WRITEBACK suppression in gs2.cpp; the
    // in-flight per-file write suppression proper lives in the floppy device. ---
    if (getenv("WIZ5_NO_WRITEBACK"))
        printf("WIZ5_NO_WRITEBACK: disk-image files will not be rewritten (mutation guard)\n");

    // --- PC traps (EC19 / DISKWR / CSP6) — ride the generic-tap per-instruction
    // gate via g_gtap_ext; force g_gtap.on so base_6502.cpp visits the hook. ---
    bool any_pc_trap = false;
    if (const char *ep = getenv("WIZ5_EC19_OUT")) {
        g_wiz5_ec19_out = fopen(ep, "w");
        g_wiz5_ec19_lines = 0; g_wiz5_ec19_cur = -1;
        for (int i = 0; i < 256; i++) g_wiz5_ec19_subseen[i] = 0;
        if (g_wiz5_ec19_out) { printf("WIZ5_EC19_OUT: SCREENDEV driver trap -> %s\n", ep); any_pc_trap = true; }
        else { fprintf(stderr, "WIZ5_EC19_OUT: FAIL cannot open '%s' -- aborting (exit 2)\n", ep); exit(2); }
    }
    if (const char *dp = getenv("WIZ5_DISKWR_OUT")) {
        g_wiz5_diskwr_out = fopen(dp, "w");
        g_wiz5_diskwr_n = 0; g_wiz5_diskrd_pblk = -1;
        for (int i = 0; i < 2048; i++) g_wiz5_diskrd_seen[i] = false;
        if (g_wiz5_diskwr_out) { printf("WIZ5_DISKWR_OUT: disk block-write capture -> %s\n", dp); any_pc_trap = true; }
        else { fprintf(stderr, "WIZ5_DISKWR_OUT: FAIL cannot open '%s' -- aborting (exit 2)\n", dp); exit(2); }
    }
    if (const char *cp = getenv("WIZ5_CSP6_OUT")) {
        g_wiz5_csp6_out = fopen(cp, "w");
        g_wiz5_csp6_n = 0;
        if (g_wiz5_csp6_out) { printf("WIZ5_CSP6_OUT: UNITWRITE eval-stack trap -> %s\n", cp); any_pc_trap = true; }
        else { fprintf(stderr, "WIZ5_CSP6_OUT: FAIL cannot open '%s' -- aborting (exit 2)\n", cp); exit(2); }
    }
    if (any_pc_trap) {
        g_gtap_ext = wiz5_config_step;   // register the per-instruction extension
        g_gtap.on = true;                // make base_6502.cpp visit generic_tap_check
        atexit(wiz5_config_close);
    }
    (void)computer;
}
