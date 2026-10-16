/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar

 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.

 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.

 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// The CTRL and co-pilot rails -- moved out of gs2.cpp; see gs2_app.hpp.
#include "gs2_app.hpp"
#include "a2gspu_snapdiff.hpp"
#include "a2gspu_events.hpp"
#include "a2gspu_fault.hpp"
#include "a2gspu_input.hpp"
#include "a2gspu_agent.hpp"


// ---- a2gspu interactive control rail (env-gated): A2GSPU_CTRL=<dir> ----
// A closed-loop stepping protocol for agent-driven sessions (pascal-toolchain
// UC-2): the caller drops <dir>/cmd.<seq> files (seq = 1,2,3,...), each one
// command; we execute it and write <dir>/ack.<seq>. Commands:
//   run <frames>            advance emulation N frames (headless, flat out)
//   keys <string>           append to the keyboard paste buffer (rest of line
//                           verbatim; \n arrives as Return via the paste path)
//   text <file>             dump text page 1 main+aux (2KB) — screen-text eyes
//   shot <file>             render + save a backbuffer BMP — graphics eyes
//   read <hex> <len> <file> dump RAM from the flat physical image (symbol-
//                           mapped game-state reads; IIe main 0x0-0xFFFF,
//                           aux 0x10000-0x1FFFF)
//   save <file>             checkpoint: CPU + full MMU snapshot + the machine's
//                           device blocks + sentinel + manifest
//   restore <file>          restore a checkpoint, transactionally (take/restore at
//                           quiescent points; the scanner and media are not in
//                           the file and self-heal over the next frames)
//   quit                    end the session
// Pacing: for one second after each ack the poll ticks every millisecond (an
// agent's next command usually follows within milliseconds), then at 20Hz
// (SDL_Delay(50)) so an idle session neither spins the CPU nor floods the
// filesystem; A2GSPU_CTRL_TIMEOUT seconds (default 300) of wall-clock silence
// auto-quits an orphaned session.
static void a2gspu_ctrl_dump_text(computer_t *computer, const char *path) {
    FILE *tfp = fopen(path, "wb");
    if (!tfp) { printf("A2GSPU CTRL: text open fail '%s'\n", path); return; }
    for (uint32_t a = 0x0400; a < 0x0800; a++) {
        uint8_t b = rail_mmu(computer)->probe_peek(a);
        fwrite(&b, 1, 1, tfp);
    }
    // The 80-column aux half. The old fallback read FPI bank $01, which on a
    // IIgs is not the memory the display is generated from -- that is Mega II
    // bank $E1, which rail_video_base resolves.
    const uint8_t *mem = rail_video_base(computer);
    for (uint32_t a = 0x0400; a < 0x0800; a++) {
        uint8_t b = mem ? mem[0x10000 + a] : rail_mmu(computer)->probe_peek(0x010000u | a);
        fwrite(&b, 1, 1, tfp);
    }
    fclose(tfp);
}

// Atomic ack: write ack.N.tmp then rename, so a polling reader never sees a
// half-written (or empty) ack file.
static void a2gspu_ctrl_ack(const char *dir, int seq, const char *result) {
    char tmppath[1024], ackpath[1024];
    snprintf(tmppath, sizeof tmppath, "%s/ack.%d.tmp", dir, seq);
    snprintf(ackpath, sizeof ackpath, "%s/ack.%d", dir, seq);
    FILE *af = fopen(tmppath, "wb");
    if (!af) return;
    fprintf(af, "%s\n", result);
    fclose(af);
    remove(ackpath);
    rename(tmppath, ackpath);
}

// Soft-switch read-status helper for `vid`.  A free function rather than a
// lambda inside a2gspu_ctrl_loop(): that function is already very large, and at
// -O3 the extra inlining candidate was enough to make g++ fall over silently
// (no diagnostic, non-zero exit) while -O0/-O1/-O2 all compiled fine.
static inline int a2gspu_sw(MMU_II *m, uint16_t a) {
    return m ? ((m->probe_peek(a) & 0x80) ? 1 : 0) : -1;
}

// Addr parse: a2ctrl::parse_addr. Soft watch: a2ctrl::g_soft_watch / soft_watch_after.

