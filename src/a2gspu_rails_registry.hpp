// a2gspu_rails_registry.hpp -- the env-rail half of the self-describing surface.
//
// AGENTS.md rule 6 requires new verbs *and rails* to be discoverable via
// `help` / `manifest`. Verbs were: a2gspu_manifest.hpp enumerates 124 of them
// from the running process, and the `help` verb's own rationale is "enumerate
// verbs from the RUNNING process rather than from documentation that may have
// drifted".
//
// Rails were not. They lived only in Docs/A2GSPU_RAILS.md, and that catalogue
// had drifted to describe 96 of the 128 env rails the code actually reads -- so
// a client discovering this fork cold could enumerate every verb from the
// process, then had to read markdown (or C++) to learn that MEMVU_LOADVIS,
// A2GSPU_COVERAGE or the whole A2GSPU_TAP_* family existed at all. That is the
// black box the oracle exists to remove, sitting inside the oracle.
//
// This table is the SOURCE OF TRUTH for rails, not a copy of the document.
// tools/a2rail/railparity.py fails if any getenv() in src/ has no entry here,
// or if an entry names a rail no code reads -- so the surface cannot silently
// drift again in either direction.
//
// Dual-mode is unaffected: static const data plus pure functions. It arms
// nothing and reads no environment.

#pragma once
#include <cstdio>
#include <cstring>

