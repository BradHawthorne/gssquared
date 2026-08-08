// a2gspu_manifest.hpp -- self-describing control surface for agentic AI.
//
// North star (Docs/AGENTIC_ORACLE.md): nothing is a black box; agents can
// discover capabilities, observation models, and contracts from the running
// process without reading C++.

#pragma once
#include <cstdio>
#include <cstring>

namespace a2manifest {

// One-line north-star for `oracle` CTRL verb (must fit a large ack).
inline const char *oracle_banner() {
    return
        "ORACLE gssquared a2gspu\n"
        "goal: no black boxes; maximal agent control+observability for closed-loop toolchains\n"
        "contracts:\n"
        "  cycles= ONLY valid as in-command deltas (run/step/run-until acks). "
        "NEVER subtract cycles across separate acks (can jump/go backwards).\n"
        "  colour= always named profile (pngc profile=4dot|mono). shot/SDL is human-only.\n"
        "  memory observe= probe_peek / flat-image paths; never toggle RAMRD to look.\n"
        "  snapshot= Class A (CPU+MMU+softswitches); devices self-heal — not full time travel.\n"
        "  dual-mode= no A2GSPU_* env => stock windowed emulator.\n"
        "discover: help | help <file> | manifest <file> | oracle\n"
        "docs: Docs/AGENTIC_ORACLE.md Docs/A2GSPU_RAILS.md Docs/DoubleHiRes.md\n";
}

struct Verb {
    const char *name;
    const char *kind;   // observe | manipulate | run | snapshot | meta | calibrate
    const char *synopsis;
    const char *model;  // observation/manipulation model id or "-"
};

// Keep sorted by name for stable manifests. Update when adding CTRL verbs.
inline const Verb *verbs(int *count) {
    static const Verb v[] = {
        {"assert", "observe", "assert peek:ADDR==VAL[;…] — probe_peek checks; status=PASS|FAIL", "memory-peek"},
        {"audio", "observe", "audio [status]|doc|sfx|on|off|reset — generated sample streams per SOURCE (doc = Ensoniq, sfx = drive sounds; never summed): count, non-silent, min/max, average level, and the same broken out per channel (ch=/l_*/r_*) so a dropped side is visible", "sample-stream"},
        {"boot", "run", "boot <frames> — run N frames from power-on path", "run-frames"},
        {"bp", "manipulate", "bp [addr|off] — interactive breakpoint; bare reports state. A hit halts; run/step/resume continue past it", "force-control"},
        {"callstream", "trace", "callstream on <file>|off|status — LIVE NDJSON toolbox stream", "tool_locator"},
        {"cov", "observe", "cov [status] | cov on <LO-HI|BANK:LO-HI> | cov off | cov reset | cov write <file> — execution coverage", "coverage"},
        {"cpu", "observe", "cpu — PBR:PC A X Y SP P E HALT STP RDY KBD AKD", "cpu-state"},
        {"cycles", "observe", "cycles — guest cycle counter NOW (absolute). "
         "Deltas only valid inside run/step/run-until acks.", "cycles-absolute-warn"},
        {"devstate", "observe", "devstate [name|all <file>] — per-device state the emulator already computes (ADB regs, disk head, mouse clamp, DOC, memory map); bare lists handlers", "device-debug"},
        {"dhgr-calibrate", "calibrate", "dhgr-calibrate — solid LORES 4-dot self-test", "colour-4dot"},
        {"dhgr-export", "calibrate", "dhgr-export <dir> — write GSDHGR4D+GSNTSC01 for a2tile", "profile-export"},
        {"dhgr-golden", "calibrate",
         "dhgr-golden <file> [page] [vram-raw|4dot] [bless] — FNV page golden", "vram-raw|4dot"},
        {"dis", "observe", "dis <addr> <n> <file> — disassemble via MMU", "memory-disasm"},
        {"help", "meta", "help [file] — list CTRL verbs (optional write full catalog)", "discover"},
        {"hgr", "observe", "hgr — HGR-related dump helper", "bits-mono"},
        {"holdkey", "manipulate", "holdkey <hex> — sticky IIgs hold key", "inject-key"},
        {"iolog", "observe", "iolog [on [cap]|off|dump <f>|reset] — $C0xx R/W ring", "io-trace"},
        {"itrace", "trace", "itrace from <pc>|now|n <c>|clear|status — LIVE per-instr trace", "execute"},
        {"key", "manipulate", "key <hex> [maxf] — inject key and wait for consume receipt", "inject-key"},
        {"keys", "manipulate", "keys <str> — paste buffer (no consume proof)", "inject-key"},
        {"load", "manipulate", "load <addr> <file> — splice binary into RAM via mmu->write", "inject-ram"},
        {"manifest", "meta", "manifest <file> — full capability dump for agents", "discover"},
        {"mount", "manipulate", "mount sXdY <path> — runtime media swap", "inject-media"},
        {"oracle", "meta", "oracle — north-star contracts (cycles/colour/snapshot)", "discover"},
        {"paddle", "manipulate", "paddle <0-3> <0-255> | paddle off — analogue paddle; seeds the 558 decay so PREAD counts the value back", "force-control"},
        {"verify", "observe", "verify <addr|BANK:addr> <len> <file> — compare guest memory to a host file in the emulator and answer with the verdict: identical, or the first differing offset and both bytes", "compare-in-place"},
        {"joymode", "manipulate", "joymode [status]|gamepad|mouse|atari — controller shape at $C061-$C063; reports the Joyport post-reset suspend window and whether it is still open", "force-control"},
        {"pbutton", "manipulate", "pbutton <0-2> <0|1> — game switch PB0-PB2 ($C061-$C063)", "force-control"},
        {"png", "observe", "png <file> [page] [scale] [auto|hgr|dhgr] — mono dots from RAM", "bits-mono"},
        {"pngc", "observe", "pngc <file> [page] [scale] [4dot|mono] — named colour PNG", "colour-4dot"},
        {"poke", "manipulate", "poke [bank:]<addr> <hexbytes...> — write bytes (labeled splice)", "inject-ram"},
        {"shr", "observe", "shr <file> [scale] — IIgs super hi-res as PNG; 320 or 640 per-line by SCB", "shr-linear"},
        {"press", "manipulate", "press <hex> [hold] — physical key edge (no consume proof)", "inject-key"},
        {"quit", "run", "quit — end CTRL session", "-"},
        {"rail", "meta", "rail list|env-only — discover env rails still launch-only (L4 gap)", "discover"},
        {"read", "observe", "read <addr> <len> <file> — probe_peek dump (side-effect-free)", "memory-peek"},
        {"reset", "manipulate", "reset — guest reset path", "force-control"},
        {"restore", "snapshot", "restore <file> — Class A snapshot load", "time-travel-A"},
        {"resume", "manipulate", "resume — clear a user halt (breakpoint/save-at) so the CPU advances again; refuses to paper over a jammed CPU", "force-control"},
        {"run", "run", "run <frames> — advance N frames; ack includes cycles= IN-COMMAND", "cycles-in-cmd"},
        {"run-until", "run", "run-until <addr> [max_instr] — stop at PC; cycles= IN-COMMAND", "cycles-in-cmd"},
        {"save", "snapshot", "save <file> — Class A CPU+MMU snapshot", "time-travel-A"},
        {"screen", "observe", "screen — screen/text helper", "text-page"},
        {"setreg", "manipulate", "setreg <reg> <hex> — force CPU register", "force-control"},
        {"shot", "observe", "shot <file> — SDL backbuffer BMP (HUMAN eyeball; not art oracle)", "sdl-human"},
        {"speaker", "observe", "speaker — $C030 toggle count and c14m stamp of the last one; sound is WHEN, not whether", "speaker-toggles"},
        {"stack", "observe", "stack [n] — bytes above SP bank0", "cpu-state"},
        {"step", "run", "step [n] — execute N instructions; cycles= IN-COMMAND", "cycles-in-cmd"},
        {"tbtrace", "trace", "tbtrace on|off|status|bank <hex>|all — LIVE toolbox/GSOS trace", "tool_locator"},
        {"tbuf", "trace", "tbuf [status]|on|off|clear|dump <file> [n] — CPU trace ring: per-instruction regs, effective address and R/W", "instr-ring"},
        {"text", "observe", "text <file> — text page main+aux dump", "text-page"},
        {"valtrap", "break", "valtrap set <hex>[:w]|clear|status — LIVE value-provenance trap", "bus_write"},
        {"vid", "observe", "vid — TEXT/MIXED/PAGE2/HIRES/80COL/80STORE/ALTCHAR/DHIRES", "softswitch"},
        {"vram", "observe", "vram <file> [page] — 8K AUX + 8K MAIN raw", "vram-raw"},
        {"watch", "observe",
         "watch add|list|sample|clear (soft peek) | watch bus add bank:lo-hi|list|clear (LIVE bus)",
         "memory-peek"},
    };
    *count = (int)(sizeof v / sizeof v[0]);
    return v;
}

inline void write_help_short(FILE *f) {
    int n = 0;
    const Verb *v = verbs(&n);
    fprintf(f, "CTRL verbs (%d). Contracts: oracle | full: help <file> | manifest <file>\n", n);
    for (int i = 0; i < n; i++)
        fprintf(f, "  %-14s [%s] %s\n", v[i].name, v[i].kind, v[i].synopsis);
}

inline void write_manifest(FILE *f) {
    fprintf(f, "# gssquared a2gspu capability manifest\n");
    fprintf(f, "version: 1\n");
    fprintf(f, "goal: no-black-box maximal agent control for closed-loop toolchains\n");
    fprintf(f, "\n## contracts\n");
    fprintf(f, "cycles_valid: in-command-only\n");
    fprintf(f, "cycles_invalid: cross-ack-subtraction\n");
    fprintf(f, "colour_art_oracle: pngc profile=4dot\n");
    fprintf(f, "colour_not_oracle: shot sdl\n");
    fprintf(f, "bits_oracle: png|vram\n");
    fprintf(f, "memory_observe: probe_peek\n");
    fprintf(f, "snapshot_class: A-cpu-mmu-softswitches\n");
    fprintf(f, "dual_mode: no-A2GSPU-env-is-stock-emulator\n");
    fprintf(f, "\n## observation_models\n");
    fprintf(f, "bits-mono,colour-4dot,colour-ntsc560,vram-raw,text-page,softswitch,");
    fprintf(f, "cpu-state,cycles-in-cmd,cycles-absolute-warn,shr-window,memory-peek,sdl-human\n");
    fprintf(f, "\n## env_rail_groups (see Docs/A2GSPU_RAILS.md)\n");
    fprintf(f, "run: SPIKE_FRAMES SPIKE_KEYS RUN_BIN LOAD LOAD_BOOT CPUTEST MMUTEST HANG_THRESHOLD\n");
    fprintf(f, "observe: DUMP TEXTDUMP TEXT40 VIDEOSUM VIDEOMAP SYMBOLS BUS_DUMP\n");
    fprintf(f, "trace: TBTRACE ITRACE CALLTRACE CALLSTREAM LOADTRACE LCTRACE MODETRACE INTLOG TAP\n");
    fprintf(f, "break: BREAK WATCH VALTRAP PCTRAP CONDTRAP STACKTRAP STACKWATCH SAVE_AT RETGUARD\n");
    fprintf(f, "manipulate: POKE FORCE_KEY\n");
    fprintf(f, "snapshot: SNAP_SAVE SNAP_LOAD RESTORE\n");
    fprintf(f, "assert: ASSERT GOLDEN\n");
    fprintf(f, "determinism: SEED FAKETIME RAMDISK\n");
    fprintf(f, "ctrl: CTRL CTRL_TIMEOUT\n");
    fprintf(f, "\n## env_only_rationale\n");
    fprintf(f, "# High-volume traces arm at launch; CTRL parity is the roadmap (AGENTIC_ORACLE L4).\n");
    fprintf(f, "env-only-today: GOLDEN(SPIKE SHR/HGR) DHGR_GOLDEN(SPIKE) SPIKE_* ASSERT(env)\n");
    fprintf(f, "ctrl-callstream: LIVE NDJSON call stream mid-session\n");
    fprintf(f, "ctrl-assert: peek clauses (live)\n");
    fprintf(f, "ctrl-watch-bus: LIVE arm g_watch ranges mid-session (path=bus_write)\n");
    fprintf(f, "ctrl-watch-soft: probe_peek sample across run/step\n");
    fprintf(f, "ctrl-valtrap: LIVE value trap mid-session\n");
    fprintf(f, "ctrl-itrace: LIVE per-instruction trace mid-session\n");
    fprintf(f, "ctrl-tbtrace: LIVE toolbox/GSOS trace mid-session\n");
    fprintf(f, "ctrl-dhgr-golden: vram-raw|4dot FNV (live; same as A2GSPU_DHGR_GOLDEN)\n");
    fprintf(f, "spike-dhgr-golden: A2GSPU_DHGR_GOLDEN + PAGE + GOLDEN_PROFILE (IIe batch gate)\n");
    fprintf(f, "\n## ctrl_verbs\n");
    int n = 0;
    const Verb *v = verbs(&n);
    for (int i = 0; i < n; i++) {
        fprintf(f, "%s\tkind=%s\tmodel=%s\t%s\n",
                v[i].name, v[i].kind, v[i].model, v[i].synopsis);
    }
    fprintf(f, "\n## docs\n");
    fprintf(f, "Docs/AGENTIC_ORACLE.md\n");
    fprintf(f, "Docs/A2GSPU_RAILS.md\n");
    fprintf(f, "Docs/INSTRUMENTATION_AUDIT.md\n");
    fprintf(f, "Docs/DoubleHiRes.md\n");
    fprintf(f, "Docs/CLOSED_LOOP_INTEGRATION.md\n");
}

// Env rails that are still launch-only (not live CTRL). Agents must relaunch
// with these set — listed here so they are not a black box.
inline void write_env_only_rails(FILE *f) {
    fprintf(f, "# env-only rails (set before process start; not live CTRL)\n");
    fprintf(f, "# See Docs/A2GSPU_RAILS.md for full semantics.\n");
    fprintf(f, "A2GSPU_SPIKE_FRAMES\tbatch headless run\n");
    fprintf(f, "A2GSPU_SPIKE_KEYS\tkey inject at spike\n");
    fprintf(f, "A2GSPU_RUN_BIN\tinject+exec flat binary (IIgs default $7E0000)\n");
    fprintf(f, "A2GSPU_LOAD\tmulti-file RAM splice file@HEX or HEX@file (;/, sep); no PC change\n");
    fprintf(f, "A2GSPU_LOAD_BOOT\tframes before A2GSPU_LOAD (default 0)\n");
    fprintf(f, "A2GSPU_VRAM_LOAD\t16K aux||main vram-raw into page (a2tile paint offline)\n");
    fprintf(f, "A2GSPU_VRAM_PAGE\t1|2 for VRAM_LOAD (default 1)\n");
    fprintf(f, "A2GSPU_WATCH\tbus write-watch ranges (provenance)\n");
    fprintf(f, "A2GSPU_WATCH_READ\talso watch reads\n");
    fprintf(f, "A2GSPU_WATCH_CHANGE\tlog only value changes\n");
    fprintf(f, "A2GSPU_VALTRAP\twho stored value V\n");
    fprintf(f, "A2GSPU_ITRACE_FROM\tper-instruction trace arm PC\n");
    fprintf(f, "A2GSPU_TBTRACE\ttoolbox/GSOS dispatch trace\n");
    fprintf(f, "A2GSPU_CALLSTREAM\tNDJSON call sequence\n");
    fprintf(f, "A2GSPU_ASSERT\tspike-end assert clauses\n");
    fprintf(f, "A2GSPU_GOLDEN\tSHR window (IIgs) or HGR main (IIe) golden hash\n");
    fprintf(f, "A2GSPU_DHGR_GOLDEN\tIIe SPIKE gate: FNV hash of DHGR page\n");
    fprintf(f, "A2GSPU_DHGR_PAGE\t1|2 which HGR page for DHGR golden (default 1)\n");
    fprintf(f, "A2GSPU_DHGR_GOLDEN_PROFILE\tvram-raw (bits) | 4dot (discrete RGB); default vram-raw\n");
    fprintf(f, "A2GSPU_HGR_PAGE\t1|2|both for main-only HGR SPIKE hash\n");
    fprintf(f, "A2GSPU_SEED\tdeterministic cold boot\n");
    fprintf(f, "A2GSPU_FAKETIME\tfrozen RTC\n");
    fprintf(f, "A2GSPU_POKE\tlaunch-time PC poke (use CTRL poke for live)\n");
    fprintf(f, "A2GSPU_BREAK\theadless PC breakpoint\n");
    fprintf(f, "A2GSPU_TAP\tgeneric PC hit NDJSON tap\n");
    fprintf(f, "# CTRL live substitutes where available:\n");
    fprintf(f, "#   soft watch -> watch add peek:ADDR (not bus WATCH)\n");
    fprintf(f, "#   assert -> assert peek:ADDR==VAL\n");
    fprintf(f, "#   dhgr-golden -> same hash as A2GSPU_DHGR_GOLDEN (live mid-session)\n");
    fprintf(f, "#   load/poke/setreg -> live inject-ram\n");
    fprintf(f, "#   A2GSPU_LOAD -> SPIKE multi-file bank inject (IIe tiles.bin@6000)\n");
}

inline void format_rail_ack(char *buf, size_t bufsz) {
    snprintf(buf, bufsz,
             "status=OK rail env-only listed in Docs/A2GSPU_RAILS.md | "
             "rail env-only <file> writes catalog | "
             "live: watch soft|bus valtrap itrace tbtrace callstream assert load poke dhgr-golden | "
             "not-yet-live: GOLDEN(SPIKE SHR) ASSERT(env) SPIKE_*");
}

// Fill buf with short help (truncated safely). Returns bytes written conceptually.
inline void format_help_ack(char *buf, size_t bufsz) {
    int n = 0;
    const Verb *v = verbs(&n);
    size_t off = 0;
    int m = snprintf(buf + off, bufsz > off ? bufsz - off : 0,
                     "help verbs=%d | oracle | help <file> | manifest <file> |", n);
    if (m > 0) off += (size_t)m;
    for (int i = 0; i < n && off + 16 < bufsz; i++) {
        m = snprintf(buf + off, bufsz - off, " %s", v[i].name);
        if (m < 0) break;
        off += (size_t)m;
    }
}

} // namespace a2manifest