// One machine-thread dispatcher for the file rail and socket agent tools.
// Transport framing and ownership policy stay with their respective callers.
static bool a2gspu_dispatch(GS2AppState *state, const char *line,
                           char *result, size_t rsz, int &ctrl_frame) {
    computer_t *computer=state->computer;
    // A2CTRL-DISPATCH-BEGIN -- shared registry parity covers this implementation.
    // ---- extracted families (a2gspu_ctrl_cmds.hpp) ----
    if (a2ctrl::try_meta(line, result, rsz)) {
        // oracle/help/manifest/rail
    } else if (a2ctrl::try_trace_break(line, result, rsz)) {
        // valtrap/itrace/tbtrace/callstream
    } else if (a2ctrl::try_assert_dhgr(line, result, rsz, computer)) {
        // assert/dhgr-*
    } else if (a2ctrl::try_watch(line, result, rsz, computer)) {
        // soft + bus watch
    } else if (a2ctrl::try_inject(line, result, rsz, computer)) {
        // poke / vram / load
    } else if (a2ctrl::try_visual(line, result, rsz, computer)) {
        // png / pngc
    } else if (a2ctrl::try_exec(line, result, rsz, computer, &ctrl_frame)) {
        // run / step / run-until (+ soft watch)
    } else if (a2ctrl::try_input(line, result, rsz, computer)) {
        // keys / press / key / holdkey
    } else if (a2ctrl::try_cpu(line, result, rsz, computer)) {
        // cpu snapshot
    } else if (a2ctrl::try_regs(line, result, rsz, computer)) {
        // read / setreg / cycles / bp / stack
    } else if (!strncmp(line, "iolog on", 8)) {
        // A2GSPU gap #6 — arm the general $C0xx access ring (read AND write,
        // every soft switch, ordered). The bare `iolog` below only ever gave
        // cumulative COUNTS for five keyboard switches, so which switch a
        // routine touched, in what order, had to be inferred from side
        // effects. Optional cap: "iolog on <n>".
        long cap = 0;
        if (sscanf(line + 8, "%ld", &cap) == 1 && cap > 0) g_io_trace_cap = (size_t)cap;
        io_trace_reset();
        g_io_trace_enabled = true;
        snprintf(result, rsz, "status=OK iolog on cap=%zu", g_io_trace_cap);
    } else if (!strncmp(line, "iolog off", 9)) {
        g_io_trace_enabled = false;
        snprintf(result, rsz, "status=OK iolog off seq=%u retained=%zu dropped=%u",
                 g_io_trace_seq, g_io_trace.size(), g_io_trace_dropped);
    } else if (!strncmp(line, "iolog dump ", 11)) {
        if (io_trace_dump(line + 11))
            snprintf(result, rsz, "status=OK iolog dumped seq=%u retained=%zu dropped=%u",
                     g_io_trace_seq, g_io_trace.size(), g_io_trace_dropped);
        else
            snprintf(result, rsz, "status=FAIL iolog-dump-fail");
    } else if (!strncmp(line, "iolog reset", 11)) {
        io_trace_reset();
        snprintf(result, rsz, "status=OK iolog reset");
    } else if (!strncmp(line, "iolog", 5) && line[5] == ' ' &&
               (!strcmp(line + 6, "dump") || !strncmp(line + 6, "dump ", 5))) {
        /* `iolog dump` with no filename. The subcommand above matches
           "iolog dump " WITH a trailing space and a name after it, so a bare
           `iolog dump` used to fall through to the catch-all below and
           answer status=OK with a keyboard-counter report -- reporting
           success at having written a file it never opened.

           That is the identical defect `cov` shipped with, and the one `dis`
           was already fixed for ("no filename -> FAIL, not a file named
           after the address"). Three verbs, one mistake, because nothing
           tested the argument-missing path of any of them. */
        snprintf(result, rsz,
                 "status=FAIL iolog-dump-needs-file -- usage: iolog dump <file>");
    } else if (!strncmp(line, "iolog", 5) && line[5] == ' ' &&
               strcmp(line + 6, "status") != 0) {
        /* Anything else after `iolog` is not a subcommand this verb has.
           It used to reach the catch-all and answer status=OK, so
           `iolog zzz` reported keyboard counters and looked like it had
           worked -- the same accept-nonsense-and-succeed shape as `cov`'s
           advertised-but-absent grammar. */
        snprintf(result, rsz,
                 "status=FAIL iolog-need: [status]|on [cap]|off|dump <file>|reset");
    } else if (!strncmp(line, "iolog", 5)) {
        // Cumulative keyboard soft-switch read counts. Diff two 'iolog' calls
        // across a 'run' to see which switch a wedged menu actually polls
        // (C000 latch / C010 strobe+AKD / C025 mods / C026 ADB data reg).
        uint64_t c[5];
        a2gspu_keygloo_read_counts(c);
        // These counters come from the IIgs ADB/KeyGloo path.  On a II-family
        // machine the keyboard does not go through KeyGloo, so they are ALWAYS
        // zero -- which reads as "nothing polled the keyboard" when the truth is
        // "this counter does not apply here".  Say so, and point at the rail that
        // does work, rather than reporting a confident zero.
        bool gs = (state->mmu_iigs != nullptr);
        snprintf(result, rsz,
            "status=OK iolog KBDreads C000=%llu C010=%llu C024=%llu C025=%llu C026=%llu%s",
            (unsigned long long)c[0], (unsigned long long)c[1], (unsigned long long)c[2],
            (unsigned long long)c[3], (unsigned long long)c[4],
            gs ? "" : "  [KeyGloo counters are IIgs-only and read 0 on this "
                      "machine; use 'iolog on' + 'iolog dump' for the real $C0xx ring]");
    } else if (!strncmp(line, "reset", 5)) {
        // reset [cold] — pull RESET (warm by default, cold with the arg).
        // Without this the rail cannot recover a wedged machine: an Apple
        // IIe autoboots slot 6 and, with no 5.25" disk present, spins in
        // the Disk II boot ROM ($C65E/$C661, X=60) forever without ever
        // reading the keyboard. Injected keys go nowhere, so `keys PR#5`
        // cannot reach a ProDOS block device in another slot. A reset
        // drops the machine to BASIC where PR#<slot> works.
        bool cold = (strstr(line, "cold") != NULL);
        computer->reset(cold);
        cpu_state *c = computer->cpu;
        snprintf(result, rsz, "status=OK reset %s PC=%02X:%04X",
            cold ? "cold" : "warm",
            (unsigned)((c->full_pc >> 16) & 0xFF),
            (unsigned)(c->full_pc & 0xFFFF));
    } else if (!strncmp(line, "boot ", 5)) {
        // boot <slot> — enter a slot's firmware directly at $Cs00, the
        // way the ROM's autoboot would. Needed because that autoboot is
        // not steerable: an Apple IIe finds the Disk II in slot 6 first
        // and, with no 5.25" media, spins in its boot ROM forever
        // ($C65E/$C661, X=60) without ever polling the keyboard, so
        // neither injected keys nor `PR#<slot>` can reach a ProDOS block
        // device in a lower slot. This is how you boot an 800K .po image
        // mounted on the SmartPort/pdblock card.
        int slot = atoi(line + 5);
        if (slot < 1 || slot > 7) {
            snprintf(result, rsz, "status=FAIL boot-bad-slot %d", slot);
        } else {
            cpu_state *c = computer->cpu;
            c->pc = (uint16_t)(0xC000 + slot * 0x100);
            c->full_pc = (uint32_t)c->pc;      /* bank 0 */
            snprintf(result, rsz, "status=OK boot slot=%d PC=00:%04X",
                     slot, (unsigned)c->pc);
        }
    } else if (!strncmp(line, "dis ", 4)) {
        // dis <hexaddr> <count> <file> — disassemble via the debugger's
        // Disassembler, which reads through the MMU (bank/langcard-correct,
        // unlike the flat 'read'). Writes N instruction lines to <file>.
        // BOTH CONVERSIONS, OR NEITHER. This accepted `>= 1`, so `dis 0400`
        // -- no count, no filename -- parsed the address, failed the %d, and
        // then left %n UNASSIGNED. off kept its initialiser of 0, so the
        // filename was taken from the start of the argument and the command
        // cheerfully created a file called "0400" and acked status=OK.
        //
        // A verb that invents a filename out of an address it was asked to
        // disassemble is not a parse failure the caller can see. Require the
        // two numbers, and require something left over to be the path.
        //
        // THE ADDRESS IS PARSED THE WAY EVERY OTHER VERB PARSES ONE.
        // This used a bare "%x", so `dis 7E:0000 ...` -- the form load,
        // read and poke all accept -- answered dis-parse-fail. It goes
        // through a2ctrl::parse_addr now; the both-conversions-or-neither
        // guard above is kept by requiring three fields AND a parsable
        // address, so `dis 0400` still fails instead of inventing a file.
        //
        // GRAMMAR: dis <addr> <n> <file> [m8|m16] [x8|x16]
        // The widths default to the LIVE CPU's M/X/E, because a 65816
        // listing decoded at the wrong immediate width desynchronises from
        // the first 16-bit immediate and invents everything after it. The
        // override exists for code that has not run yet, where the only
        // authority on the mode is the source Rosetta assembled.
        char atok[64] = {0}, ftok[512] = {0}, w1[16] = {0}, w2[16] = {0};
        uint32_t addr = 0; int n = 20;
        const int nf = sscanf(line + 4, "%63s %d %511s %15s %15s", atok, &n, ftok, w1, w2);
        if (nf >= 3 && ftok[0] && a2ctrl::parse_addr(atok, &addr)) {
            if (n <= 0 || n > 512) n = 20;
            cpu_state *dc = computer->cpu;
            const bool nat816 = (dc->cpu_type == PROCESSOR_65816) && ((dc->E & 1) == 0);
            bool dm8 = !nat816 || (dc->_M & 1);
            bool dx8 = !nat816 || (dc->_X & 1);
            const char *wsrc = "cpu";
            for (const char *w : { (const char *)w1, (const char *)w2 }) {
                if (!w[0]) continue;
                if      (!strcmp(w, "m8"))  { dm8 = true;  wsrc = "arg"; }
                else if (!strcmp(w, "m16")) { dm8 = false; wsrc = "arg"; }
                else if (!strcmp(w, "x8"))  { dx8 = true;  wsrc = "arg"; }
                else if (!strcmp(w, "x16")) { dx8 = false; wsrc = "arg"; }
            }
            Disassembler dis(rail_mmu(computer), dc->cpu_type);
            dis.setMX(dm8, dx8);
            dis.setAddress(addr);
            std::vector<std::string> lines = dis.disassemble(n);
            FILE *df = fopen(ftok, "wb");
            if (df) {
                // THE LISTING SAYS WHAT IT ASSUMED. Entry widths are an
                // assumption about a static byte stream, not a measurement
                // of it, and a listing that does not say so reads as fact.
                fprintf(df, "; dis $%02X/%04X n=%d entry widths m=%d x=%d (source=%s); "
                            "REP/SEP followed as decoded, XCE assumed 8-bit\n",
                        (unsigned)((addr >> 16) & 0xFF), (unsigned)(addr & 0xFFFF),
                        n, dm8 ? 8 : 16, dx8 ? 8 : 16, wsrc);
                for (auto &l : lines) fprintf(df, "%s\n", l.c_str());
                fclose(df);
                snprintf(result, rsz,
                         "status=OK dis n=%d mx=m%dx%d@start src=%s unreadable=%d",
                         n, dm8 ? 8 : 16, dx8 ? 8 : 16, wsrc, dis.unreadable());
            } else {
                snprintf(result, rsz, "status=FAIL dis-fail");
            }
        } else {
            snprintf(result, rsz, "status=FAIL dis-parse-fail");
        }
    } else if (!strncmp(line, "text ", 5)) {
        a2gspu_ctrl_dump_text(computer, line + 5);
        snprintf(result, rsz, "status=OK text %s", line + 5);
    } else if (!strncmp(line, "shot ", 5)) {
        video_system_t *vs = computer->video_system;
        vs->update_display(true);
        vs->save_screenshot(line + 5);
        snprintf(result, rsz, "status=OK shot profile=sdl-human %s", line + 5);
    } else if (!strncmp(line, "mount ", 6)) {
        // mount sXdY <path> — swap media at runtime (the 1981 flippy dance,
        // agent edition: multi-disk originals prompt for disk swaps mid-run).
        // Saves-and-unmounts anything in the drive first so writes persist.
        int slot = 0, drive = 0, off = 0;
        if (sscanf(line + 6, "s%dd%d %n", &slot, &drive, &off) == 2 && line[6 + off]) {
            const char *path = line + 6 + off;

            /* THE PATH IS CHECKED FIRST, AND THAT IS THE WHOLE FIX.
               mount_media() returns true for a file that does not exist, so
               this answered status=OK with no media in the drive. The guest
               then has no block device, $CsFF reads whatever firmware is
               there, and a driver call through it runs away into the I/O
               page -- a failure three subsystems away from the lie that
               caused it. It cost three rounds of diagnosis on a resource
               backend that turned out to be correct all along.

               Checking BEFORE the unmount matters just as much: the
               unmount below is unconditional, so a mistyped path used to
               eject the working disk and then report success at having
               done so. Validate, then disturb. */
            FILE *probe = fopen(path, "rb");
            if (!probe) {
                snprintf(result, rsz,
                         "status=FAIL mount-no-such-file '%s' -- nothing was unmounted",
                         path);
            } else {
                fclose(probe);
                storage_key_t key;
                key.slot = (uint16_t)slot;
                key.drive = (uint16_t)(drive - 1);
                key.partition = 0;
                key.subunit = 0;
                computer->mounts->unmount_media(key, SAVE_AND_UNMOUNT);  // best-effort
                disk_mount_t dm{ (uint16_t)slot, (uint16_t)(drive - 1),
                                 std::string(path) };
                if (!computer->mounts->mount_media(dm))
                    snprintf(result, rsz, "status=FAIL mount-fail '%s'", path);
                else
                    snprintf(result, rsz, "status=OK mount s%dd%d", slot, drive);
            }
        } else {
            snprintf(result, rsz, "status=FAIL mount-parse-fail");
        }
    } else if (!strncmp(line, "hgr", 3)) {
        // hgr [1|2] [<file>] -- HGR page as a 40x24 ASCII density map.
        // `shot` writes a BMP the agent cannot read back, and VIDEOMAP is
        // SHR-only, so on a II-family machine an agent had NO readable view of
        // the graphics screen at all.  Honors HGR's interleaved line layout:
        //   addr = base + (y&7)*$400 + ((y>>3)&7)*$80 + (y>>6)*$28
        // Bit 7 is the palette select, not a pixel, so it is masked out.
        int page = 1; char fbuf[512]; fbuf[0] = 0;
        sscanf(line + 3, " %d %511s", &page, fbuf);
        if (page != 2) page = 1;
        uint32_t base = (page == 2) ? 0x4000 : 0x2000;
        static const char *ramp = " .:-=+*#%@";
        char map[24][41];
        int lit_total = 0;
        for (int cy = 0; cy < 24; cy++) {
            for (int cx = 0; cx < 40; cx++) {
                int bits = 0;
                for (int sy = 0; sy < 8; sy++) {
                    int y = cy * 8 + sy;
                    uint32_t a = base + (uint32_t)((y & 7) << 10)
                                      + (uint32_t)(((y >> 3) & 7) << 7)
                                      + (uint32_t)((y >> 6) * 0x28) + cx;
                    uint8_t b = rail_mmu(computer)->probe_peek(a) & 0x7F;
                    for (int k = 0; k < 7; k++) if (b & (1 << k)) bits++;
                }
                lit_total += bits;
                int lvl = (bits * 9) / 56;           // 0..56 set pixels -> 0..9
                map[cy][cx] = ramp[lvl > 9 ? 9 : lvl];
            }
            map[cy][40] = 0;
        }
        // Always report the lit-pixel total.  Without it a blank screen and a
        // failed read are indistinguishable -- both print 24 rows of spaces,
        // and an agent cannot tell "nothing drawn yet" from "my probe broke".
        printf("HGR PAGE %d ($%04X): %d lit pixel(s) of 53760\n",
               page, base, lit_total);
        if (fbuf[0]) {
            FILE *hf = fopen(fbuf, "wb");
            if (hf) {
                for (int cy = 0; cy < 24; cy++) fprintf(hf, "%s\n", map[cy]);
                fclose(hf);
                snprintf(result, rsz, "status=OK hgr profile=hgr-ascii-coarse page%d -> %s", page, fbuf);
            } else {
                snprintf(result, rsz, "status=FAIL hgr-write-fail");
            }
        } else {
            // No file: stream the map straight into the ack so a one-shot
            // "what is on screen right now" needs no second round trip.
            printf("HGR PAGE %d ($%04X) 40x24 density map:\n", page, base);
            for (int cy = 0; cy < 24; cy++) printf("  |%s|\n", map[cy]);
            snprintf(result, rsz, "status=OK hgr profile=hgr-ascii-coarse page%d dumped to stdout", page);
        }
    } else if (!strncmp(line, "screen", 6)) {
        // screen [<file>] -- ONE mode-aware "what is on the display" verb.
        //
        // Rationale: choosing the decoder by hand is the single most repeated
        // error in agent-driven sessions, and it fails SILENTLY.  Observed in
        // one Ultima V run: (a) captured HGR while the machine was in TEXT mode
        // and got lit=23299 of pure nonsense; (b) read the text page while the
        // machine was in HIRES and saw a stale "Apple //e" banner that was no
        // longer displayed, which reads as "the boot hung"; (c) captured HGR
        // page 1 (blank) while the program was drawing to page 2 (lit=26880).
        // Each mistake produced a confident, wrong conclusion.
        //
        // So: read VideoScannerII, pick the decoder AND the page, and always
        // report BOTH HGR pages' lit counts so double-buffering is visible
        // rather than something you have to already suspect.
        display_state_t *dss = (display_state_t *)computer->cached_display_state;
        VideoScannerII *vs = dss ? dss->video_scanner : nullptr;
        if (!vs) { snprintf(result, rsz, "status=FAIL screen-no-scanner"); }
        else {
            bool txt = vs->is_text(), mix = vs->is_mixed();
            bool hir = vs->is_hires(), p2 = vs->is_page_2();
            char fbuf[512]; fbuf[0] = 0;
            sscanf(line + 6, " %511s", fbuf);

            // Both HGR pages, always -- cheap, and it catches double-buffering.
            long litp[2] = {0, 0};
            for (int pg = 0; pg < 2; pg++) {
                uint32_t b = pg ? 0x4000 : 0x2000;
                for (int y = 0; y < 192; y++)
                    for (int cx = 0; cx < 40; cx++) {
                        uint8_t v = rail_mmu(computer)->probe_peek(
                                        a2png::hgr_addr(b, y, cx)) & 0x7F;
                        for (int k = 0; k < 7; k++) if (v & (1 << k)) litp[pg]++;
                    }
            }
            // Text page (respecting the display page), decoded to ASCII.
            //
            // Two counts, deliberately NOT collapsed into one:
            //   nonblank  -- any row with a non-space cell
            //   textlike  -- rows that plausibly contain REAL text
            // Counting only "nonblank" reported text_rows=6 on a screen with no
            // text at all, because HGR-era code leaves graphics bytes lying in
            // $0400-$07FF and 74% of byte values mask to a printable character.
            // A single number there is worse than none: it says "there is a
            // prompt" when there is not, and the loop then waits for input that
            // will never be asked for.
            //
            // Discriminator: real UI text is overwhelmingly letters, digits,
            // spaces and a little punctuation, and arrives in runs. Random bytes
            // masked to 7 bits are spread across the whole symbol range. So
            // require both a >=60% share of text-ish characters among the
            // non-space cells AND a run of >=4 consecutive ones.
            uint32_t tb = p2 ? 0x0800 : 0x0400;
            int tnb = 0, ttext = 0;
            char rows[24][41];
            bool rowtext[24] = {false};
            for (int y = 0; y < 24; y++) {
                uint32_t a = tb + (uint32_t)((y % 8) << 7) + (uint32_t)((y / 8) * 0x28);
                for (int x = 0; x < 40; x++) {
                    uint8_t ch = rail_mmu(computer)->probe_peek(a + x) & 0x7F;
                    rows[y][x] = (ch >= 32 && ch < 127) ? (char)ch : ' ';
                }
                rows[y][40] = 0;
                int nonspace = 0, texty = 0, run = 0, bestrun = 0;
                for (int x = 0; x < 40; x++) {
                    char c = rows[y][x];
                    bool t = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                             (c >= '0' && c <= '9') ||
                             c == ' ' || c == '.' || c == ',' || c == ':' ||
                             c == ';' || c == '!' || c == '?' || c == '\'' ||
                             c == '-' || c == '(' || c == ')' || c == '/';
                    if (c != ' ') nonspace++;
                    // A space BREAKS the run.  Letting it pass through was the
                    // whole defect: a row of graphics residue like
                    // "0 0 0 0 >>    >>0 0 0 0" is all digits-and-spaces, so it
                    // scored bestrun=8 and was reported as text.  What actually
                    // distinguishes prose from residue is WORDS -- a run of
                    // consecutive non-space characters. "Apple //e" gives 5;
                    // "0 0 0 0" gives 1.
                    if (t && c != ' ') { texty++; run++; if (run > bestrun) bestrun = run; }
                    else run = 0;
                }
                if (nonspace) tnb++;
                if (nonspace >= 3 && bestrun >= 4 && texty * 10 >= nonspace * 6) {
                    rowtext[y] = true; ttext++;
                }
            }
            // DHGR must be NAMED. Reporting a double hi-res screen as "HIRES"
            // is the same silent-wrong-decoder failure this verb exists to
            // prevent, one level down: the reader sees HIRES, trusts the
            // 280-wide PNG, and is looking at half of every tile -- a 28-dot
            // tile appears as two 7-dot fragments with gaps, which reads as a
            // broken blitter rather than as a renderer aimed at one bank.
            bool dbl = vs->is_dblres() && vs->is_80col();
            const char *mode = txt ? (mix ? "TEXT+MIXED" : "TEXT")
                                   : (hir ? (dbl ? (mix ? "DHGR+MIXED" : "DHGR")
                                                 : (mix ? "HIRES+MIXED" : "HIRES"))
                                          : (dbl ? "DLORES" : "LORES"));
            // Detail goes to a FILE, not stdout.  The ack file is the only
            // reliable reply channel: stdout is buffered, so printf detail can
            // sit unflushed for an arbitrary time (observed: the SCREEN line
            // never appeared in the redirected log at all, while the ack was
            // instant).  And `result` is only 128 bytes, so it cannot carry 24
            // rows.  A sidecar file is both reliable and unbounded.
            char sidecar[600];
            snprintf(sidecar, sizeof sidecar, "%s",
                     fbuf[0] ? fbuf : "screen.png");
            { size_t n = strlen(sidecar);
              if (n > 4 && !strcmp(sidecar + n - 4, ".png")) sidecar[n - 4] = 0;
              strncat(sidecar, ".txt", sizeof(sidecar) - strlen(sidecar) - 1); }
            if (FILE *sf = fopen(sidecar, "w")) {
                fprintf(sf, "mode=%s page=%d hgr1_lit=%ld hgr2_lit=%ld "
                            "text_rows=%d nonblank_rows=%d\n",
                        mode, p2 ? 2 : 1, litp[0], litp[1], ttext, tnb);
                // Emit ALL non-blank rows, tagging which read as real text, so a
                // reader can audit the classifier instead of trusting it.
                for (int y = 0; y < 24; y++) {
                    bool nb = false;
                    for (int x = 0; x < 40; x++) if (rows[y][x] != ' ') { nb = true; break; }
                    if (nb) fprintf(sf, "%s T%02d |%s|\n",
                                    rowtext[y] ? "TEXT" : "junk", y, rows[y]);
                }
                fclose(sf);
            }
            printf("SCREEN: mode=%s page=%d  hgr1_lit=%ld hgr2_lit=%ld "
                   "text_rows=%d nonblank_rows=%d -> %s\n",
                   mode, p2 ? 2 : 1, litp[0], litp[1], ttext, tnb, sidecar);
            fflush(stdout);
            // ALWAYS emit the PNG, even when the mode says TEXT.
            //
            // Gating this on the mode created a blind spot: after `restore` the
            // video scanner's mode is stale (snapshots deliberately exclude
            // device state and let it self-heal over the following frames), so a
            // restored in-game session reported mode=TEXT while the real display
            // was hi-res -- and `screen` then wrote no image at all, leaving no
            // way to see the screen precisely when the state was hardest to
            // re-reach. The PNG is cheap and the lit counts already say which
            // page carries content, so emit unconditionally and let the reader
            // decide. Being able to look must never depend on a flag being right.
            {
                if (!fbuf[0]) snprintf(fbuf, sizeof fbuf, "screen.png");
                uint32_t base = p2 ? 0x4000 : 0x2000;
                // Aux comes from the flat image at +$10000, the way
                // VideoScannerIIe fetches it. probe_peek cannot be used for it:
                // on the IIe it follows the live RAMRD switch rather than
                // taking a bank, so reaching aux through it would mean
                // disturbing guest state in order to observe it.
                const uint8_t *sflat = rail_video_base(computer);
                bool sdbl = dbl && sflat;
                const int W = sdbl ? 560 : 280, H = 192;
                const int bpc = sdbl ? 14 : 7;
                // A DHGR dot is half as wide, so 560x192 covers the same screen
                // as 280x192; square pixels would stretch it 2:1.
                const int sx_n = 2, sy_n = sdbl ? 4 : 2;
                std::vector<uint8_t> img((size_t)(W*sx_n) * (H*sy_n), 0);
                for (int y = 0; y < H; y++)
                    for (int cx = 0; cx < 40; cx++) {
                        uint32_t a = a2png::hgr_addr(base, y, cx);
                        uint8_t bm = sdbl ? sflat[a] : rail_mmu(computer)->probe_peek(a);
                        uint8_t ba = sdbl ? sflat[a + 0x10000] : 0;
                        for (int k = 0; k < bpc; k++) {
                            uint8_t src = sdbl ? (k < 7 ? ba : bm) : bm;
                            int bit = sdbl ? (k % 7) : k;
                            uint8_t v = (src & (1 << bit)) ? 255 : 0;
                            int px = cx * bpc + k;
                            for (int sy = 0; sy < sy_n; sy++)
                                for (int sx = 0; sx < sx_n; sx++)
                                    img[(size_t)(y*sy_n+sy)*(W*sx_n) + (px*sx_n+sx)] = v;
                        }
                    }
                a2png::write_gray(fbuf, img.data(), W*sx_n, H*sy_n);
            }
            snprintf(result, rsz,
                     "status=OK screen mode=%s page=%d hgr1_lit=%ld hgr2_lit=%ld "
                     "text_rows=%d nonblank_rows=%d png=%s",
                     mode, p2 ? 2 : 1, litp[0], litp[1], ttext, tnb, fbuf);
        }
    } else if (!strncmp(line, "cov", 3) && (line[3] == '\0' || line[3] == ' ')) {
        // cov | cov status   -- report the armed range and what it has marked
        // cov on <LO-HI>     -- arm (BANK:LO-HI also accepted, hex)
        // cov map <file>     -- bind source/segment plan, fresh overlay generation
        // cov off            -- disarm and release the bitmap
        // cov reset          -- clear the bits, keep the range
        // cov write <file>   -- dump the bitmap (legacy bare `cov <file>` still works)
        //
        // A2GSPU_COVERAGE_OUT is flushed at SPIKE end, which never executes in
        // a CTRL session -- so an interactive run could accumulate coverage and
        // then throw it away on quit.  Worse, env-only configuration means
        // coverage is always "everything since boot", when the interesting
        // question is almost always scoped: what does THIS menu / THIS combat /
        // THIS shop conversation touch?  `cov reset` + activity + `cov write`
        // answers that, and is what makes the emulator->disassembler edge
        // usable for archaeology rather than just a boot-time curiosity.
        const char *arg = line + 3;
        while (*arg == ' ') arg++;
        if (!*arg || !strcmp(arg, "status")) {
            uint32_t span = a2gspu_cov_span();
            snprintf(result, rsz, g_cov_on
                     ? "status=OK cov on $%06X-$%06X marked=%llu/%u (%.1f%%)"
                     : "status=OK cov off",
                     g_cov_lo, g_cov_hi, (unsigned long long)g_cov_marked, span,
                     span ? 100.0 * (double)g_cov_marked / (double)span : 0.0);
            if(!g_cov_maps.empty()) {
                size_t used=strlen(result);
                snprintf(result+used,rsz-used," format=A2COV2 maps=%zu run=%llu generation=%llu valid=%d %s",
                    g_cov_maps.size(),(unsigned long long)g_cov_run_id,
                    (unsigned long long)g_cov_generation,g_cov_map_error[0]?0:1,g_cov_map_error);
            }
        } else if (!strncmp(arg,"counts",6) && (arg[6]=='\0' || arg[6]==' ')) {
            const char *sub=arg+6; while(*sub==' ')++sub;
            if (!strcmp(sub,"on"))
                snprintf(result,rsz,"status=%s cov counts enabled=%d model=instruction-cycles",
                    a2gspu_cov_counts_on()?"OK":"FAIL",g_cov_on?1:0);
            else if (!strcmp(sub,"off")) {
                g_cov_counts_on=false; g_cov_counts.clear();
                snprintf(result,rsz,"status=OK cov counts off");
            } else if (!strncmp(sub,"write ",6)) {
                const char *path=sub+6; while(*path==' ')++path;
                snprintf(result,rsz,"status=%s cov counts format=a2profile-v1 pcs=%zu %s",
                    a2gspu_cov_counts_write(path)?"OK":"FAIL",g_cov_counts.size(),g_cov_map_error);
            } else snprintf(result,rsz,"status=%s cov counts enabled=%d pcs=%zu -- counts on|off|write <file>",
                *sub?"FAIL":"OK",g_cov_counts_on,g_cov_counts.size());
        } else if (!strncmp(arg,"map",3) && (arg[3]=='\0' || arg[3]==' ')) {
            const char *path=arg+3;while(*path==' ')path++;
            if(!*path) {
                a2gspu_cov_off();
                snprintf(result,rsz,"status=FAIL bad-args -- cov map needs a deasmiigs --coverage-plan file");
            } else if(a2gspu_cov_map_load(path)) {
                snprintf(result,rsz,"status=OK cov mapped format=A2COV2 maps=%zu run=%llu generation=%llu -- executed bytes verified against plan",
                    g_cov_maps.size(),(unsigned long long)g_cov_run_id,(unsigned long long)g_cov_generation);
            } else {
                snprintf(result,rsz,"status=FAIL cov-map-failed %s",g_cov_map_error);
            }
        } else if (!strncmp(arg, "on", 2) && (arg[2] == '\0' || arg[2] == ' ')) {
            const char *range = arg + 2;
            while (*range == ' ') range++;
            if (!*range) {
                snprintf(result, rsz, "status=FAIL bad-args -- "
                         "cov on needs a range. usage: cov on <LO-HI> or <BANK:LO-HI>, hex");
            } else if (a2gspu_cov_init(range)) {
                snprintf(result, rsz, "status=OK cov armed $%06X-$%06X",
                         g_cov_lo, g_cov_hi);
            } else {
                snprintf(result, rsz, "status=FAIL cov-arm-fail '%s' -- "
                         "want LO-HI or BANK:LO-HI in hex", range);
            }
        } else if (!strcmp(arg, "off")) {
            if (!g_cov_on) snprintf(result, rsz, "status=OK cov already off");
            else { a2gspu_cov_off(); snprintf(result, rsz, "status=OK cov off"); }
        } else if (!strcmp(arg, "reset")) {
            if (!g_cov_on) {
                snprintf(result, rsz, "status=FAIL cov-not-armed -- "
                         "nothing to reset; arm it with `cov on <LO-HI>`");
            } else {
                a2gspu_cov_reset();
                snprintf(result, rsz, "status=OK cov reset (range $%06X-$%06X)",
                         g_cov_lo, g_cov_hi);
            }
        } else {
            // `cov write <file>`, or the legacy bare `cov <file>`.
            const char *path = arg;
            if (!strncmp(arg, "write", 5) && (arg[5] == '\0' || arg[5] == ' ')) {
                path = arg + 5;
                while (*path == ' ') path++;
            }
            if (!*path) {
                snprintf(result, rsz, "status=FAIL bad-args -- "
                         "cov write needs a file. usage: cov write <file>");
            } else if (!g_cov_on) {
                // The defect this replaces: an unarmed `cov <file>` wrote nothing
                // and still answered status=OK, so an agent following the old
                // synopsis could "collect coverage" all session and hold no bytes.
                snprintf(result, rsz, "status=FAIL cov-not-armed -- "
                         "nothing written; arm it with `cov on <LO-HI>` first");
            } else if (a2gspu_cov_write(path)) {
                uint32_t span = a2gspu_cov_span();
                // format= and hash=, because `cov` writes TWO formats and this
                // ack named neither. A mapped run (any `cov map <plan>`, which
                // is the normal deasmiigs source-bound path) silently produces
                // A2COV2 instead of A2COV1; a consumer reading only this line
                // could not tell, and the one shared reader refused the file it
                // was handed. hash= is the same house-FNV the artifact carries,
                // so the reader can prove it read what was written (P4).
                snprintf(result, rsz, "status=OK cov wrote %s format=%s hash=%016llX (%llu/%u bytes = %.1f%%)",
                         path, g_cov_last_format, (unsigned long long)g_cov_last_hash,
                         (unsigned long long)g_cov_marked, span,
                         span ? 100.0 * (double)g_cov_marked / (double)span : 0.0);
            } else {
                snprintf(result, rsz, "status=FAIL cov-write-failed '%s' %s", path,g_cov_map_error);
            }
        }
    } else if (!strncmp(line, "diff ", 5)) {
        a2snapdiff::command(line+5,result,rsz);
    } else if (!strncmp(line, "save ", 5)) {
        // save <file> -- CPU regs + full MMU snapshot (RAM + page tables +
        // softswitch state) + the device blocks this machine has (the ADB
        // micro, the Ensoniq, the interrupt registers, the IWM and drive
        // heads, the block device and its RAM-disk overlay) + sentinel +
        // manifest. The scanner and the media are not saved; the manifest's
        // excluded= says so. Take it at a quiescent point.
        std::string snapshot_path;
        if(a2fault::busy(computer->cpu) || a2input::key_down)snprintf(result,rsz,"status=FAIL save-active-injector-state; clear faults/release key before saving (host ownership not serialized)");
        else if(!a2snapdiff::snapshot_path(line+5,snapshot_path))snprintf(result,rsz,"status=FAIL save-invalid-path");
        else if (a2gspu_ctrl_snap_save(snapshot_path.c_str(), computer, state))
            snprintf(result, rsz, "status=OK save %s", snapshot_path.c_str());
        else snprintf(result, rsz, "status=FAIL save-fail");
    } else if (!strncmp(line, "restore ", 8)) {
        /* RESTORE IS TRANSACTIONAL.
         *
         * It used to load the CPU registers, then the MMU, then the device
         * blocks, straight into the live machine, and answer FAIL at whichever
         * step refused -- after the earlier steps had already overwritten the
         * machine. A snapshot of the wrong geometry (a IIe file into a IIgs
         * session) replaced every register, failed the MMU's geometry check,
         * and left a machine that was neither the old one nor the file's while
         * the ack said only "restore-fail". A FAIL that has changed the
         * machine is a lie of the kind AGENTS.md rule 3 forbids.
         *
         * So the current machine is saved first, by the same code as `save`,
         * and any failed step restores it. The ack names the step that failed
         * and whether the rollback held. */
        std::string manifest;
        const char *stage = nullptr;
        std::string snapshot_path;
        const bool injector_busy=a2fault::busy(computer->cpu) || a2input::key_down;
        const bool valid_path=!injector_busy && a2snapdiff::snapshot_path(line+8,snapshot_path);
        FILE *probe = valid_path?fopen(snapshot_path.c_str(), "rb"):nullptr;
        if(!valid_path) {
            snprintf(result,rsz,"status=FAIL restore-fail stage=preflight rolled-back=n/a reason=%s; machine untouched",injector_busy?"active-injector-state; clear faults/release key":"invalid-path");
        } else if (!probe) {
            snprintf(result, rsz, "status=FAIL restore-fail stage=open rolled-back=n/a -- cannot open '%s'; the machine was not touched", line + 8);
        } else {
            fclose(probe);
            std::error_code ec;
            std::filesystem::path rb = std::filesystem::temp_directory_path(ec) /
                ("a2gspu-restore-rollback-" + std::to_string((long long)getpid()) + ".snap");
            const std::string rbs = rb.string();
            bool have_rb = !ec && a2gspu_ctrl_snap_save(rbs.c_str(), computer, state);
            if (!have_rb) {
                snprintf(result, rsz, "status=FAIL restore-fail stage=rollback-save rolled-back=n/a -- could not checkpoint the current machine first, so nothing was restored");
            } else if (a2gspu_ctrl_snap_restore(snapshot_path.c_str(), computer, state, manifest, &stage)) {
                computer->cpu->halt = 0;   // force-run after restore
                a2gspu_snap_print_manifest(manifest);
                a2gspu_snap_print_devices();
            /* THE ACK CARRIES THE MANIFEST ITSELF, not a present/absent flag.
             *
             * It used to say only manifest=present, and the doc sent the
             * caller to the session log for the rest. Two things were wrong
             * with that. The log is stdout, which was block-buffered, so the
             * manifest line did not exist anywhere a live consumer could read
             * it until the process exited (fixed in SDL_AppInit, but the
             * split channel was the deeper fault: P3 is one record, one
             * channel). And class=/included=/excluded=/frame=/seed=/build=/
             * exe= appear ONLY in the manifest line -- a consumer deciding
             * "can I window this workload from this snapshot, or must I pay a
             * cold boot" had no way to read the fields that answer it off the
             * rail at all. Appended at the end of the line so every existing
             * prefix match on status=/manifest=/devices=/skipped= still
             * works. */
                snprintf(result, rsz, "status=OK restore %s manifest=%s devices=%s skipped=%s%s%s", line + 8,
                         manifest.empty() ? "absent" : "present",
                         g_snap_dev_restored.empty() ? "-" : g_snap_dev_restored.c_str(),
                         g_snap_dev_skipped.empty() ? "-" : g_snap_dev_skipped.c_str(),
                         manifest.empty() ? "" : " | ", manifest.c_str());
            } else {
                std::string rb_manifest; const char *rb_stage = nullptr;
                bool rolled = a2gspu_ctrl_snap_restore(rbs.c_str(), computer, state, rb_manifest, &rb_stage);
                if (rolled)
                    snprintf(result, rsz, "status=FAIL restore-fail stage=%s rolled-back=yes -- '%s' did not restore; the machine is as it was before this command",
                             stage ? stage : "?", line + 8);
                else
                    snprintf(result, rsz, "status=FAIL restore-fail stage=%s rolled-back=FAILED(%s) -- the machine state is undefined: reset, or restore a snapshot known to fit",
                             stage ? stage : "?", rb_stage ? rb_stage : "?");
            }
            if (have_rb) std::filesystem::remove(rb, ec);
        }
    } else if (!strncmp(line, "vid", 3)) {
        // vid -- the live video mode, read from the VIDEO SCANNER's own state.
        //
        // This verb previously read the $C01x status switches with
        // a2gspu_sw() -> probe_peek(). probe_peek is the observation-free
        // read: it returns the RAW page-table byte, NOT the value a real
        // read of a soft-switch status register produces. So on a IIe every
        // $C01x came back $80 and `vid` reported ALL EIGHT flags as 1; on the
        // IIgs they came back 0 and it reported all eight as 0. The verb was
        // fabricating its answer on BOTH platforms -- worse than no telemetry,
        // because it looked authoritative.
        //
        // VideoScannerII is the authority the renderer itself uses, so this
        // now reports what is actually being scanned out.
        display_state_t *dsv = (display_state_t *)computer->cached_display_state;
        VideoScannerII *vs = dsv ? dsv->video_scanner : nullptr;
        if (vs) {
            snprintf(result, rsz,
                "status=OK vid TEXT=%d MIXED=%d PAGE2=%d HIRES=%d 80COL=%d 80STORE=%d "
                "ALTCHAR=%d DHIRES=%d SHR=%d",
                vs->is_text() ? 1 : 0, vs->is_mixed() ? 1 : 0,
                vs->is_page_2() ? 1 : 0, vs->is_hires() ? 1 : 0,
                vs->is_80col() ? 1 : 0, vs->is_80store() ? 1 : 0,
                vs->is_altchrset() ? 1 : 0, vs->is_dblres() ? 1 : 0,
                vs->is_shr() ? 1 : 0);
        } else {
            snprintf(result, rsz, "status=FAIL vid-no-scanner");
        }
    } else if (!strncmp(line, "quit", 4)) {
        snprintf(result, rsz, "status=OK quit");
        return true;
    } else {
        /* "unknown-cmd" WAS A LIE FOR MOST OF THE VERBS THAT REACHED HERE.
         *
         * The dispatch above matches `strncmp(line, "read ", 5)` -- with the
         * trailing space -- so a bare `read` matches nothing, falls through
         * the entire chain, and was told the command did not exist. Twenty
         * of the thirty-six argument-taking verbs behaved that way. For an
         * agent driving this rail that is the worst possible answer: it is
         * indistinguishable from a typo or a version skew, it carries no
         * syntax, and the reasonable conclusion to draw from it -- "this
         * emulator cannot do that" -- is false.
         *
         * The verb registry already holds a synopsis for every verb, for
         * `help` and `manifest`. So look the first word up: if it is real,
         * the problem is the arguments, and the synopsis is exactly what the
         * caller needs to fix it. Only a genuinely unrecognised word still
         * gets unknown-cmd, which then means what it says. */
        char first[32] = {0};
        for (size_t i = 0; i < sizeof first - 1 && line[i] &&
                           line[i] != ' ' && line[i] != '\t'; i++) {
            first[i] = line[i];
        }
        int nverbs = 0;
        const a2manifest::Verb *vtab = a2manifest::verbs(&nverbs);
        const char *syn = nullptr;
        for (int i = 0; i < nverbs; i++) {
            if (!strcmp(vtab[i].name, first)) { syn = vtab[i].synopsis; break; }
        }
        if (syn) {
            snprintf(result, rsz,
                     "status=FAIL bad-args -- %s IS a verb; its arguments are "
                     "wrong or missing. usage: %s", first, syn);
        } else {
            snprintf(result, rsz,
                     "status=FAIL unknown-cmd '%s' -- not a verb. `help` lists "
                     "all of them; `manifest <file>` writes the full contract.",
                     first);
        }
    }
    // A2CTRL-DISPATCH-END
    return false;
}