namespace a2rails {

// family: run | observe | trace | break | manipulate | snapshot | assert
//       | device | determinism | meta | calibrate | tap | memvu | wiz5
//       | alias | ctrl
struct Rail {
    const char *name;
    const char *family;
    const char *synopsis;
};

inline const Rail *rails(int &n) {
    static const Rail r[] = {
    {"A2GSPU_ASSERT",              "assert",      ";/,-separated name OP value checks (OP == != <= >= < >); the spike exits non-zero if any fails. Fields: scb_mode, qd_carry/qd_err, lt_carry, cs_carry/pr_carry, gsos_err, nonzero/distinct, idxN (N=0..15), and peek:HHHHHH (observation-free 24-bit byte read)"},
    {"A2GSPU_BREAK",               "break",       "headless breakpoint at a 24-bit PC"},
    {"A2GSPU_BRKDUMP",             "break",       "dump CPU registers on BRK (crash under GS/OS) + a SPIKE-END register dump"},
    {"A2GSPU_BRKMEM",              "break",       "on BRK, dump the recent-PC history + bytes around the crash PC and stacked return"},
    {"A2GSPU_BUS_DUMP",            "observe",     "~BUS cycle dump (source = slot_bus) for the gssquared->card host gate"},
    {"A2GSPU_BUS_STREAM",          "observe",     "live ~BUS slot-bus CDC streaming (-> PB INGRESS_SOURCE=0)"},
    {"A2GSPU_CALLSTREAM",          "trace",       "symbol-free NDJSON of the toolbox/GS-OS call sequence (for tools/gdiff/calldiff.py first-divergence diffing)"},
    {"A2GSPU_CALLTRACE",           "observe",     "<hexPC>`"},
    {"A2GSPU_CALLTRACE_N",         "trace",       "cap CALLTRACE entries"},
    {"A2GSPU_CALLTRACE_SKIP",      "trace",       "skip the first n calls before logging"},
    {"A2GSPU_CONDTRAP",            "break",       "conditional flag-provenance trap: at PC, when flag f(c/z/i/d/x/m/v/n)==v, report the PC that last changed that flag"},
    {"A2GSPU_CONDTRAP_MAX",        "break",       "CONDTRAP hit cap"},
    {"A2GSPU_COPILOT",             "observe",     "windowed, read-only telemetry rail; human input stays active"},
    {"A2GSPU_COVERAGE",            "observe",     "LO-HI or BANK:LO-HI -- per-byte execution bitmap over that range, written at spike end. Feeds deasmiigs --coverage so the code/data split is a measurement, not a heuristic."},
    {"A2GSPU_COVERAGE_OUT",        "observe",     "file the COVERAGE bitmap is flushed to. Written BEFORE the gate block, which exits on any gate and would otherwise discard the run."},
    {"A2GSPU_CPUTEST",             "run",         "short-circuit the spike: run one CPU micro-test case, print === CPUTEST COMPLETE (PASS/FAIL) ===, exit with its rc"},
    {"A2GSPU_CTRL",                "ctrl",        "enable the interactive stepping rail: 20 Hz poll of <dir>/cmd.N, atomic <dir>/ack.N reply; replaces the fixed-frame spike. Also forces headless when set"},
    {"A2GSPU_CTRL_TIMEOUT",        "ctrl",        "idle timeout in seconds before an orphaned session quits (default 300)"},
    {"A2GSPU_DHGR_GOLDEN",         "observe",     "SPIKE-end IIe DHGR gate (bless if missing; MATCH/DIFF exit)"},
    {"A2GSPU_DHGR_GOLDEN_PROFILE", "observe",     "vram-raw (default) or 4dot -- must match how the golden file was blessed"},
    {"A2GSPU_DHGR_PAGE",           "observe",     "2`"},
    {"A2GSPU_DUMP",                "observe",     "hexdump memory ranges (hex). $E0/$E1 read the Mega II image directly (side-effect-free); other banks via the MMU"},
    {"A2GSPU_ERRHOOK",             "trace",       "surface carry-set GS/OS dispatch errors (<-- SYSTEM/LOADER ERROR)"},
    {"A2GSPU_FAKETIME",            "determinism", "freeze the RTC at that instant (bypasses localtime/timezone) so RTC-derived OS state is run-to-run stable"},
    {"A2GSPU_FORCE_KEY",           "manipulate",  "force the ADB $C000 keyboard latch to return this key (drives a $C000-polling guest headlessly)"},
    {"A2GSPU_GOLDEN",              "assert",      "hash the SHR window ($E1:2000-9FFF, house FNV-1a-64); if the file exists compare (DIFF => gate fail; corrupt/empty file => hard fail); if absent, bless it"},
    {"A2GSPU_HANG_THRESHOLD",      "run",         "frame budget for the no-BRK degenerate-loop hang detector (status=HANG instead of burning frames)"},
    {"A2GSPU_HGR_PAGE",            "observe",     "1|2 -- which HGR page the dump/golden gate reads, for a workload that gates on the page it just drew."},
    {"A2GSPU_INTLOG",              "trace",       "interrupt-entry logger (IRQ/BRK/COP entries)"},
    {"A2GSPU_ITRACE_FRAME",        "trace",       "per-instruction trace, armed at the start of headless frame N"},
    {"A2GSPU_ITRACE_FROM",         "trace",       "per-instruction trace, armed when PC first hits this 24-bit address"},
    {"A2GSPU_ITRACE_HI",           "trace",       "high bound (hex, 24-bit) of the ITRACE PC-range window; setting it enables ITRACE."},
    {"A2GSPU_ITRACE_LO",           "trace",       "low bound (hex, 24-bit) of the ITRACE PC-range window; setting it enables ITRACE."},
    {"A2GSPU_ITRACE_N",            "trace",       "cap logged instructions (default 256)"},
    {"A2GSPU_ITRACE_OUT",          "trace",       "write ITRACE to a file instead of stderr"},
    {"A2GSPU_ITRACE_REARM",        "trace",       "re-arm ITRACE this many times (windowed re-capture)"},
    {"A2GSPU_LCTRACE",             "trace",       "Language-Card soft-switch access log"},
    {"A2GSPU_LOAD",                "run",         "multi-file deliberate RAM splice (IIe+IIgs); no PC change. Also accepts HEX@file. Separators: ; or ,. Hard-fails SPIKE on any error."},
    {"A2GSPU_LOADTRACE",           "trace",       "segment-overlay tracker: log contiguous write-bursts (segment loads) into the region + when they overlay prior loads"},
    {"A2GSPU_LOADTRACE_MIN",       "trace",       "minimum burst length counted as a load"},
    {"A2GSPU_LOAD_BOOT",           "run",         "frames before A2GSPU_LOAD (default 0)"},
    {"A2GSPU_MILESTONES",          "trace",       "load a boot-milestone list; print a reached / NOT-REACHED table at spike end"},
    {"A2GSPU_MMUTEST",             "run",         "short-circuit the spike: run one MMU+video (FPI mapping / SHR decode) micro-test, exit with its rc"},
    {"A2GSPU_MODETRACE",           "trace",       "CPU mode-transition events (XCE / REP / SEP -> e/M/X changes)"},
    {"A2GSPU_MOUSETRACE",          "trace",       "append each ADB mouse injection to kg_mouse_trace.txt -- proves a mouse delta reached the micro, not just the host."},
    {"A2GSPU_OUT_DIR",             "meta",        "directory prefix applied to rail output filenames. An absolute path (or Windows drive letter) is honoured verbatim, so an explicit caller-chosen path is never rewritten."},
    {"A2GSPU_PCTRAP",              "break",       "one-shot dump of registers + recent-PC ring on first entry to a PC range"},
    {"A2GSPU_POKE",                "manipulate",  "one-shot deliberate splice at a PC. Actions: A/X/Y/S/D/P/DBR/PB=<hex> (reg/flag), PC=<hex> (force-branch), M<hex24>=<hexbyte> (poke mem), RTS/RTL (force-return), SKIP=<n> (skip bytes)"},
    {"A2GSPU_POKE_NTH",            "manipulate",  "fire the POKE on the Nth hit of the PC (default 1)"},
    {"A2GSPU_RAMDISK",             "device",      "serve a mounted image from a copy-on-write RAM buffer; writes never touch the host file"},
    {"A2GSPU_RESTORE",             "snapshot",    "alias for SNAP_LOAD (load-at-start)"},
    {"A2GSPU_RETGUARD",            "observe",     "strict`"},
    {"A2GSPU_ROM",                 "observe",     "ROM image path for the lazy-loaded ROM-vs-kernel PC classifier (default resources/roms/apple2gs/main.rom)"},
    {"A2GSPU_ROM_SYMBOLS",         "observe",     "name ROM-resident PCs (else <ROM>)"},
    {"A2GSPU_RUN_BIN",             "run",         "after boot frames, inject a flat binary into IIgs RAM (default load/exec $7E0000), reset the traces to isolate it, set PC to it"},
    {"A2GSPU_RUN_BOOT",            "run",         "boot frames to run before RUN_BIN injection (default spike_frames/2)"},
    {"A2GSPU_SAVE_AT",             "break",       "snapshot + halt at the first hit of an arbitrary 24-bit PC (a breakpoint that also checkpoints)"},
    {"A2GSPU_SEED",                "determinism", "seed the cold-boot SP entropy (srand) so a from-scratch spike is byte-reproducible (warm restores overwrite SP anyway)"},
    {"A2GSPU_SERIAL_DEBUG",        "device",      "verbose real-card serial/reconnect logging"},
    {"A2GSPU_SERIAL_PORT",         "device",      "override the real-card CDC probe port (default tries COM10 first)"},
    {"A2GSPU_SNAP_HI",             "observe",     "high bound of the SNAP region logger."},
    {"A2GSPU_SNAP_LO",             "observe",     "low bound of the SNAP region logger; with SNAP_HI and SNAP_PCS, dumps [LO,HI] at up to 8 trigger PCs."},
    {"A2GSPU_SNAP_LOAD",           "snapshot",    "restore that snapshot at start and skip the ~1200-frame boot (the frame loops self-heal excluded device/scanner/clock state)"},
    {"A2GSPU_SNAP_MAX",            "observe",     "cap on SNAP records emitted."},
    {"A2GSPU_SNAP_OUT",            "trace",       "destination file for the SNAP region-logger (requires LO/HI + PCS)"},
    {"A2GSPU_SNAP_PCS",            "trace",       "up to 8 trigger PCs at which to dump the SNAP window"},
    {"A2GSPU_SNAP_SAVE",           "snapshot",    "at spike end, save CPU regs + full MMU snapshot (128K + page tables + soft-switch state) + trailing sentinel"},
    {"A2GSPU_SNAP_ZP",             "observe",     "zero-page pointer correlation key, recorded as an annotation -- the emitter keys on PC, so this labels rather than triggers."},
    {"A2GSPU_SPIKE_FRAMES",        "run",         "run N deterministic frames headless (with -n), then evaluate the dump/assert/golden gates and exit. The master headless switch."},
    {"A2GSPU_SPIKE_KEYS",          "run",         "inject keystrokes via the keyboard paste buffer (\\n->\\r), paced by the guest's own $C000 polls"},
    {"A2GSPU_SPIKE_KEYS_AT",       "run",         "frame at which SPIKE_KEYS is injected (default spike_frames/2)"},
    {"A2GSPU_SP_NDEV",             "device",      "override the SmartPort device count (A/B faithfulness; 32 reproduces pre-fix enumeration)"},
    {"A2GSPU_STACKTRAP",           "break",       "one-shot dump when S first enters the range"},
    {"A2GSPU_STACKWATCH",          "observe",     "\"LO-HI\"`"},
    {"A2GSPU_STACKWATCH_JUMP",     "break",       "imbalance threshold (single-step S delta that trips STACKWATCH)"},
    {"A2GSPU_STOP_ON_FAULT",       "break",       "halt the spike on BRK/COP/SysFail instead of burning remaining frames"},
    {"A2GSPU_SYMBOLS",             "observe",     "load a symbol table (annotate trace/PCs with names)"},
    {"A2GSPU_SYM_BASE",            "observe",     "pin the symbol-relocation base (disables auto-inference)"},
    {"A2GSPU_SYM_SUSPECT",         "observe",     "append [SUSPECT reused] to any symbol whose name resolves at >1 address (advisory only)"},
    {"A2GSPU_TAP",                 "tap",         "generic title-agnostic call tap (src/generic_tap.hpp, from the harvested wiz5 taps): NDJSON record on each hit of the watched PC(s)"},
    {"A2GSPU_TAP_CYC_PER_MS",      "tap",         "cycles per millisecond used to convert tap timestamps; cyc_per_sec derives from it unless overridden."},
    {"A2GSPU_TAP_CYC_PER_SEC",     "tap",         "cycles per second for tap timestamps; overrides the value derived from CYC_PER_MS."},
    {"A2GSPU_TAP_DUMP_EVERY",      "tap",         "seconds between generic-tap session dumps."},
    {"A2GSPU_TAP_DUMP_ON",         "tap",         "LABEL[:MIN] -- dump trigger for the generic tap session."},
    {"A2GSPU_TAP_MAX",             "tap",         "cap on generic-tap records."},
    {"A2GSPU_TAP_PCS",             "tap",         "PC list the generic tap samples on."},
    {"A2GSPU_TAP_PTRS",            "tap",         "tap tuning: extra ZP-pointer register fields, which PCs, hit cap, dump-on-op / dump cadence, ZP-stream cadence, cycle-rate calibration"},
    {"A2GSPU_TAP_SESSION",         "tap",         "periodic capture session: ZP stream + $0400-BFFF RAM / $2000-5FFF HGR + soft-switch .meta + text-page snapshots to <dir>"},
    {"A2GSPU_TAP_ZP_EVERY",        "tap",         "seconds between generic-tap zero-page dumps."},
    {"A2GSPU_TBTRACE",             "trace",       "trace Tool Locator ($E10000) + GS/OS class-0/1 dispatch ($E100A8/$E100B0): call name on entry, carry+A on return"},
    {"A2GSPU_TBTRACE_BANK",        "trace",       "scope TBTRACE to a single bank"},
    {"A2GSPU_TEXT40",              "observe",     "at spike end, ASCII-decode the 40-col text page ($E0:0400-07FF) to stdout"},
    {"A2GSPU_TEXTDUMP",            "observe",     "dump text page 1 (2KB): main $0400-07FF via probe_peek (no $C0xx side effects) + aux $0400-07FF (80-col even columns)"},
    {"A2GSPU_TRACE_EXT",           "trace",       "append DBR/DP/cycle-delta/scanline columns to 65816 trace lines"},
    {"A2GSPU_TRACE_FROM",          "trace",       "arm tracing only once full_pc first hits this 24-bit PC"},
    {"A2GSPU_TRAPDUMP",            "break",       "extra memory region (hex) dumped by PCTRAP (default 48 bytes)"},
    {"A2GSPU_VALTRAP",             "break",       "value-provenance store trap: bind the PC that stores VALUE into memory (LE reassembled across consecutive bytes; w=1-4, default 3). \"Who wrote value V\""},
    {"A2GSPU_VALTRAP_MAX",         "break",       "VALTRAP hit cap (default 64)"},
    {"A2GSPU_VIDEOMAP",            "observe",     "a 40x25 ASCII dominant-index map of the Super Hi-Res window (SEE layout headless)"},
    {"A2GSPU_VIDEOSUM",            "observe",     "per-line SCB (320/640) + palette histogram (RGB888) + mode-correct pixel-index histogram"},
    {"A2GSPU_VRAM_LOAD",           "run",         "inject 16K aux||main (profile=vram-raw) into HGR page flat image -- a2tile offline paint path"},
    {"A2GSPU_VRAM_PAGE",           "observe",     "2`"},
    {"A2GSPU_WATCH",               "break",       "write-watchpoint on address range(s): log each write with writer PC/S/D/DBR (\"who wrote address X\"). Warns on a bare 24-bit addr (missing :) that would never fire"},
    {"A2GSPU_WATCH_CHANGE",        "break",       "log only when the value differs from the last seen for that range"},
    {"A2GSPU_WATCH_IPC_ZP",        "break",       "generic interpreter-IP attribution: makes the NDJSON ipc field read a 2-byte LE ZP pointer (observation-free) -- attributes a store to the interpreter's segment. Default-off (ipc=0). For UCSD p-code (Wizardry) set 9E"},
    {"A2GSPU_WATCH_MAX",           "break",       "WATCH hit cap (default 256; 0 = unlimited)"},
    {"A2GSPU_WATCH_OUT",           "break",       "emit WATCH as NDJSON to a file instead of stdout (adds ts frame-stamp + ipc fields)"},
    {"A2GSPU_WATCH_READ",          "break",       "also watch reads of the WATCH ranges"},
    {"MEMVU_BLAME",                "memvu",       "[=topN] attribute externally-visible traffic to the code that generates it. Refuses loudly if its allocation fails rather than reporting an empty ranking."},
    {"MEMVU_IREUSE",               "memvu",       "instruction-line reuse against a direct-mapped model, reporting hit / miss / modemiss, plus the workload's instruction footprint. Both parameters must be powers of two; a bad geometry is REFUSED, not rounded. =1 takes the 512x64 default"},
    {"MEMVU_LOADVIS",              "memvu",       "load-visibility accounting -- the read-side counterpart to STOREVIS. Either rail arms the shared classifier; the model is told whether an MMU can shadow rather than inferring a zero."},
    {"MEMVU_OPMIX",                "memvu",       "executed-opcode histogram, split by CPU mode context (E, M-width, X-width), plus hand-derived group totals (branch / RMW / stack / call-return / block-move / mode-switch) and the raw per-opcode counts"},
    {"MEMVU_SEAM",                 "memvu",       "a MODEL, not a counter -- cost of routing part of a workload's memory traffic to the machine. Parameters are mandatory; an incomplete set is REFUSED. Output is an upper bound"},
    {"MEMVU_STOREVIS",             "memvu",       "store-visibility accounting. Classifies every CPU store as DEVICE ($Cxxx in banks $00/$01/$E0/$E1), SLOWSIDE (direct Mega II $E0/$E1), SHADOWED (the machine mirrored it per its own $C035 configuration) or PRIVATE, and prints one summary line at spike end"},
    {"MEMVU_STOREVIS_BANKS",       "memvu",       "add the per-bank stores/visible breakdown (one line per bank that saw traffic)"},
    {"MEMVU_WORKSET",              "memvu",       "direct-page access count (exact, at the two direct-page data funnels), how many distinct direct pages are live, the D-change count, and the stack pointer's observed range and page histogram"},
    {"WIZ5_CSP6_OUT",              "wiz5",        "PC trap capturing CSP6 entries to a file."},
    {"WIZ5_DISKWR_OUT",            "wiz5",        "PC trap capturing disk block writes to a file."},
    {"WIZ5_DUMP_EVERY",            "alias",       "legacy alias for A2GSPU_TAP_DUMP_EVERY."},
    {"WIZ5_EC19_OUT",              "wiz5",        "PC trap capturing SCREENDEV driver entries to a file. Rides the generic-tap per-instruction gate."},
    {"WIZ5_KEYRAM",                "wiz5",        "dump RAM once per keystroke; needs a session directory to write into."},
    {"WIZ5_KEYS",                  "wiz5",        "file of scheduled keystroke entries to inject."},
    {"WIZ5_KEY_DELAY",             "wiz5",        "frames of delay between scheduled keystrokes."},
    {"WIZ5_NO_WRITEBACK",          "wiz5",        "suppress WIZ5_WRITEBACK even when it is set."},
    {"WIZ5_SESSION",               "alias",       "legacy alias for A2GSPU_TAP_SESSION."},
    {"WIZ5_TAP",                   "alias",       "legacy alias for A2GSPU_TAP. A2GSPU_TAP* is canonical; the alias lets one merged tool serve the wiz5/pascal projects with no script change."},
    {"WIZ5_WRITEBACK",             "wiz5",        "write in-memory (possibly modified) disks back to their FILES at spike end. OFF by default under mutation-guard discipline -- use only on writable scratch images."},
    {"WIZ5_ZP_EVERY",              "alias",       "legacy alias for A2GSPU_TAP_ZP_EVERY."},
    };
    n = (int)(sizeof(r) / sizeof(r[0]));
    return r;
}

inline int rail_count() { int n = 0; rails(n); return n; }

inline const Rail *rail_find(const char *name) {
    int n = 0;
    const Rail *r = rails(n);
    for (int i = 0; i < n; i++)
        if (!strcmp(r[i].name, name)) return &r[i];
    return nullptr;
}

// Enumerate to `out`. `family` filters (nullptr / "" / "all" = everything);
// `prefix` filters by name prefix, so a client can ask for just MEMVU_ or the
// A2GSPU_TAP_ family. Returns the row count so a caller can fail loud on zero
// rather than reporting an empty list as success.
inline int rail_emit(FILE *out, const char *family, const char *prefix) {
    int n = 0, shown = 0;
    const Rail *r = rails(n);
    bool all = !family || !*family || !strcmp(family, "all");
    size_t plen = prefix ? strlen(prefix) : 0;
    for (int i = 0; i < n; i++) {
        if (!all && strcmp(r[i].family, family)) continue;
        if (plen && strncmp(r[i].name, prefix, plen)) continue;
        fprintf(out, "rail %-26s %-11s %s\n",
                r[i].name, r[i].family, r[i].synopsis);
        shown++;
    }
    return shown;
}

}  // namespace a2rails
