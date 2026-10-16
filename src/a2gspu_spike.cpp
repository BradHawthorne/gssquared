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
// The headless spike -- moved out of gs2.cpp; see gs2_app.hpp.
#include "gs2_app.hpp"
#include "a2gspu_rail_setters.hpp"   // rails that are also armed live: one apply function each
#include "a2gspu_artifact_meta.hpp"   // M6: <artifact>.meta.json beside every raw artifact
#include "a2gspu_writeback.hpp"

// A2GSPU_OUT_DIR: parallel-safe output relocation. When set, the fixed-name
// headless/spike output files (spike_e1.bin, spike_e1_init.bin, spike_frame.bmp,
// spike_trace.bin, spike_slot.bin, spike_mmu_truth.bin, plus the A2GSPU_TEXTDUMP
// target) are written UNDER that directory, so N concurrent GSSquared invocations
// with distinct OUT_DIR never clobber each other's dumps. Fills `buf` and returns
// it; when OUT_DIR is UNSET returns `name` UNCHANGED (byte-identical to the legacy
// behavior — the dual-mode invariant). An absolute `name` (leading '/'/'\\' or a
// Windows drive-letter) is honored verbatim even when OUT_DIR is set, so an
// explicit caller-chosen path is never rewritten.
static const char *a2gspu_out_path(const char *name, char *buf, size_t bufsz) {
    const char *dir = SDL_getenv("A2GSPU_OUT_DIR");
    if (!dir || !*dir || !name) return name;
    bool absolute = name[0] == '/' || name[0] == '\\' ||
                    (name[0] != '\0' && name[1] == ':');   // Windows drive-letter (C:...)
    if (absolute) return name;
    snprintf(buf, bufsz, "%s/%s", dir, name);
    return buf;
}