std::string a2gspu_agent_request(GS2AppState *state,bool discovery,const std::string &command,int &frame) {
    a2ctrljson::native_payload.clear();a2ctrljson::native_status.clear();a2ctrljson::surface="socket";
    char result[8192]={},json[16384]={};
    auto fail=[&](const char *why){snprintf(result,sizeof result,"status=FAIL %s",why);a2ctrljson::format(json,sizeof json,command.c_str(),result);return std::string(json);};
    computer_t *computer=state?state->computer:nullptr;
    if(!state || state->phase!=PHASE_EMULATION || !computer || !computer->cpu || !computer->clock)return fail("machine-not-ready; select/start a machine first");
    if(discovery)return "{\"schema\":\"a2agent-tools-v1\",\"surface\":\"socket\",\"control\":{\"requires_paused\":true,\"live_observation_exception\":\"context read queries (including snapshot host-file output) execute at machine-thread boundaries while running; on/off/mark require pause\",\"automatic_takeover\":false,\"default_request\":\"AGENT_COMMAND\",\"quit_request\":\"QUIT\",\"max_command_utf8_bytes\":2047,\"execution\":\"explicit run/step/key waits return paused; resume intentionally runs\",\"effects\":\"command-dependent; mixed verbs may arm instruments or write files; read the synopsis\"},\"capabilities\":"+a2gspu_agent_capabilities(computer)+"}";
    if(!a2agent::valid_command(command))return fail("invalid-command-framing");
    const std::string word=a2agent::word(command);int count=0;const auto *verbs=a2manifest::verbs(&count);bool found=false;
    for(int i=0;i<count;i++)if(word==verbs[i].name){found=true;break;}
    if(!found)return fail("unknown-tool; discover AGENT_TOOLS");
    if(word=="quit")return fail("use-QUIT-request; native shutdown acknowledges before closing the socket");
    if(word!="pause" && !a2agent::live_context(command) && (computer->execution_mode!=EXEC_PAUSED || computer->instructions_left))return fail("requires-paused-machine; use PAUSE or the explicit pause tool before agent tools");
    const bool executes=word=="run" || word=="step" || word=="run-until" || word=="key" || word=="press" || word=="holdkey";
    struct PauseOnExit {computer_t *computer;bool armed;~PauseOnExit(){if(armed){computer->execution_mode=EXEC_PAUSED;computer->instructions_left=0;}}} restore{computer,executes};
    if(executes)computer->execution_mode=EXEC_NORMAL;
    a2events::begin_command(command.c_str());
    try {
        snprintf(result,sizeof result,"status=OK");
        a2gspu_dispatch(state,command.c_str(),result,sizeof result,frame);
    } catch(const std::exception &error) {
        a2ctrljson::native_payload.clear();a2ctrljson::native_status.clear();
        snprintf(result,sizeof result,"status=FAIL tool-exception %s",error.what());
    }
    fflush(stdout);a2gspu_device_events(computer);a2events::completed(command.c_str(),result);
    a2ctrljson::format(json,sizeof json,command.c_str(),result);return json;
}

void a2gspu_ctrl_loop(GS2AppState *state) {
    computer_t *computer = state->computer;
    const char *dir = SDL_getenv("A2GSPU_CTRL");
    int idle_timeout_s = 300;
    if (const char *tmo = SDL_getenv("A2GSPU_CTRL_TIMEOUT")) idle_timeout_s = SDL_atoi(tmo);
    computer->execution_mode = EXEC_NORMAL;
    printf("A2GSPU CTRL: interactive rail on '%s' (idle timeout %ds)\n", dir, idle_timeout_s);
    printf("A2GSPU CTRL: agentic oracle — send 'oracle' or 'help' / 'manifest <file>' "
           "(no black boxes; Docs/AGENTIC_ORACLE.md)\n");
    int seq = 1;
    // COMMAND LATENCY. The poll slept 50 ms per miss and every command paid a
    // 20 ms re-read guard, so the emulator side alone cost 20-70 ms per command
    // (measured medians 41-63 ms) on top of whatever the client added -- about
    // nine commands a second for an agent that must go one at a time. Now: a hot
    // window after each ack, and no guard for a client that declares it writes
    // cmd.N atomically (A2GSPU_CTRL_ATOMIC=1; session.ps1 sets it). Waiting never
    // advances the machine, so none of this can move a cycle count.
    const bool atomic_client = SDL_getenv("A2GSPU_CTRL_ATOMIC") != nullptr;
    uint64_t last_ack_ms = SDL_GetTicks();
    uint64_t idle_since_ms = SDL_GetTicks();
    // Harvested (wiz5): a persistent frame clock for the interactive rail so the
    // frame-armed instruments work here too — WATCH `ts`, ITRACE_FRAME, and the
    // loadtrace frame attribution all key off g_iigs_cur_frame, which was only
    // advanced by the headless spike loop. Ticking it per `run` frame joins CTRL
    // reads/writes to the same frame timeline the spike uses. Additive: with no
    // A2GSPU_* frame-instrument armed, iigs_itrace_frame_tick just sets a counter.
    int ctrl_frame = 0;
    char cmdpath[1024];
    // Large ack buffer: help/oracle contracts must fit; bare "ok" is a black box.
    char result[8192];
    for (;;) {
        snprintf(cmdpath, sizeof cmdpath, "%s/cmd.%d", dir, seq);
        FILE *f = fopen(cmdpath, "rb");
        if (!f) {
            const uint64_t now_ms = SDL_GetTicks();
            SDL_Delay(now_ms - last_ack_ms < 1000 ? 1 : 50);   // hot after an ack, else 20Hz
            if (SDL_GetTicks() - idle_since_ms >= (uint64_t)idle_timeout_s * 1000) {
                printf("A2GSPU CTRL: idle timeout — quitting orphaned session\n");
                break;
            }
            continue;
        }
        char line[2048] = {0};
        size_t n = fread(line, 1, sizeof line - 1, f);
        fclose(f);

        // A COMMAND FILE IS NOT GUARANTEED TO APPEAR ATOMICALLY.
        //
        // Acks have been written tmp-then-rename since a polling reader was
        // caught seeing a half-written one. The command direction had no such
        // protection: a client that writes cmd.N in place can be observed
        // mid-write, and the short read then matches no handler and gets
        // "unknown-cmd" for a command that is perfectly valid. Observed with
        // `read <addr> <len> <long path>` -- the longest command an agent
        // harness issues, and the most likely to straddle a write boundary.
        // Re-issuing it verbatim succeeded, which is a race, not a syntax error.
        //
        // A well-behaved client renames into place and never trips this. This
        // is here for the ones that do not: read again after a tick and discard
        // the first result if the file changed underneath. Costs 20ms per
        // command and cannot mask a genuine error, because a command that is
        // still growing was never a command yet.
        //
        // A client that renames into place (A2GSPU_CTRL_ATOMIC=1) cannot be seen
        // mid-write, so it does not pay for the ones that can.
        FILE *f2 = nullptr;
        if (!atomic_client) {
            SDL_Delay(20);
            f2 = fopen(cmdpath, "rb");
        }
        if (f2) {
            char line2[2048] = {0};
            size_t n2 = fread(line2, 1, sizeof line2 - 1, f2);
            fclose(f2);
            if (n2 != n || (n && memcmp(line, line2, n) != 0)) {
                continue;               // still being written -- look again
            }
        }
        // Accept LF and Windows CRLF; CR-before-LF stripping left a CR on
        // the final argument of ordinary Windows text files.
        while (n > 0 && (line[n-1] == '\r' || line[n-1] == '\n')) line[--n] = 0;
        // A2CTRL-DISPATCH-BEGIN -- railparity.ps1 reads CTRL command words (strncmp/strcmp
        // against `line`) only between these markers, in whatever file they sit. Keep every
        // verb the CTRL rail answers inside a marked region, and nothing else (the co-pilot's
        // allow-list is deliberately outside).
        bool json_reply = !strncmp(line, "json ", 5) && line[5];
        a2ctrljson::native_payload.clear();
        a2ctrljson::native_status.clear();
        a2ctrljson::surface="ctrl";
        if (json_reply) {
            memmove(line, line + 5, strlen(line + 5) + 1);
            n = strlen(line);
        }
        snprintf(result, sizeof result, "status=OK");
        a2events::begin_command(line);
        const bool requested_quit=a2gspu_dispatch(state,line,result,sizeof result,ctrl_frame);
        if(requested_quit) {
            if(json_reply){char json[16384];a2ctrljson::format(json,sizeof json,line,result);a2gspu_ctrl_ack(dir,seq,json);}
            else a2gspu_ctrl_ack(dir,seq,result);
            printf("A2GSPU CTRL: session ended after %d command(s)\n",seq);
            return;
        }
        // A2CTRL-DISPATCH-END
        /* ORDERING, not just flushing. Whatever this verb printed to stdout --
         * the snapshot manifest, the device list, a SYMS walk -- must be on disk
         * BEFORE the ack that announces it, or a harness that reads ack.N and
         * then scrapes session.log races the buffer and reads the log as it was
         * one command ago. setvbuf(_IONBF) in SDL_AppInit already makes this
         * true; this is here so it STAYS true if someone ever restores a buffer
         * for throughput. Belt and braces on the one boundary that matters. */
        fflush(stdout);
        a2gspu_device_events(computer);
        a2events::completed(line,result);
        if (json_reply) {
            char json[16384]; a2ctrljson::format(json,sizeof json,line,result);
            a2gspu_ctrl_ack(dir, seq, json);
        } else a2gspu_ctrl_ack(dir, seq, result);
        seq++;
        last_ack_ms = idle_since_ms = SDL_GetTicks();
    }
}