void run_headless_spike(GS2AppState *state) {
    computer_t *computer = state->computer;
    char a2gspu_outbuf[1024];   // scratch for a2gspu_out_path() (A2GSPU_OUT_DIR)

    // a2gspu interactive control rail: when A2GSPU_CTRL is set, the stepping
    // protocol replaces the fixed-frame spike entirely (dumps are on-demand).
    // GAP-1: the CTRL loop is now entered LATER (just after the env-instrument
    // suite + symbols are armed, below) instead of here, so a live/co-pilot
    // session has full access to WATCH / VALTRAP / BREAK / POKE / ITRACE /
    // CALLTRACE / SNAP / CONDTRAP / STACKTRAP / STACKWATCH / MILESTONES /
    // RETGUARD + symbols. Those instruments hook run_one_frame's CPU loop, which
    // the CTRL rail drives, so once armed they fire during `run` commands. The
    // bus/obs oracle arming stays spike-only (its rings record every SHR write
    // and would grow unbounded across a long interactive session).
    const bool a2gspu_ctrl_mode = (SDL_getenv("A2GSPU_CTRL") != nullptr) || state->agent_socket_mode;

    // Env-gated CPU micro-test short-circuit: when A2GSPU_CPUTEST names a case,
    // run only that case and exit with its verdict (the normal frame spike, the
    // GS/OS round-trip, and the boot golden are all bypassed). Keeps the CPU
    // corner-case proof on the same headless exit-code rail as the boot gate.
    if (!a2gspu_ctrl_mode) if (const char *ct = SDL_getenv("A2GSPU_CPUTEST")) {
        int rc = run_cpu_microtest(state, ct);
        printf("=== CPUTEST COMPLETE (%s) ===\n", rc == 0 ? "PASS" : "FAIL");
        exit(rc);
    }

    // Env-gated MMU + VIDEO micro-test short-circuit (same rail as A2GSPU_CPUTEST):
    // exercises the FPI mapping + SHR decode contracts the render golden trusts,
    // then exits with the verdict. Bypasses the frame spike / boot golden entirely.
    if (!a2gspu_ctrl_mode) if (const char *mt = SDL_getenv("A2GSPU_MMUTEST")) {
        int rc = run_mmu_microtest(state, mt);
        printf("=== MMUTEST COMPLETE (%s) ===\n", rc == 0 ? "PASS" : "FAIL");
        exit(rc);
    }

    // A2GSPU_PACE: before PINS, which reports it. Headless defaults to fast, and a
    // fast run's audio is generated and probed but never played.
    a2gspu_pace_arm_headless();
    if (g_a2gspu_pace_fast && computer->audio_system) computer->audio_system->pause();

    if (!a2gspu_ctrl_mode) {
        printf("\n=== A2GSPU HEADLESS SPIKE: running %d frames ===\n", state->spike_frames);
        a2gspu_pins_print(state->platform_id, state->spike_frames, BUILD_VERSION);
    }

    computer->execution_mode = EXEC_NORMAL;

    // Bus/obs oracle arming is SPIKE-ONLY: its rings record every SHR write and
    // would grow unbounded across a long interactive CTRL session. The CTRL rail
    // still gets the full env-instrument suite (armed just below).
    if (!a2gspu_ctrl_mode) {
    bus_trace_reset();            // arm the bus-trace oracle
    g_bus_trace_enabled = true;
    slot_bus_reset();             // arm the faithful slot-bus model (the virtual slot)
    g_slot_bus_enabled = true;
    mmu_state_trace_reset();      // arm the ground-truth MMU-state stream (cycle-aligned with the slot bus)
    g_mmu_state_trace_enabled = true;
    obs_reset();                  // arm the Observatory spine + the NClock cost reclaim (the keystone proof)
    g_obs_enabled = true;
    g_obs_clock_cost_enabled = true;
    obs_register_iigs_core(computer->cpu);   // register cpu.*/clock.* LEVEL signals (the fault-context view)
    if (computer->irq_control)               // register the aggregate IRQ line as a LEVEL signal
        obs_add_scalar(OBS_SUB_IRQ, 0, 0, "irq.pending", OBS_T_U64,
                       computer->irq_control->obs_pending_ptr(), 0, 8, OBS_F_INTERNAL_ONLY);
    // VGC render-debug substrate (the BPWS-relevant hardware, audit-VERIFIED): the SHR
    // Scan-Control-Byte table ($E1:$9D00, one SCB/line, bit7=640, bit6=scanline-IRQ-en,
    // bits0-3=palette#) and the 16-palette RAM ($E1:$9E00, 16x16 entries of $0RGB). These
    // live in STABLE Mega II $E1 memory (not the transient VideoScannerIIgs), and their
    // writes are BUS_OBSERVABLE -> the card snoop can arbitrate them (task #60).
    //
    // A memory window is a host pointer, and since upstream 18f5d04c the Mega II keeps
    // $E1:$2000-$9FFF INTERLEAVED whenever the aux window is linearized (SHR on). The
    // SCB and palette tables are then not contiguous in the store, so pointers to
    // $19D00/$19E00 would sample pixel bytes. The window is the whole physical SHR
    // store instead, named for what it is; CPU-order SCB and palette bytes come from
    // MMU_IIgs::megaii_cpu_peek / megaii_cpu_view.
    if (uint8_t *m2 = state->mmu_iigs ? state->mmu_iigs->get_megaii_memory_base() : nullptr) {
        obs_add_memwindow(OBS_SUB_VGC, 0, "vgc.shr_store", m2 + 0x12000, 0x8000, OBS_F_BUS_OBSERVABLE);
    }
    }  // end if(!a2gspu_ctrl_mode): bus/obs oracle arming is spike-only

    // Arm the headless GS/OS app-bringup diagnostics (env-gated, stdout-only).
    // A2GSPU_TAP*: generic title-agnostic PC-hit / session tap (merged wiz5 tap).
    // No-op unless A2GSPU_TAP or A2GSPU_TAP_SESSION is set.
    a2record_register(computer->clock);
    generic_tap_arm();
    // A2GSPU_TRAP / A2GSPU_TRAP_FILE / A2GSPU_TRAP_OUT: configurable PC traps.
    // Idempotent; the run_one_frame lazy arm covers the CTRL and windowed paths.
    // A malformed spec is fatal here, before a single frame runs.
    a2gspu_trap_arm();
    // M9: count instructions and stores from here, for GSDIAG's run= (and any
    // A2GSPU_EXPECT_RUN gate). Before liveness, which shares the taps.
    a2gspu_progress_taps_arm(state->mmu_iigs != nullptr);
    g_a2gspu_cpu_for_names = computer->cpu;   // names by default check bank-0 ROM mapping live; setters ask its MMU the platform
    a2gspu_liveness_arm(state->mmu_iigs != nullptr);
    iigs_names_ready();         // at arm, so its one line prints whole and never inside a report
    g_iigs_tbtrace_enabled = (SDL_getenv("A2GSPU_TBTRACE") != nullptr);
    // RAILS THAT CAN ALSO BE ARMED LIVE. Each used to be an inline getenv block
    // here; each is now one apply function registered with the rail
    // (a2gspu_rail_setters.cpp), called below with the environment's value in the
    // order the blocks ran, and by `rail set` / `rail unset` on the CTRL rail.
    // One parser per rail. A value that cannot be honoured is still fatal here.
    a2gspu_rail_arm_env("A2GSPU_ERRHOOK");
    a2gspu_rail_arm_env("A2GSPU_BRKDUMP");
    a2gspu_rail_arm_env("A2GSPU_STOP_ON_FAULT");
    a2gspu_rail_arm_env("A2GSPU_BRKMEM");
    a2gspu_rail_arm_env("A2GSPU_LCTRACE");
    a2gspu_rail_arm_env("A2GSPU_HANG_THRESHOLD");
    // A2GSPU_WATCH="bank:lo-hi[,bank:lo-hi...]" (hex) — address-range write-watchpoint.
    if (const char *w = SDL_getenv("A2GSPU_WATCH")) {
        g_watch_count = 0;
        const char *p = w;
        int watch_warns = 0;
        while (*p && g_watch_count < 8) {
            const char *tok = p;
            uint32_t bank = (uint32_t)strtoul(p, (char**)&p, 16);
            bool saw_colon = (*p == ':');
            if (saw_colon) p++;
            uint32_t lo = (uint32_t)strtoul(p, (char**)&p, 16);
            bool saw_dash = (*p == '-');
            if (saw_dash) p++;
            uint32_t hi = (uint32_t)strtoul(p, (char**)&p, 16);
            // WATCH-format self-check (the "WATCH didn't fire" trap, made loud). A spec
            // wants bank:lo-hi. A BARE 24-bit address (e.g. E119A0, no ':') makes strtoul
            // eat the whole value as `bank`; the armed range becomes ($bank<<16) which no
            // real address ever hits -> the watch silently never fires and you wrongly
            // conclude the write never happened (a false graveyard). Warn to STDERR so the
            // golden STDOUT the CI gate parses is untouched.
            if (!saw_colon || bank > 0xFF) {
                fprintf(stderr, "A2GSPU_WATCH: ** FORMAT WARNING ** '%.*s' looks like a BARE "
                        "24-bit address (bank=$%X). Use bank:lo-hi (e.g. E1:19A0-19A3); as-is "
                        "it arms $%06X and will NEVER fire.\n",
                        (int)(p - tok), tok, (unsigned)bank,
                        (unsigned)(((bank << 16) | (lo & 0xFFFF)) & 0xFFFFFF));
                watch_warns++;
            } else if (saw_dash && lo > hi) {
                fprintf(stderr, "A2GSPU_WATCH: ** FORMAT WARNING ** '%.*s' has lo>hi ($%X>$%X) "
                        "-- inverted/empty range, will never fire.\n",
                        (int)(p - tok), tok, (unsigned)lo, (unsigned)hi);
                watch_warns++;
            }
            g_watch_ranges[g_watch_count].lo = (bank << 16) | (lo & 0xFFFF);
            g_watch_ranges[g_watch_count].hi = (bank << 16) | (hi & 0xFFFF);
            g_watch_count++;
            while (*p == ',' || *p == ' ') p++;
        }
        g_watch_on = (g_watch_count > 0);
        if (g_watch_on) {
            printf("A2GSPU_WATCH: %d range(s):", g_watch_count);
            for (int i = 0; i < g_watch_count; i++)
                printf(" %06X-%06X", g_watch_ranges[i].lo, g_watch_ranges[i].hi);
            printf("%s\n", watch_warns ? "  [see A2GSPU_WATCH FORMAT WARNINGS on stderr]" : "");
        }
    }
    // WATCH v2 (batch-2): cap / NDJSON file / change-only / also-watch-reads.
    a2gspu_rail_arm_env("A2GSPU_WATCH_MAX");
    g_watch_change_only = (SDL_getenv("A2GSPU_WATCH_CHANGE") != nullptr);
    g_watch_read_on     = (SDL_getenv("A2GSPU_WATCH_READ")   != nullptr);
    a2gspu_rail_arm_env("A2GSPU_WATCH_OUT");
    if (g_watch_on && (g_watch_max != 256 || g_watch_change_only || g_watch_read_on || g_watch_out))
        printf("A2GSPU_WATCH v2: max=%d change_only=%d read=%d out=%d\n",
               g_watch_max, g_watch_change_only ? 1 : 0, g_watch_read_on ? 1 : 0, g_watch_out ? 1 : 0);
    a2gspu_rail_arm_env("A2GSPU_VALTRAP_MAX");
    // A2GSPU_VALTRAP="<hexval>[:<width>]" — value-provenance store trap: bind the PC
    // that stores VALUE (e.g. a bogus pointer) into memory, LE across consecutive bytes.
    if (const char *vt = SDL_getenv("A2GSPU_VALTRAP")) {
        char *ep = nullptr;
        g_valtrap_val = (uint32_t)strtoul(vt, &ep, 16);
        if (ep && *ep == ':') g_valtrap_width = atoi(ep + 1);
        if (g_valtrap_width < 1) g_valtrap_width = 1;
        if (g_valtrap_width > 4) g_valtrap_width = 4;
        g_valtrap_on = true;
        printf("A2GSPU_VALTRAP: trap value $%X width=%d max=%d\n", g_valtrap_val, g_valtrap_width, g_valtrap_max);
    }
    a2gspu_rail_arm_env("A2GSPU_SYM_SUSPECT");
    // A2GSPU_CALLSTREAM=<file> — EXEC-DIFF: symbol-free NDJSON of the toolbox/GS-OS call
    // sequence for the ours-vs-pristine first-divergence differ (tools/gdiff/calldiff.py).
    if (const char *cs = SDL_getenv("A2GSPU_CALLSTREAM")) {
        g_callstream_out = fopen(cs, "wb");
        g_callstream_on  = (g_callstream_out != nullptr);
        printf("A2GSPU_CALLSTREAM: %s -> '%s'\n", g_callstream_on ? "NDJSON" : "OPEN-FAILED", cs);
        if (!g_callstream_on) { fprintf(stderr, "A2GSPU_CALLSTREAM: FAIL cannot open '%s' -- aborting (exit 2)\n", cs); exit(2); }
    }
    a2gspu_rail_arm_env("A2GSPU_LOADMAP");
    a2gspu_rail_arm_env("A2GSPU_CONDTRAP_MAX");
    a2gspu_rail_arm_env("A2GSPU_CONDTRAP");
    a2gspu_rail_arm_env("A2GSPU_LOADTRACE_MIN");
    a2gspu_rail_arm_env("A2GSPU_LOADTRACE");
    a2gspu_rail_arm_env("A2GSPU_PCTRAP");
    a2gspu_rail_arm_env("A2GSPU_TRAPDUMP");
    a2gspu_rail_arm_env("A2GSPU_STACKTRAP");
    a2gspu_rail_arm_env("A2GSPU_STACKWATCH_JUMP");
    a2gspu_rail_arm_env("A2GSPU_STACKWATCH");
    if (const char *bp = SDL_getenv("A2GSPU_BREAK")) {
        g_iigs_break_enabled = true;
        g_iigs_break_addr = (uint32_t)strtoul(bp, nullptr, 16) & 0xFFFFFF;
    }
    // ======== Batch-1 instruments (each default-OFF; a flag-off run is a no-op) ========
    // (1) A2GSPU_SAVE_AT=<hexPC>[@<file>]: snapshot + halt at an arbitrary breakpoint.
    if (const char *sa = SDL_getenv("A2GSPU_SAVE_AT")) {
        g_save_at_enabled = true; g_save_at_fired = false;
        g_save_at_addr = (uint32_t)strtoul(sa, nullptr, 16) & 0xFFFFFF;
        const char *at = strchr(sa, '@');
        if (at && at[1]) { strncpy(g_saveat_path, at + 1, sizeof(g_saveat_path) - 1); g_saveat_path[sizeof(g_saveat_path) - 1] = 0; }
        g_saveat_mmu = state->mmu_iigs; g_saveat_computer = computer;
        g_save_at_fn = a2gspu_save_at_cb;
        printf("A2GSPU SAVE_AT: snapshot+halt at first PC=$%06X -> '%s'\n", g_save_at_addr, g_saveat_path);
    }
    // MEMVU: store/load visibility, the instruction stream, the seam model and
    // blame. Each rail is one setter (a2gspu_rail_setters.cpp), applied here in the
    // order the old blocks ran, and live by `rail set`.
    a2gspu_rail_arm_env("MEMVU_STOREVIS");
    a2gspu_rail_arm_env("MEMVU_LOADVIS");
    a2gspu_rail_arm_env("MEMVU_OPMIX");
    a2gspu_rail_arm_env("MEMVU_IREUSE");
    a2gspu_rail_arm_env("MEMVU_WORKSET");
    a2gspu_rail_arm_env("MEMVU_SEAM");
    a2gspu_rail_arm_env("MEMVU_BLAME");
    a2gspu_rail_arm_env("MEMVU_STOREVIS_BANKS");

    // (2) A2GSPU_POKE="<hexPC>:<act>[;<act>...]": one-shot DELIBERATE state injection.
    //     acts: A/X/Y/S/D/P/DBR/PB=<hex> reg/flag; PC=<hex> force-branch;
    //     M<hex24>=<hexbyte> poke mem; RTS/RTL force-return; SKIP=<n> skip bytes.
    if (const char *pk = SDL_getenv("A2GSPU_POKE")) {
        const char *colon = strchr(pk, ':');
        if (colon) {
            g_poke_pc = (uint32_t)strtoul(pk, nullptr, 16) & 0xFFFFFF;
            g_poke_actions.clear();
            char buf[512]; strncpy(buf, colon + 1, sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0;
            for (char *tok = strtok(buf, ";"); tok; tok = strtok(nullptr, ";")) {
                while (*tok == ' ') tok++;
                IigsPokeAction a{0, 0, 0};
                if (tok[0] == 'M') {                                  // M<hex24>=<hexbyte>
                    char *eq = strchr(tok, '=');
                    if (eq) { a.kind = 'M'; a.addr = (uint32_t)strtoul(tok + 1, nullptr, 16) & 0xFFFFFF; a.val = (uint32_t)strtoul(eq + 1, nullptr, 16); g_poke_actions.push_back(a); }
                } else if (!strncmp(tok, "RTS", 3)) { a.kind = 'R'; g_poke_actions.push_back(a); }
                else if (!strncmp(tok, "RTL", 3))   { a.kind = 'L'; g_poke_actions.push_back(a); }
                else if (!strncmp(tok, "SKIP=", 5)) { a.kind = 'N'; a.val = (uint32_t)strtoul(tok + 5, nullptr, 10); g_poke_actions.push_back(a); }
                else {
                    char *eq = strchr(tok, '=');
                    if (eq) {
                        a.val = (uint32_t)strtoul(eq + 1, nullptr, 16);
                        if      (!strncmp(tok, "DBR", 3)) a.kind = 'B';
                        else if (!strncmp(tok, "PB", 2))  a.kind = 'K';
                        else if (!strncmp(tok, "PC", 2))  a.kind = 'J';
                        else switch (tok[0]) {
                            case 'A': a.kind = 'A'; break;  case 'X': a.kind = 'X'; break;
                            case 'Y': a.kind = 'Y'; break;  case 'S': a.kind = 'S'; break;
                            case 'D': a.kind = 'D'; break;  case 'P': a.kind = 'P'; break;
                            default:  a.kind = 0;   break;
                        }
                        if (a.kind) g_poke_actions.push_back(a);
                    }
                }
            }
            g_poke_on = !g_poke_actions.empty(); g_poke_fired = false; g_poke_hits = 0;
            g_poke_nth = 1;
            a2gspu_rail_arm_env("A2GSPU_POKE_NTH");
            printf("A2GSPU POKE: %zu action(s) armed at PC=$%06X hit #%d (DELIBERATE splice)\n",
                   g_poke_actions.size(), g_poke_pc, g_poke_nth);
        }
    }
    a2gspu_rail_arm_env("A2GSPU_INTLOG");
    a2gspu_rail_arm_env("A2GSPU_TRACE_EXT");
    a2gspu_rail_arm_env("A2GSPU_MODETRACE");
    if (const char *bk = SDL_getenv("A2GSPU_TBTRACE_BANK"))
        g_iigs_tbtrace_bank = (int)strtoul(bk, nullptr, 16);
    g_iigs_trace_from = 0; g_iigs_trace_armed = false;
    a2gspu_rail_arm_env("A2GSPU_TRACE_FROM");

    // A2GSPU_COVERAGE="LO-HI"|"BANK:LO-HI" -> per-byte execution bitmap, written
    // to A2GSPU_COVERAGE_OUT at spike end.  Feeds deasmiigs --coverage so the
    // code/data split becomes a measurement instead of a heuristic.
    if (const char *cv = SDL_getenv("A2GSPU_COVERAGE")) a2gspu_cov_init(cv);

    // ---- A2GSPU_ITRACE: additive, env-gated, per-instruction execution trace ----
    // Two arm modes (OR'd): A2GSPU_ITRACE_FROM=<hexPC> arms when full_pc first
    // hits that 24-bit PC; A2GSPU_ITRACE_FRAME=<N> arms at the start of headless
    // frame N. A2GSPU_ITRACE_N caps logged instructions (default 256). The master
    // gate g_iigs_itrace_enabled is set iff at least one arm mode is supplied, so
    // a normal run/golden (no ITRACE_* set) pays only one untaken branch.
    g_iigs_itrace_enabled = false; g_iigs_itrace_armed = false;
    g_iigs_itrace_logged = 0; g_iigs_cur_frame = 0;
    g_iigs_itrace_use_pc = false; g_iigs_itrace_from = 0; g_iigs_itrace_frame = -1;
    g_iigs_itrace_n = 256;
    g_iigs_itrace_lo = 0; g_iigs_itrace_hi = 0; g_iigs_itrace_out = nullptr; g_iigs_itrace_rearm = 0;
    if (const char *itf = SDL_getenv("A2GSPU_ITRACE_FROM")) {
        g_iigs_itrace_from = (uint32_t)strtoul(itf, nullptr, 16) & 0xFFFFFF;
        g_iigs_itrace_use_pc = true; g_iigs_itrace_enabled = true;
    }
    a2gspu_rail_arm_env("A2GSPU_CALLTRACE_N");
    a2gspu_rail_arm_env("A2GSPU_CALLTRACE_SKIP");
    a2gspu_rail_arm_env("A2GSPU_CALLTRACE");
    a2gspu_rail_arm_env("A2GSPU_MILESTONES");
    a2gspu_rail_arm_env("A2GSPU_RETGUARD");
    a2gspu_rail_arm_env("A2GSPU_ITRACE_FRAME");
    if (const char *itn = SDL_getenv("A2GSPU_ITRACE_N")) {
        int v = (int)strtol(itn, nullptr, 10);
        if (v > 0) g_iigs_itrace_n = v;
    }
    // ITRACE v2 (batch-2): PC-RANGE window + per-instr EA/value + file sink + re-arm.
    a2gspu_rail_arm_env("A2GSPU_ITRACE_LO");
    a2gspu_rail_arm_env("A2GSPU_ITRACE_HI");
    a2gspu_rail_arm_env("A2GSPU_ITRACE_REARM");
    a2gspu_rail_arm_env("A2GSPU_ITRACE_OUT");
    if (g_iigs_itrace_enabled)
        fprintf(stderr, "IIGS ITRACE: enabled (from=%s$%06X frame=%d n=%d)\n",
                g_iigs_itrace_use_pc ? "" : "(none)",
                (unsigned)g_iigs_itrace_from, g_iigs_itrace_frame, g_iigs_itrace_n);
    g_iigs_sym_base = 0; g_iigs_sym_base_locked = false;
    a2gspu_rail_arm_env("A2GSPU_SYM_BASE");
    if (const char *sp = SDL_getenv("A2GSPU_SYMBOLS")) iigs_symbols_load(sp);
    // A2GSPU_ROM_SYMBOLS=<file> (batch-2): name ROM-resident PCs (else "<ROM>").
    a2gspu_rail_arm_env("A2GSPU_ROM_SYMBOLS");
    // A2GSPU_SNAP region-logger (batch-2): dump [LO,HI] at up to 8 trigger PCS -> OUT.
    // The window and PCs configure; OUT arms (and says why when it cannot).
    g_snap_npc = 0; g_snap_count = 0; g_snap_out = nullptr;
    a2gspu_rail_arm_env("A2GSPU_SNAP_LO");
    a2gspu_rail_arm_env("A2GSPU_SNAP_HI");
    a2gspu_rail_arm_env("A2GSPU_SNAP_PCS");
    a2gspu_rail_arm_env("A2GSPU_SNAP_MAX");
    a2gspu_rail_arm_env("A2GSPU_SNAP_OUT");
    a2gspu_rail_arm_env("A2GSPU_SNAP_ZP");
    g_iigs_pending.clear();
    g_iigs_last_result.clear();
    g_iigs_last_gsos_err = 0;

    // GAP-1: enter the interactive CTRL rail HERE — after the full env-instrument
    // suite + symbols are armed — so WATCH/VALTRAP/BREAK/POKE/ITRACE/CALLTRACE/
    // SNAP/CONDTRAP/STACK*/MILESTONES/RETGUARD fire during the loop's `run`
    // frames. Everything below (E1 snapshot, boot loops, oracle dumps, golden
    // gate) is the fixed-frame spike and is bypassed for CTRL, exactly as the
    // original early short-circuit did.
    if (a2gspu_ctrl_mode) {
        if(state->agent_socket_mode) {
            computer->execution_mode=EXEC_PAUSED;computer->instructions_left=0;
            g_a2gspu_agent_bootstrap_complete=true;
            a2gspu_pins_print(state->platform_id,0,BUILD_VERSION);
            printf("A2GSPU AGENT: shared CTRL bootstrap complete; socket owns a paused machine\n");
            return;
        }
        a2gspu_ctrl_loop(state);
        // Destroy the computer BEFORE exiting. exit() does not run destructors,
        // and computer_t::~computer_t() is what fires every registered shutdown
        // handler -- so in rail mode every shutdown-time artifact was silently
        // discarded. The ~BUS cycle dump is the one that surfaced it: the device
        // announced "cycle dump enabled" at startup, recorded cycles all session,
        // and then wrote nothing, with no error, because the writer never ran.
        // Any device that registers a shutdown handler had the same hole.
        delete state->computer;
        state->computer = nullptr;
        exit(0);
    }

    // Snapshot the $E1 SHR window at trace-arm time. Replaying the trace
    // (init + every captured write) must byte-match the final $E1 below -> proves
    // the two taps capture 100% of SHR-affecting writes (snoop-completeness test).
    {
        const uint8_t *m2i = state->mmu_iigs ? state->mmu_iigs->megaii_cpu_view() : nullptr;   // CPU order
        if (m2i) {
            const std::string ip = a2gspu_out_path("spike_e1_init.bin", a2gspu_outbuf, sizeof a2gspu_outbuf);
            FILE *fi = fopen(ip.c_str(), "wb");
            if (fi) {
                fwrite(m2i + 0x12000, 1, 0x8000, fi); fclose(fi); // $E1:$2000-$9FFF
                uint64_t h = HOUSE_FNV_BASIS;
                for (int a = 0; a < 0x8000; a++) { h ^= m2i[0x12000 + a]; h *= HOUSE_FNV_PRIME; }
                a2gspu_artifact_meta(ip.c_str(), "megaii-e1-shr-window-v1", 1, 0x8000,
                                     "u8 x 32768: Mega II bank $E1 $2000-$9FFF at trace arm",
                                     "house-FNV-1a-64 over all 32768 bytes", h);
            }
        }
    }

    // Optional: inject + launch a flat Rosetta-built binary after some boot frames.
    //   A2GSPU_RUN_BIN=path[@HEXADDR]   (default load/exec addr $7E0000)
    //   A2GSPU_RUN_BOOT=N               (boot frames before injecting; default = spike_frames/2)
    // The binary's bus effects are isolated by resetting the traces at injection time.
    const char *snap_save = SDL_getenv("A2GSPU_SNAP_SAVE");
    const char *snap_load = SDL_getenv("A2GSPU_SNAP_LOAD");
    if (!snap_load) snap_load = SDL_getenv("A2GSPU_RESTORE");  // A2GSPU_RESTORE: load-at-start alias (generalizes SNAP_LOAD)
    const char *runbin = getenv("A2GSPU_RUN_BIN");

    // SNAP_LOAD: short-circuit the ~1200-frame boot by restoring a prior desktop snapshot.
    // Runs BEFORE the boot loops below; the post-restore frame loops then self-heal the
    // state the file does not hold (the scanner, the clock, floppy media -- the manifest's
    // excluded= list) before $E1 is read. The device blocks it does hold restore here.
    bool snap_restored_adb = false;   // a snapshot that holds the ADB micro takes keys like a booted machine
    if (snap_load) {
        FILE *sf = fopen(snap_load, "rb");
        if (!sf) {
            fprintf(stderr, "A2GSPU SNAP: FAIL could not open '%s' for load -- aborting SPIKE (exit 2)\n", snap_load);
            exit(2);
        } else {
            bool cpu_ok = a2gspu_cpu_load(sf, computer->cpu);
            bool mmu_ok = (cpu_ok && state->mmu_iigs) ? state->mmu_iigs->A2GSPU_restore(sf) : false;
            bool dev_ok = (cpu_ok && mmu_ok) ? a2gspu_snap_read_devices(sf, computer) : false;
            bool end_ok = dev_ok ? a2gspu_snap_check_sentinel(sf) : false;
            const std::string manifest = end_ok ? a2gspu_snap_read_manifest(sf) : std::string();
            fclose(sf);
            if (!cpu_ok || !mmu_ok || !dev_ok || !end_ok) {
                printf("A2GSPU SNAP: restore FAILED from '%s' (cpu=%s mmu=%s devices=%s end=%s) -- bad/stale magic, "
                       "geometry mismatch, a device block that did not restore, or truncated snapshot; aborting.\n", snap_load,
                       cpu_ok ? "ok" : "BAD-MAGIC", mmu_ok ? "ok" : "BAD", dev_ok ? "ok" : "BAD",
                       end_ok ? "ok" : "TRUNC");
                exit(2);
            }
            snap_restored_adb = g_snap_dev_restored.find("adb") != std::string::npos;
            computer->cpu->halt = 0;          // force-run (HLT would no-op run_one_frame)
            // re-arm the trace window so it covers ONLY the post-restore run (mirror of
            // the inject re-arm on the runbin path)
            bus_trace_reset(); slot_bus_reset(); mmu_state_trace_reset();
            printf("A2GSPU SNAP: restored from '%s'; skipping boot.\n", snap_load);
            a2gspu_snap_print_manifest(manifest);
            a2gspu_snap_print_devices();
        }
    }

    // Shared frame counter so LOAD_BOOT / RUN_BOOT / main loop do not double-run.
    int spike_i = 0;

    // A2GSPU_LOAD: multi-file deliberate RAM splice WITHOUT changing PC.
    // For a2tile / a2engine banks on IIe (tiles.bin@$6000, attrs@$8800) before
    // A2GSPU_DHGR_GOLDEN. Specs are comma/semicolon-separated:
    //   tiles.bin@6000;attrs.bin@8800
    //   6000@tiles.bin,8800@attrs.bin
    // Optional A2GSPU_LOAD_BOOT=N frames first (default 0). Hard-fails on any
    // entry error so CI cannot silently gate an unloaded bank.
    if (const char *loads = SDL_getenv("A2GSPU_LOAD")) {
        int load_boot = 0;
        if (const char *lb = SDL_getenv("A2GSPU_LOAD_BOOT")) load_boot = SDL_atoi(lb);
        if (load_boot < 0) load_boot = 0;
        if (load_boot > state->spike_frames) load_boot = state->spike_frames;
        if (!snap_load) {
            for (; spike_i < load_boot; spike_i++) {
                iigs_itrace_frame_tick(spike_i);
                if (!run_one_frame(computer)) {
                    printf("SPIKE: halted during LOAD_BOOT at frame %d\n", spike_i);
                    break;
                }
            }
        }
        if (!a2ctrl::apply_env_loads(computer, loads)) {
            printf("A2GSPU LOAD: aborting SPIKE (exit 2) — fix paths/specs\n");
            exit(2);
        }
    }

    // A2GSPU_VRAM_LOAD: inject offline-painted DHGR page (16K aux‖main).
    // Used by a2tile `paint` so A2GSPU_DHGR_GOLDEN can gate without guest blit.
    //   A2GSPU_VRAM_LOAD=page.vram   A2GSPU_VRAM_PAGE=1|2 (default 1)
    if (const char *vl = SDL_getenv("A2GSPU_VRAM_LOAD")) {
        int vpage = 1;
        if (const char *vp = SDL_getenv("A2GSPU_VRAM_PAGE")) {
            if (vp[0] == '2') vpage = 2;
        }
        char err[256];
        if (a2ctrl::load_vram_raw(computer, vl, vpage, err, sizeof err)) {
            printf("A2GSPU VRAM_LOAD: OK page%d 16384 aux+main from %s "
                   "(DELIBERATE splice profile=vram-raw)\n",
                   vpage, vl);
        } else {
            printf("A2GSPU VRAM_LOAD: FAIL %s — aborting SPIKE (exit 2)\n", err);
            exit(2);
        }
    }

    if (runbin) {
        char binpath[1024];
        strncpy(binpath, runbin, sizeof(binpath) - 1);
        binpath[sizeof(binpath) - 1] = 0;
        uint32_t loadaddr = 0x7E0000;
        char *at = strchr(binpath, '@');
        if (at) { *at = 0; loadaddr = (uint32_t)strtoul(at + 1, nullptr, 16); }
        int bootframes = state->spike_frames / 2;
        const char *bf = getenv("A2GSPU_RUN_BOOT");
        if (bf) bootframes = atoi(bf);
        if (bootframes < 0) bootframes = 0;
        if (bootframes > state->spike_frames) bootframes = state->spike_frames;

        if (!snap_load) {   // SNAP_LOAD already provided a booted desktop; skip the long boot
            for (; spike_i < bootframes; spike_i++) {
                iigs_itrace_frame_tick(spike_i);
                if (!run_one_frame(computer)) { printf("SPIKE: halted during boot at frame %d\n", spike_i); break; }
            }
        }
        FILE *rb = (state->mmu_iigs) ? fopen(binpath, "rb") : nullptr;
        if (rb) {
            // THE INJECTION IS VERIFIED, OR IT DID NOT HAPPEN.
            //
            // This used to be a hand-rolled `while (fgetc) mmu->write(loadaddr + n)`
            // loop that printed the FILE's size as the byte count. Three things
            // were wrong with it and all three reported success:
            //   * no read-back, so `RUN_BIN=rt.bin@00D000` wrote 13 bytes into
            //     bank-0 ROM, printed "injected 13 bytes at $00D000; PC set."
            //     and the CPU ran ROM;
            //   * a flat 24-bit add, so `@7EFFFE` spilled the third byte into
            //     bank $7F while claiming it landed at $7E;
            //   * no language-card bank-in, which is why the $D000 write went
            //     nowhere at all.
            // a2ctrl::load_file_at fixes all three (it banks the language card
            // in for a bank-0 $D000+ load -- two reads of $C08B, a real
            // soft-switch side effect this rail did not previously perform, and
            // must, for such a write to land) and verifies every byte by
            // probe_peek before returning true.
            //
            // ONE DELIBERATE DIFFERENCE FROM load_file_at'S OWN POLICY: it
            // WRAPS to (start+n)&0xFFFF rather than spilling. A wrap is quieter
            // than a spill and worse -- a Rosetta binary folded back over its
            // own entry point still "loads" -- so RUN_BIN refuses the case
            // outright, before a single byte is written.
            fseek(rb, 0, SEEK_END);
            const long rbsz = ftell(rb);
            fclose(rb);
            const unsigned lo16 = loadaddr & 0xFFFF, bank8 = (loadaddr >> 16) & 0xFF;
            if (rbsz > 0 && (long)lo16 + rbsz > 0x10000) {
                fprintf(stderr, "A2GSPU RUN: FAIL '%s' is %ld bytes at $%02X/%04X -- byte %ld "
                        "would wrap to $%02X/0000 and overwrite the start of the bank "
                        "(the loader confines a write to one bank) -- aborting SPIKE (exit 2)\n",
                        binpath, rbsz, bank8, lo16, (long)(0x10000 - lo16), bank8);
                exit(2);
            }
            long nb = 0; char lerr[256] = {0};
            if (a2ctrl::load_file_at(computer, loadaddr, binpath, &nb, lerr, sizeof lerr)) {
                bus_trace_reset(); slot_bus_reset(); mmu_state_trace_reset();   // isolate the injected program
                computer->cpu->full_pc = loadaddr;                              // jump to it (pb = addr>>16)
                printf("A2GSPU RUN: injected %ld bytes at $%02X/%04X-$%02X/%04X (verified by read-back) "
                       "after %d boot frames; PC set.\n",
                       nb, bank8, lo16, bank8, (unsigned)((lo16 + (unsigned)nb - 1) & 0xFFFF), spike_i);
            } else {
                // Same refusal as the missing-binary branch below, for the same
                // reason: a run whose binary did not arrive is the boot disk
                // wearing the binary's name.
                fprintf(stderr, "A2GSPU RUN: FAIL %s -- aborting SPIKE (exit 2)\n", lerr);
                exit(2);
            }
        } else if (!state->mmu_iigs) {
            a2gspu_warn("A2GSPU RUN", "no IIgs MMU on this platform -- skipped (use -p 5)");
        } else {
            // A run whose binary did not load is the boot disk wearing the
            // binary's name. It measured as a healthy row once. Refuse.
            fprintf(stderr, "A2GSPU RUN: FAIL could not open binary '%s' -- aborting SPIKE (exit 2)\n", binpath);
            exit(2);
        }
        for (; spike_i < state->spike_frames; spike_i++) {
            iigs_itrace_frame_tick(spike_i);
            if (!run_one_frame(computer)) { printf("SPIKE: emulation halted early at frame %d\n", spike_i); break; }
            if (g_iigs_hang_detected) { printf("SPIKE: hang detected, halting at frame %d\n", spike_i); break; }
        }
        printf("A2GSPU RUN: final CPU full_pc=$%06X (if ~= the inject addr, the injected code ran)\n",
               (unsigned)computer->cpu->full_pc);
    } else {
        // a2gspu spike key injection (env-gated; pascal-toolchain UC-2 boot spikes):
        //   A2GSPU_SPIKE_KEYS=<string>   keys to type (\n is translated to \r by
        //                                the keyboard paste path)
        //   A2GSPU_SPIKE_KEYS_AT=N       frame to inject at (default spike_frames/2)
        // Feeds the keyboard's existing paste_buffer, so consumption is paced by
        // the emulated software's own $C000 polls — no artificial key timing.
        const char *spike_keys = SDL_getenv("A2GSPU_SPIKE_KEYS");
        int spike_keys_at = state->spike_frames / 2;
        if (const char *ka = SDL_getenv("A2GSPU_SPIKE_KEYS_AT")) spike_keys_at = SDL_atoi(ka);
        for (; spike_i < state->spike_frames; spike_i++) {
            int i = spike_i;
            iigs_itrace_frame_tick(i);
            if (spike_keys && i == spike_keys_at) {
                if (snap_load && !snap_restored_adb)
                    a2gspu_warn("A2GSPU SPIKE KEYS", "the machine was restored from a snapshot that excludes the ADB "
                                "micro; injected keys may never be collected (a restored desktop never polls $C000 again)");
                keyboard_state_t *kb = (keyboard_state_t *)computer->get_module_state(MODULE_KEYBOARD);
                keygloo_state_t *kg = (keygloo_state_t *)computer->get_module_state(MODULE_KEYGLOO);
                if (kb) {
                    kb->paste_buffer += spike_keys;
                    printf("A2GSPU SPIKE KEYS: injected %d chars at frame %d (paste path)\n",
                           (int)strlen(spike_keys), i);
                } else if (kg && kg->kg) {
                    // A IIgs has no MODULE_KEYBOARD and therefore no paste buffer:
                    // its keys arrive through the ADB/KeyGloo path. Until this
                    // branch existed the rail printed "no keyboard module --
                    // skipped" on every IIgs run, which is honest but leaves the
                    // one platform this fork exists for unable to be driven in
                    // spike mode at all. Same one-force_key-per-char sequence the
                    // CTRL `keys` verb uses, so the two paths cannot diverge.
                    int nkeys = 0;
                    for (const char *p = spike_keys; *p; p++) {
                        uint8_t ch = (uint8_t)*p;
                        if (ch == '\n') ch = '\r';
                        kg->kg->force_key(ch);
                        keygloo_update_interrupt_status(kg, kg->kg);
                        for (int f = 0; f < 2 && spike_i < state->spike_frames; f++) {
                            if (!run_one_frame(computer)) break;
                            spike_i++;
                        }
                        kg->kg->hold_key(0);
                        kg->kg->key_up();
                        nkeys++;
                    }
                    printf("A2GSPU SPIKE KEYS: injected %d chars at frame %d (ADB path)\n",
                           nkeys, i);
                } else {
                    a2gspu_warn("A2GSPU SPIKE KEYS", "no keyboard AND no KeyGloo -- skipped");
                }
            }
            if (!run_one_frame(computer)) {
                printf("SPIKE: emulation halted early at frame %d\n", i);
                break;
            }
            if (g_iigs_hang_detected) {   // #2: no-BRK degenerate-loop hang -> stop wasting frames
                printf("SPIKE: hang detected, halting at frame %d\n", i);
                break;
            }
        }
    }
    // SNAP_SAVE: after BOTH boot paths, post run_one_frame mutation, pre trace-disarm.
    // The assert/golden gate further below still runs, so a SNAP_SAVE run can be golden-blessed.
    if (snap_save) {
        FILE *sf = fopen(snap_save, "wb");
        if (!sf) { printf("A2GSPU SNAP: could not open '%s' for save\n", snap_save); }
        else if (!state->mmu_iigs) { printf("A2GSPU SNAP: no IIgs MMU (use -p 5)\n"); fclose(sf); }
        else {
            a2gspu_cpu_save(sf, computer->cpu);
            state->mmu_iigs->A2GSPU_snapshot(sf);
            const std::string devs = a2gspu_snap_write_devices(sf, computer);
            a2gspu_snap_write_sentinel(sf);   // trailing magic -> truncation is detectable on load
            const std::string manifest = a2gspu_snap_manifest_text(spike_i, devs);
            a2gspu_snap_write_manifest(sf, manifest);
            fclose(sf);
            printf("A2GSPU SNAP: saved machine state to '%s'\n", snap_save);
            a2gspu_snap_print_manifest(manifest);
        }
    }

    g_bus_trace_enabled = false;  // disarm before any teardown writes
    g_obs_enabled = false;        // disarm the Observatory spine (ring stays intact for the dump/proof)
    g_slot_bus_enabled = false;
    g_mmu_state_trace_enabled = false;

    // ---- (1) renderer-free $E1 oracle ----
    const uint8_t *m2 = state->mmu_iigs ? state->mmu_iigs->megaii_cpu_view() : nullptr;   // CPU order
    if (m2) {
        const uint8_t *e1 = m2 + 0x10000;          // Mega II bank $E1 (64 KB), as the CPU addresses it
        const std::string e1p = a2gspu_out_path("spike_e1.bin", a2gspu_outbuf, sizeof a2gspu_outbuf);
        FILE *f = fopen(e1p.c_str(), "wb");
        const bool e1_written = (f != nullptr);
        if (f) { fwrite(e1, 1, 0x10000, f); fclose(f); }
        uint64_t hash = HOUSE_FNV_BASIS;    // FNV-1a 64
        int nonzero = 0; uint8_t seen[256] = {0}; int ndist = 0;
        for (int a = 0x2000; a <= 0x9FFF; a++) {
            uint8_t b = e1[a];
            hash = (hash ^ b) * HOUSE_FNV_PRIME;
            if (b) nonzero++;
            if (!seen[b]) { seen[b] = 1; ndist++; }
        }
        printf("SPIKE E1: wrote spike_e1.bin (64KB).\n");
        printf("SPIKE E1: SHR-window $2000-$9FFF nonzero=%d/32768 distinct=%d hash=%016llX\n",
               nonzero, ndist, (unsigned long long)hash);
        printf("SPIKE E1: SCB[0..7]@$9D00 = %02X %02X %02X %02X %02X %02X %02X %02X\n",
               e1[0x9D00], e1[0x9D01], e1[0x9D02], e1[0x9D03],
               e1[0x9D04], e1[0x9D05], e1[0x9D06], e1[0x9D07]);
        if (e1_written)
            a2gspu_artifact_meta(e1p.c_str(), "megaii-e1-64k-v1", 1, 0x10000,
                                 "u8 x 65536: Mega II bank $E1 $0000-$FFFF at spike end",
                                 "house-FNV-1a-64 over file bytes $2000-$9FFF (the SHR window; the SPIKE E1 hash)", hash);
        // Headless SHR/video-state summary (mode/palette/pixel histogram).
        if (SDL_getenv("A2GSPU_VIDEOSUM")) iigs_video_summary(e1);
        // Downsampled ASCII map of the screen (SEE a rectangle/layout headless).
        if (SDL_getenv("A2GSPU_VIDEOMAP")) iigs_video_map(e1);
        // 40-col text page ASCII decode (bank $E0:$0400-$07FF = megaii base m2).
        if (SDL_getenv("A2GSPU_TEXT40")) iigs_text40(m2);
    } else {
        printf("SPIKE E1: mmu_iigs/megaii base is NULL -- FAILED\n");
    }

    // ---- (1.5) bus-trace oracle: ordered SHR-write golden ----
    {
        uint64_t n = 0;
        const std::string tp = a2gspu_out_path("spike_trace.bin", a2gspu_outbuf, sizeof a2gspu_outbuf);
        uint64_t th = bus_trace_dump(tp.c_str(), &n);
        uint64_t c0 = g_bus_trace.empty() ? 0 : g_bus_trace.front().cycle;
        uint64_t c1 = g_bus_trace.empty() ? 0 : g_bus_trace.back().cycle;
        printf("SPIKE TRACE: wrote spike_trace.bin (%llu SHR writes) content-hash=%016llX\n",
               (unsigned long long)n, (unsigned long long)th);
        a2gspu_artifact_meta(tp.c_str(), "bus-trace-v1", (unsigned)sizeof(BusTraceRecord), n,
                             "u64 cycle, u32 addr17 (bit16 = bank $E1), u8 data, u8 rw (1=write), u8 src (0 direct $E1, 1 shadowed), u8 pad",
                             "house-FNV-1a-64 over each record's addr17 low 3 bytes (LE), data, rw, in file order", th);
        printf("SPIKE TRACE: cycle span [%llu .. %llu]\n",
               (unsigned long long)c0, (unsigned long long)c1);
        // Bracket the shadow-mirror slot-visibility outcome (provenance-tagged, before any hardware).
        uint64_t ha = 0, hd = 0, nd = 0, ns = 0;
        bus_trace_brackets(&ha, &hd, &nd, &ns);
        printf("SPIKE BRACKET: shadow-VISIBLE   (naked M2B0 sees ALL)      hash=%016llX  writes=%llu  miss=0\n",
               (unsigned long long)ha, (unsigned long long)(nd + ns));
        printf("SPIKE BRACKET: shadow-INVISIBLE (naked M2B0, direct-$E1 only) hash=%016llX  writes=%llu  MISS=%llu shadowed\n",
               (unsigned long long)hd, (unsigned long long)nd, (unsigned long long)ns);

        // ---- Observatory keystone proof: the bus projection of the GENERAL signal
        //      spine reproduces the bus-trace golden BY CONSTRUCTION (same store site,
        //      same order) -> ObsRecord is a faithful superset of BusTraceRecord, and
        //      the E1 determinism golden is a projection of the one Observatory stream.
        uint64_t on = 0;
        uint64_t oh = obs_hash_bus(&on);
        printf("SPIKE OBS: obs_hash_bus=%016llX (%llu BUS_TXN, SHR window) vs bus_trace=%016llX -> %s\n",
               (unsigned long long)oh, (unsigned long long)on, (unsigned long long)th,
               (oh == th) ? "MATCH (superset proven)" : "MISMATCH");
        uint32_t last_aux = g_obs_ring.empty() ? 0 : g_obs_ring.back().aux;
        printf("SPIKE OBS: ring=%llu records; reclaimed last aux (c14m_cost<<8|cycle_type)=%08X\n",
               (unsigned long long)g_obs_ring.size(), last_aux);
    }

    // ---- (1.6) faithful slot-bus stream (the virtual slot; superset of the SHR oracle) ----
    {
        uint64_t n = 0;
        const std::string slp = a2gspu_out_path("spike_slot.bin", a2gspu_outbuf, sizeof a2gspu_outbuf);
        uint64_t sh = slot_bus_dump(slp.c_str(), &n);
        printf("SPIKE SLOT: wrote spike_slot.bin (%llu Mega-II writes) content-hash=%016llX\n",
               (unsigned long long)n, (unsigned long long)sh);
        a2gspu_artifact_meta(slp.c_str(), "slot-bus-v1", (unsigned)sizeof(slot_bus_cycle_t), n,
                             "u64 cycle, u16 addr, u8 data, u8 ctl (SLOT_CTL_* bits), then padding to the record size",
                             "house-FNV-1a-64 over each record's addr low byte, addr high byte, data, ctl and a zero byte, in file order", sh);
    }

    // ---- (1.65) MEMVU_STOREVIS: store-visibility accounting ----
    // What fraction of this workload's stores land where a non-CPU agent can see
    // them? A property of the software, so it is answerable here exactly.
    memvu_report_all(stdout);

    // ---- (1.7) ground-truth MMU-state stream (the bus-snoop comparator's authoritative reference) ----
    {
        uint64_t n = 0;
        const std::string mp = a2gspu_out_path("spike_mmu_truth.bin", a2gspu_outbuf, sizeof a2gspu_outbuf);
        uint64_t mh = mmu_state_trace_dump(mp.c_str(), &n);
        printf("SPIKE MMU: wrote spike_mmu_truth.bin (%llu mapping-state changes) content-hash=%016llX\n",
               (unsigned long long)n, (unsigned long long)mh);
        a2gspu_artifact_meta(mp.c_str(), "mmu-state-v1", (unsigned)sizeof(MMUStateRecord), n,
                             "u64 cycle, u8 reg_state ($C068), u8 reg_shadow ($C035), u8 reg_new_video ($C029), u8 reg_speed ($C036), u8 flags (bit0 80STORE, bit1 HIRES, bit2 TEXT, bit3 MIXED), u8 reg_slot ($C02D), u8 disp_vmode, u8 disp_mode, u8 pad x7",
                             "house-FNV-1a-64 over each record's reg_state, reg_shadow, reg_new_video, reg_speed, flags, in file order", mh);
    }

    // ---- (2) backbuffer pixel-readback datum ----
    video_system_t *vs = computer->video_system;
    vs->update_display(true);
    SDL_ClearError();
    vs->save_screenshot(a2gspu_out_path("spike_frame.bmp", a2gspu_outbuf, sizeof a2gspu_outbuf));
    const char *err = SDL_GetError();
    printf("SPIKE FRAMEBUF: save_screenshot('spike_frame.bmp') SDL_GetError='%s'\n",
           (err && *err) ? err : "(none)");

    // ---- (2.5) text page 1 dump (env-gated, platform-agnostic) ----
    // A2GSPU_TEXTDUMP=<file>: 2KB — main $0400-$07FF via probe_peek (no C0XX
    // side effects) followed by AUX $0400-$07FF from the flat 128K image
    // (aux lives at +0x10000; holds the EVEN columns in 80-col mode, which
    // Apple Pascal 1.3 uses). Lets harnesses assert on screen TEXT instead of
    // pixels (pascal-toolchain UC-2 RUN_GREEN gate).
    if (const char *tf = SDL_getenv("A2GSPU_TEXTDUMP")) {
        tf = a2gspu_out_path(tf, a2gspu_outbuf, sizeof a2gspu_outbuf);   // A2GSPU_OUT_DIR (relative names only)
        FILE *tfp = fopen(tf, "wb");
        if (tfp) {
            for (uint32_t a = 0x0400; a < 0x0800; a++) {
                uint8_t b = rail_mmu(computer)->probe_peek(a);
                fwrite(&b, 1, 1, tfp);
            }
            // Same aux half, same reason -- see the note on the other textdump.
            const uint8_t *mem = rail_video_base(computer);
            for (uint32_t a = 0x0400; a < 0x0800; a++) {
                uint8_t b = mem ? mem[0x10000 + a] : rail_mmu(computer)->probe_peek(0x010000u | a);
                fwrite(&b, 1, 1, tfp);
            }
            fclose(tfp);
            printf("A2GSPU TEXTDUMP: wrote '%s' (text page 1 main+aux, 2KB)\n", tf);
        } else {
            printf("A2GSPU TEXTDUMP: could not open '%s'\n", tf);
        }
    }

    // ---- (3) optional headless memory-range hexdump + final CPU state ----
    if (const char *dg = SDL_getenv("A2GSPU_DUMP")) {
        const uint8_t *mb = state->mmu_iigs ? state->mmu_iigs->megaii_cpu_view() : nullptr;   // CPU order
        iigs_mem_range_dump(computer->cpu, mb, dg);
    }
    if (g_iigs_brkdump_enabled) iigs_cpu_state_dump_regs(computer->cpu, "SPIKE-END");
    if (g_loadtrace_on) { iigs_loadtrace_flush(); printf("IIGS LOADTRACE: %d segment loads recorded\n", g_lt_loads); }
    if (!g_obs_registry.empty()) obs_view_fault(computer->cpu, "SPIKE-END");  // Observatory LEVEL-pull proof (post-golden, neutral)
    obs_view_memwindows("*");   // Observatory: dump the registered dark-subsystem memory windows (DOC/ADB/SCC)
    {   // Observatory: IRQ edge timeline proof — aggregate pending + edges recorded this run
        uint64_t irqp = 0; obs_read(obs_sigid(OBS_SUB_IRQ, 0, 0), &irqp);
        uint64_t nedge = 0; for (const ObsRecord &r : g_obs_ring) if (r.kind == OBS_K_IRQ_EDGE) nedge++;
        printf("OBS IRQ: pending=%08llX  edges_recorded=%llu\n",
               (unsigned long long)irqp, (unsigned long long)nedge);
    }
    {   // Observatory: OwnerHandle self-check (transient-owner indirection: bind, then stale-safe)
        const void *saved = g_obs_owner_base[OBS_OWNER_VGC];
        int scratch = 0;
        obs_owner_register(OBS_OWNER_VGC, &scratch);
        bool bound = (g_obs_owner_base[OBS_OWNER_VGC] == &scratch);
        obs_owner_release(OBS_OWNER_VGC);
        bool freed = (g_obs_owner_base[OBS_OWNER_VGC] == nullptr);
        g_obs_owner_base[OBS_OWNER_VGC] = saved;   // restore (no-op today; safe once VGC binds it)
        printf("OBS HANDLE: register->bound=%d release->freed=%d (expect 1/1)\n", bound, freed);
    }
    iigs_milestones_report();   // A2GSPU_MILESTONES: reached / NOT-REACHED table

    // A2GSPU_COVERAGE: flush the execution bitmap BEFORE the gate block below,
    // which exit()s on any gate and would otherwise discard the run's coverage.
    if (const char *co = SDL_getenv("A2GSPU_COVERAGE_OUT")) a2gspu_cov_write(co);

    // ---- (4) golden-diff (#9) + assertion gate (#2) -> exit code (CI loop) ----
    {
        const uint8_t *m2a = state->mmu_iigs ? state->mmu_iigs->megaii_cpu_view() : nullptr;   // CPU order
        if (m2a) {
            const uint8_t *e1a = m2a + 0x10000;
            int nz = 0; uint8_t sn[256] = {0}; int nd = 0;
            uint64_t h = HOUSE_FNV_BASIS;   // house FNV basis
            for (int a = 0x2000; a <= 0x9FFF; a++) {
                uint8_t b = e1a[a]; h = (h ^ b) * HOUSE_FNV_PRIME;
                if (b) nz++; if (!sn[b]) { sn[b] = 1; nd++; }
            }
            int gate_rc = 0; bool any_gate = false, gate_blessed = false;
            if (const char *gf = SDL_getenv("A2GSPU_GOLDEN")) {
                any_gate = true;
                // ONE implementation, a2dhgr::golden_gate_file. This was a third
                // hand-rolled copy of it and the copy had drifted in the way
                // copies do: it had no final `else`, so a golden path under a
                // directory that does not exist printed NOTHING -- not a line
                // with GOLDEN in it -- and the run still reported gate=PASS
                // rc=0 and exited 0. Docs/A2GSPU_RAILS.md states the opposite
                // principle outright: an output sink that cannot be opened
                // aborts, because a run that measured something other than what
                // it was asked to is worse than no run.
                gate_rc |= a2dhgr::golden_gate_file(gf, h, "IIGS GOLDEN",
                                                    "shr-window", &gate_blessed);
                // A bless compared nothing. Warn so warnings= is non-zero and
                // A2GSPU_STRICT=1 refuses to call the run OK; the verdict word
                // below says BLESSED rather than PASS for the same reason.
                if (gate_blessed)
                    a2gspu_warn("IIGS GOLDEN", "blessed a missing golden; nothing was compared");
            }
            if (const char *eh = SDL_getenv("A2GSPU_EXPECT_HASH")) {
                // The inline form of GOLDEN: a hash pinned in the invocation
                // rather than in a file. A harness that differences two runs
                // can demand the shorter one reproduce before it subtracts.
                any_gate = true;
                const uint64_t want = strtoull(eh, nullptr, 16);
                const bool match = (want == h);
                printf("A2GSPU EXPECT_HASH: %s (cur=%016llX want=%016llX)\n",
                       match ? "MATCH" : "MISMATCH", (unsigned long long)h, (unsigned long long)want);
                if (!match) gate_rc = 1;
            }
            {
                // M9: the run-hash twin of EXPECT_HASH -- the same screen, the
                // same clock AND the same work (insns, stores).
                bool run_fail = false;
                if (a2gspu_expect_run_gate(h, computer->clock->get_cycles(), &run_fail)) {
                    any_gate = true;
                    if (run_fail) gate_rc = 1;
                }
            }
            if (const char *as = SDL_getenv("A2GSPU_ASSERT")) {
                // ARMED BY A NON-EMPTY SPEC, NOT BY THE VARIABLE'S PRESENCE.
                // Arming on presence alone made A2GSPU_ASSERT="" report
                // gate=PASS: a verdict on a question nobody asked. A harness
                // that exports the variable unconditionally-empty wants no
                // gate, so it gets gate=none; a non-empty spec that parses to
                // nothing is a different thing and the evaluator fails it.
                if (a2gspu_assert_armed(as)) {
                    any_gate = true;
                    gate_rc |= iigs_eval_asserts(as, computer->cpu, e1a, nz, nd);
                } else {
                    printf("IIGS ASSERT: A2GSPU_ASSERT is set but empty -- no gate armed (gate=none)\n");
                }
            }
            // machine-readable status line for the corpus harness (exit code stays
            // gate-driven for harness compat; the status NAME is the richer category)
            long scb = (e1a[0x9D00] & 0x80) ? 640 : 320;
            // Honest verdict: a no-BRK degenerate-loop hang and a boot that never
            // reached all milestones are NOT "OK" — distinguish them so a silent hang
            // (e.g. a wild jump into garbage that never BRKs) stops reading as success.
            const char *st = (any_gate && gate_rc) ? "GATE_FAIL"
                           : g_iigs_brk_count       ? "CRASH_BRK"
                           : g_iigs_hang_detected    ? "HANG"
                           : g_iigs_last_gsos_err    ? "GSOS_ERROR"
                           : iigs_boot_incomplete()  ? "STALLED" : "OK";
            // THREE OUTCOMES, NOT TWO. "compared and matched" and "compared
            // nothing, wrote the file" both returned 0 and both printed
            // gate=PASS, so a first run against a fresh golden path was
            // byte-indistinguishable from a verified one on the machine-readable
            // line. BLESSED is its own word. rc and the exit code stay 0.
            const char *gate_word = !any_gate    ? "none"
                                  : gate_rc      ? "FAIL"
                                  : gate_blessed ? "BLESSED" : "PASS";
            iigs_emit_status(st, any_gate ? gate_rc : 0, gate_word,
                             g_iigs_last_gsos_err, g_iigs_brk_count, scb, h,
                             computer->clock->get_cycles());
            if (SDL_getenv("A2GSPU_STRICT") && g_a2gspu_warnings) {
                printf("A2GSPU_STRICT: %d warning(s) during the run -> exit 3\n", g_a2gspu_warnings);
                exit(3);
            }
            if (any_gate) {
                printf("=== SPIKE COMPLETE (gate %s) ===\n", gate_word);
                exit(gate_rc);
            }
        } else {
            // ---- Non-IIgs (Apple II / II+ / IIe / IIc) spike verdict --------------
            //
            // Everything above is gated on the Mega II image, which exists only on
            // -p 5.  So on a IIe spike the golden, the asserts, the GSDIAG line AND
            // the gate-driven exit were all skipped: the run printed
            // "SPIKE E1: mmu_iigs/megaii base is NULL -- FAILED" and still exited 0.
            // A gate that cannot fail is not a gate, and a harness has nothing to
            // parse.  This is the II-family analogue.
            //
            // The IIe counterpart of the SHR window is the hi-res screen: hash HGR
            // page 1 ($2000-$3FFF) and page 2 ($4000-$5FFF).  Reads go through
            // probe_peek so they are observation-free (no $C0xx soft-switch side
            // effects) -- the emulated machine cannot tell it is being measured.
            //
            // A2GSPU_HGR_PAGE selects what the golden covers: 1, 2, or "both"
            // (default) -- a tile engine that renders to page 2 while showing page 1
            // wants to gate on the page it just drew.
            static uint8_t hgr[0x4000];
            uint32_t lo = 0x2000, hi = 0x5FFF;
            if (const char *pg = SDL_getenv("A2GSPU_HGR_PAGE")) {
                if (pg[0] == '1') { lo = 0x2000; hi = 0x3FFF; }
                else if (pg[0] == '2') { lo = 0x4000; hi = 0x5FFF; }
            }
            int nz = 0, nd = 0, lit = 0; uint8_t seen[256] = {0};
            uint64_t h = HOUSE_FNV_BASIS;
            size_t n = 0;
            for (uint32_t a = lo; a <= hi && n < sizeof(hgr); a++, n++) {
                uint8_t b = rail_mmu(computer) ? rail_mmu(computer)->probe_peek(a) : 0;
                hgr[n] = b;
                h = (h ^ b) * HOUSE_FNV_PRIME;
                if (b) nz++;
                // `lit` counts bytes with an actual PIXEL set (low 7 bits).  Bit 7
                // is the HGR palette selector, not a pixel, and a page cleared to
                // black in the high palette is filled with $80 -- so `nonzero`
                // alone reports a completely blank screen as ~94% "content".  That
                // reading is actively misleading, so report both.
                if (b & 0x7F) lit++;
                if (!seen[b]) { seen[b] = 1; nd++; }
            }
            printf("SPIKE HGR: $%04X-$%04X nonzero=%d lit=%d distinct=%d hash=%016llX\n",
                   lo, hi, nz, lit, nd, (unsigned long long)h);

            // DHGR peer golden (a2tile / a2engine): same as CTRL `dhgr-golden`.
            // Profiles: vram-raw (default, 16K AUX‖MAIN FNV) | 4dot (discrete RGB).
            // A2GSPU_DHGR_PAGE=1|2; A2GSPU_DHGR_GOLDEN_PROFILE=vram-raw|4dot.
            uint64_t dhgr_h = 0;
            bool dhgr_ok = false;
            a2dhgr::GoldenProfile dhgr_prof = a2dhgr::GoldenProfile::VramRaw;
            if (const char *pp = SDL_getenv("A2GSPU_DHGR_GOLDEN_PROFILE")) {
                a2dhgr::GoldenProfile tmp;
                if (a2dhgr::parse_golden_profile(pp, &tmp))
                    dhgr_prof = tmp;
                else
                    printf("SPIKE DHGR: unknown A2GSPU_DHGR_GOLDEN_PROFILE='%s' "
                           "(use vram-raw|4dot); keeping vram-raw\n", pp);
            }
            const char *dhgr_pname = a2dhgr::golden_profile_name(dhgr_prof);
            {
                uint32_t dbase = 0x2000u;
                if (const char *dp = SDL_getenv("A2GSPU_DHGR_PAGE")) {
                    if (dp[0] == '2') dbase = 0x4000u;
                }
                const uint8_t *flat = rail_video_base(computer);
                if (flat) {
                    const uint8_t *aux = flat + 0x10000 + dbase;
                    const uint8_t *mainb = flat + dbase;
                    uint64_t h_raw = a2dhgr::page_fnv64(aux, mainb);
                    uint64_t h_4dot = a2dhgr::page_rgb_fnv64(aux, mainb, 1);
                    dhgr_h = a2dhgr::page_hash(aux, mainb, dhgr_prof);
                    dhgr_ok = true;
                    printf("SPIKE DHGR: page@$%04X profile=vram-raw hash=%016llX\n",
                           (unsigned)dbase, (unsigned long long)h_raw);
                    printf("SPIKE DHGR: page@$%04X profile=4dot hash=%016llX\n",
                           (unsigned)dbase, (unsigned long long)h_4dot);
                    printf("SPIKE DHGR: gate profile=%s hash=%016llX\n",
                           dhgr_pname, (unsigned long long)dhgr_h);
                } else {
                    printf("SPIKE DHGR: no flat image (cannot hash AUX‖MAIN)\n");
                }
            }

            int gate_rc = 0; bool any_gate = false, gate_blessed = false;
            if (const char *dgf = SDL_getenv("A2GSPU_DHGR_GOLDEN")) {
                any_gate = true;
                if (!dhgr_ok) {
                    printf("DHGR GOLDEN: ERROR — no flat image for hash\n");
                    gate_rc = 1;
                } else {
                    bool b = false;
                    gate_rc |= a2dhgr::golden_gate_file(dgf, dhgr_h, "DHGR GOLDEN",
                                                         dhgr_pname, &b);
                    if (b) {
                        gate_blessed = true;
                        a2gspu_warn("DHGR GOLDEN", "blessed a missing golden; nothing was compared");
                    }
                }
            }
            if (const char *gf = SDL_getenv("A2GSPU_GOLDEN")) {
                any_gate = true;
                // The II-family twin of the IIgs block above, and it carried the
                // same two defects: a hand-rolled copy of golden_gate_file with
                // no "cannot open or create" branch (silent gate=PASS on an
                // unwritable path), and a bless that reported PASS for a
                // comparison that never happened.
                bool b = false;
                gate_rc |= a2dhgr::golden_gate_file(gf, h, "HGR GOLDEN", "hgr-page", &b);
                if (b) {
                    gate_blessed = true;
                    a2gspu_warn("HGR GOLDEN", "blessed a missing golden; nothing was compared");
                }
            }
            if (const char *eh = SDL_getenv("A2GSPU_EXPECT_HASH")) {
                // The inline form of GOLDEN: a hash pinned in the invocation
                // rather than in a file. A harness that differences two runs
                // can demand the shorter one reproduce before it subtracts.
                any_gate = true;
                const uint64_t want = strtoull(eh, nullptr, 16);
                const bool match = (want == h);
                printf("A2GSPU EXPECT_HASH: %s (cur=%016llX want=%016llX)\n",
                       match ? "MATCH" : "MISMATCH", (unsigned long long)h, (unsigned long long)want);
                if (!match) gate_rc = 1;
            }
            {
                // M9: the run-hash twin of EXPECT_HASH -- the same screen, the
                // same clock AND the same work (insns, stores).
                bool run_fail = false;
                if (a2gspu_expect_run_gate(h, computer->clock->get_cycles(), &run_fail)) {
                    any_gate = true;
                    if (run_fail) gate_rc = 1;
                }
            }
            if (const char *as = SDL_getenv("A2GSPU_ASSERT")) {
                // Same arm rule as the IIgs block above: an empty spec arms no
                // gate rather than passing one.
                if (a2gspu_assert_armed(as)) {
                    any_gate = true;
                    // peek:/nonzero/distinct are machine-independent; the SHR-only fields
                    // (scb_mode, idxN) are meaningless here and report as such.
                    gate_rc |= iigs_eval_asserts(as, computer->cpu, hgr, nz, nd);
                } else {
                    printf("IIGS ASSERT: A2GSPU_ASSERT is set but empty -- no gate armed (gate=none)\n");
                }
            }
            const char *st = (any_gate && gate_rc) ? "GATE_FAIL"
                           : g_iigs_brk_count       ? "CRASH_BRK"
                           : g_iigs_hang_detected   ? "HANG" : "OK";
            // scb is an SHR concept; report 0 on the II family rather than a fake mode.
            // gate=BLESSED is the third outcome: see the IIgs block above.
            const char *gate_word = !any_gate    ? "none"
                                  : gate_rc      ? "FAIL"
                                  : gate_blessed ? "BLESSED" : "PASS";
            iigs_emit_status(st, any_gate ? gate_rc : 0, gate_word,
                             0, g_iigs_brk_count, 0, h,
                             computer->clock->get_cycles());
            if (SDL_getenv("A2GSPU_STRICT") && g_a2gspu_warnings) {
                printf("A2GSPU_STRICT: %d warning(s) during the run -> exit 3\n", g_a2gspu_warnings);
                exit(3);
            }
            if (any_gate) {
                printf("=== SPIKE COMPLETE (gate %s) ===\n", gate_word);
                exit(gate_rc);
            }
        }
    }

    // WIZ5_WRITEBACK (wiz5_clean fork): opt-in save-experiment flush. Persists
    // the in-memory (possibly game-modified) disks to their FILES at spike end.
    // OFF by default (mutation-guard discipline) — use ONLY on writable scratch
    // images to capture an on-disk save. Suppressed when WIZ5_NO_WRITEBACK is set.
    // Canonical A2GSPU_* with the WIZ5_* originals as aliases: writing modified
    // disks back is not Pascal-specific. Suppression stays OR-ed across both
    // spellings, so setting either NO_WRITEBACK still wins over either WRITEBACK
    // -- a guard that can be bypassed by using the other name is not a guard.
    if (a2writeback::current().flush()) {
        for (const auto &dm : state->disks_to_mount) {
            storage_key_t k{}; k.slot = dm.slot; k.drive = dm.drive;
            k.partition = 0; k.subunit = 0;
            printf("WIZ5_WRITEBACK: flushing slot %d drive %d -> %s\n",
                   dm.slot, dm.drive, dm.filename.c_str());
            computer->mounts->unmount_media(k, SAVE_AND_UNMOUNT);
        }
    }

    printf("=== SPIKE COMPLETE ===\n");
}

/* ========================================================================
   SDL3 App Callback Entry Points
   ======================================================================== */