// Windowed co-pilot / agent UX rail. Unlike A2GSPU_CTRL this never takes over
// the frame loop and never runs on a worker thread: one command is polled after
// each normal frame, on the same SDL thread that owns the machine.
//
// Observe verbs (cpu/png/speaker/audio/pause-status/...) sample without
// disturbing guest state. Control verbs (pause/resume/reset/quit/press/mount/...)
// intentionally act as the user for agentic DUAL-03 closed-loop proofs.
// Instrumentation env (SPIKE/COVERAGE/CTRL takeover) stays independent and off.
// OBSERVATION CAN BE ARMED HERE TOO: `rail` (discovery, readback, and set/unset
// of any rail whose apply function is windowed-safe) and `trap` (records only).
// A windowed agent that needed a trap used to have to relaunch under CTRL and
// lose the human's session. Splices (poke/load/setreg) stay refused -- they race
// the human -- and so does any rail that halts, because a halt ends a windowed
// session.
void a2gspu_copilot_poll(computer_t *computer) {
    static const char *dir = SDL_getenv("A2GSPU_COPILOT");   // once: this runs every frame
    if (!dir || !*dir || !computer) return;
    static int seq = 1;
    char path[1024]; snprintf(path,sizeof path,"%s/cmd.%d",dir,seq);
    FILE *f=fopen(path,"rb"); if(!f) return;
    char line[2048]={0}, result[8192]={0};
    size_t n=fread(line,1,sizeof line-1,f); fclose(f);
    while(n && (line[n-1]=='\r'||line[n-1]=='\n')) line[--n]=0;
    const bool json_reply=!strncmp(line,"json ",5) && line[5];
    if(json_reply)memmove(line,line+5,strlen(line+5)+1);
    bool handled=false;
    a2ctrljson::native_payload.clear();
    a2ctrljson::native_status.clear();
    a2ctrljson::surface="copilot";

    // --- observe / discover ---
    a2events::begin_command(line);
    if (!strcmp(line,"protocol") || !strcmp(line,"systems") || !strncmp(line,"systems ",8) ||
        !strncmp(line,"manifest ",9) || !strcmp(line,"limitations") || !strncmp(line,"limitations ",12) ||
        !strcmp(line,"help") || !strncmp(line,"help ",5) ||
        !strcmp(line,"oracle") || !strcmp(line,"copilot"))
        handled=a2ctrl::try_meta(line,result,sizeof result);
    else if (!strncmp(line,"diff ",5)) {
        a2snapdiff::command(line+5,result,sizeof result);handled=true;
    }
    else if (!strcmp(line,"cpu") || !strncmp(line,"cpu ",4))
        handled=a2ctrl::try_cpu(line,result,sizeof result,computer);
    else if (!strncmp(line,"rail",4) && (line[4]==0 || line[4]==' ')) {
        const char *rest = line[4] ? line+5 : "";
        while (*rest==' ') rest++;
        if (!strncmp(rest,"set ",4) || !strncmp(rest,"unset ",6)) {
            const char *p = rest + (rest[0]=='u' ? 6 : 4);
            while (*p==' ') p++;
            char nm[64]={0}; size_t k=0;
            while (*p && *p!=' ' && k+1<sizeof nm) nm[k++]=*p++;
            const a2rails::SetterReg *st = a2rails::setter_find(nm);
            if (st && !st->windowed) {
                snprintf(result,sizeof result,
                    "status=FAIL copilot-rail-not-windowed %s -- it halts the machine, and a halt ends a windowed session; use the CTRL rail",
                    nm);
                handled=true;
            } else handled=a2ctrl::try_meta(line,result,sizeof result);
        } else handled=a2ctrl::try_meta(line,result,sizeof result);
    }
    else if (!strncmp(line,"trap",4) && (line[4]==0 || line[4]==' '))
        handled=a2ctrl::try_trace_break(line,result,sizeof result);
    else if (!strncmp(line,"context",7) && (!line[7] || line[7]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"inspect",7) && (!line[7] || line[7]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"ui",2) && (!line[2] || line[2]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"subscribe",9) && (!line[9] || line[9]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"capture",7) && (!line[7] || line[7]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"input",5) && (!line[5] || line[5]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"read ",5))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strncmp(line,"assert ",7))
        handled=a2ctrl::try_assert_dhgr(line,result,sizeof result,computer);
    else if (!strncmp(line,"png ",4)||!strncmp(line,"pngc ",5))
        handled=a2ctrl::try_visual(line,result,sizeof result,computer);
    else if (!strncmp(line,"audio",5) && (line[5]==0 || line[5]==' '))
        handled=a2ctrl::try_regs(line,result,sizeof result,computer);
    else if (!strcmp(line,"pause") || !strcmp(line,"pause-status") || !strcmp(line,"resume"))
        handled=a2ctrl::try_exec(line,result,sizeof result,computer,nullptr);
    else if (a2ctrl::try_input(line,result,sizeof result,computer))
        handled=true;
    else if (!strncmp(line,"reset",5) && (line[5]==0 || line[5]==' ')) {
        // Same path as MENU_MACHINE_RESET / MENU_MACHINE_RESTART.
        const bool cold = (line[5]==' ' && strstr(line+6,"cold"));
        computer->reset(cold);
        char st[192]; a2ctrl::format_cpu_line(computer, st, sizeof st);
        snprintf(result,sizeof result,"status=OK reset %s %s", cold?"cold":"warm", st);
        handled=true;
    } else if (!strcmp(line,"quit") || !strncmp(line,"quit ",5)) {
        // Same path as EVENT_QUIT / window chrome close under auto-launch.
        if (computer->cpu) computer->cpu->halt = HLT_USER;
        snprintf(result,sizeof result,"status=OK quit");
        handled=true;
    } else if (!strncmp(line,"mount ",6)) {
        // Same Mounts::mount_media path the File>Open Disk Image dialog uses
        // after SDL_ShowOpenFileDialog returns. Host OS file pickers are not
        // agent-closed-loop; this verb is the DUAL-03 disk-selection contract.
        int slot = 0, drive = 0, off = 0;
        if (sscanf(line + 6, "s%dd%d %n", &slot, &drive, &off) == 2 && line[6 + off]) {
            const char *mpath = line + 6 + off;
            FILE *probe = fopen(mpath, "rb");
            if (!probe) {
                snprintf(result,sizeof result,
                         "status=FAIL mount-no-such-file '%s' -- nothing was unmounted",
                         mpath);
            } else {
                fclose(probe);
                storage_key_t key;
                key.slot = (uint16_t)slot;
                key.drive = (uint16_t)(drive - 1);
                key.partition = 0;
                key.subunit = 0;
                computer->mounts->unmount_media(key, SAVE_AND_UNMOUNT);
                disk_mount_t dm{ (uint16_t)slot, (uint16_t)(drive - 1),
                                 std::string(mpath) };
                if (!computer->mounts->mount_media(dm))
                    snprintf(result,sizeof result,"status=FAIL mount-fail '%s'", mpath);
                else
                    snprintf(result,sizeof result,"status=OK mount s%dd%d", slot, drive);
            }
        } else {
            snprintf(result,sizeof result,"status=FAIL mount-parse-fail");
        }
        handled=true;
    }

    if(!handled) snprintf(result,sizeof result,
        "status=FAIL copilot-command-not-allowed use='copilot' "
        "(observe, windowed control, rail and trap only; no poke/load/setreg splices)");
    a2gspu_device_events(computer);
    a2events::completed(line,result);
    if(json_reply){char json[16384];a2ctrljson::format(json,sizeof json,line,result);a2gspu_ctrl_ack(dir,seq,json);}
    else a2gspu_ctrl_ack(dir,seq,result);
    seq++;
}
