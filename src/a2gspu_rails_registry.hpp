// a2gspu_rails_registry.hpp -- the env-rail half of the self-describing surface.
//
// AGENTS.md rule 6 requires new verbs *and rails* to be discoverable via
// `help` / `manifest`. Verbs were: a2gspu_manifest.hpp enumerates every one of
// them from the running process, and the `help` verb's own rationale is "enumerate
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
// tools/a2rail/railparity.ps1 fails if any getenv() in src/ has no entry here,
// or if an entry names a rail no code reads -- so the surface cannot silently
// drift again in either direction.
//
// Dual-mode is unaffected: static const data plus pure functions. It arms
// nothing and reads no environment.

#pragma once
#include <cstdio>
#include <cstring>

namespace a2rails {

// family: run | observe | trace | break | manipulate | snapshot | assert |
//         memvu | tap | ctrl | meta | determinism | device | wiz5 | alias
// It is the same word as the Group column of the rail's row in
// Docs/A2GSPU_RAILS.md; railparity.ps1 fails a rail whose two disagree, because
// `rail list trace` and the catalogue used to answer the same question differently.
//       | device | determinism | meta | calibrate | tap | memvu | wiz5
//       | alias | ctrl
struct Rail {
    const char *name;
    const char *family;
    const char *synopsis;
};

inline const Rail *rails(int &n) {
    static const Rail r[] = {
    {"A2GSPU_ASSERT",              "assert",      ";/,-separated name OP value checks (OP == != <= >= < >); the spike exits non-zero if any fails OR cannot be evaluated. Fields: scb_mode, qd_carry/qd_err, lt_carry, cs_carry/pr_carry, gsos_err, nonzero/distinct, idxN (N=0..15), and peek:HHHHHH (observation-free 24-bit byte read; refused on a handler-backed page). VALUES ARE DECIMAL unless prefixed $ or 0x (0d forces decimal; a bare LEADING ZERO makes it octal), and a peek: comparand must carry an explicit base -- a bare hex-shaped literal is refused, not guessed. A clause with no comparison operator is counted, warned and failed; a non-empty spec that yields 0 checks FAILS; a spec set to empty/whitespace arms no gate at all (gate=none). SAME evaluator as the live CTRL `assert` verb -- they were two implementations behind one name and disagreed five ways; assertparity.ps1 keeps them honest"},
    {"A2GSPU_BREAK",               "break",       "headless breakpoint at a 24-bit PC"},
    {"A2GSPU_BRKDUMP",             "break",       "dump CPU registers on BRK (crash under GS/OS) + a SPIKE-END register dump"},
    {"A2GSPU_BRKMEM",              "break",       "on BRK, dump the recent-PC history + bytes around the crash PC and stacked return"},
    {"A2GSPU_BUS_DUMP",            "observe",     "~BUS cycle dump (source = slot_bus) for the gssquared->card host gate"},
    {"A2GSPU_BUS_STREAM",          "observe",     "live ~BUS slot-bus CDC streaming (-> PB INGRESS_SOURCE=0)"},
    {"A2GSPU_CALLSTREAM",          "trace",       "symbol-free NDJSON of the toolbox/GS-OS call sequence (for tools/gdiff/calldiff.py first-divergence diffing)"},
    {"A2GSPU_CALLTRACE",           "trace",     "JSR/JSL call trace, armed from frame 0 (1) or when the PC first reaches a hex address (<hexPC>)"},
    {"A2GSPU_CALLTRACE_N",         "trace",       "cap CALLTRACE entries"},
    {"A2GSPU_CALLTRACE_SKIP",      "trace",       "skip the first n calls before logging"},
    {"A2GSPU_CONDTRAP",            "break",       "conditional flag-provenance trap: at PC, when flag f(c/z/i/d/x/m/v/n)==v, report the PC that last changed that flag"},
    {"A2GSPU_CONDTRAP_MAX",        "break",       "cap on CONDTRAP hits reported (default 8)"},
    {"A2GSPU_COPILOT",             "ctrl",     "windowed agent rail: observe+UX control (pause/key/mount/quit); instrumentation stays opt-in/off"},
    {"A2GSPU_COVERAGE",            "observe",     "LO-HI or BANK:LO-HI -- per-byte execution bitmap over that range, written at spike end. Feeds deasmiigs --coverage so the code/data split is a measurement, not a heuristic."},
    {"A2GSPU_COVERAGE_OUT",        "observe",     "file the COVERAGE bitmap is flushed to. Written BEFORE the gate block, which exits on any gate and would otherwise discard the run."},
    {"A2GSPU_CPUTEST",             "run",         "short-circuit the spike: run one CPU micro-test case, print === CPUTEST COMPLETE (PASS/FAIL) ===, exit with its rc"},
    {"A2GSPU_CTRL",                "ctrl",        "enable the interactive stepping rail: 20 Hz poll of <dir>/cmd.N, atomic <dir>/ack.N reply; replaces the fixed-frame spike. Also forces headless when set"},
    {"A2GSPU_CTRL_TIMEOUT",        "ctrl",        "idle timeout in seconds before an orphaned session quits (default 300)"},
    {"A2GSPU_DHGR_GOLDEN",         "assert",     "SPIKE-end IIe DHGR gate (bless if missing; MATCH/DIFF exit). A bless compared nothing: it reports gate=BLESSED, not gate=PASS, and raises a WARNING so A2GSPU_STRICT=1 refuses it"},
    {"A2GSPU_DHGR_GOLDEN_PROFILE", "assert",     "vram-raw (default) or 4dot -- must match how the golden file was blessed"},
    {"A2GSPU_DHGR_PAGE",           "assert",     "which page the SPIKE-end IIe DHGR golden reads: 1 or 2 (default 1)"},
    {"A2GSPU_DUMP",                "observe",     "hexdump memory ranges (hex). $E0/$E1 read the Mega II image directly (side-effect-free); other banks via the MMU"},
    {"A2GSPU_ERRHOOK",             "trace",       "surface carry-set GS/OS dispatch errors (<-- SYSTEM/LOADER ERROR)"},
    {"A2GSPU_EXPECT_HASH",         "assert",      "hex hash the SHR window (IIgs) or HGR page (II-family) must equal at spike end; MATCH/MISMATCH printed, mismatch is a gate failure (exit 1). The inline form of GOLDEN, for harnesses that pin a value per row"},
    {"A2GSPU_BACKGROUND_WINDOW",   "ctrl",        "keep the window out of the user's way: not focusable, not activated when shown, pushed to the back -- what A2GSPU_CTRL does implicitly, for an automated windowed run without the CTRL rail (the co-pilot gate)"},
    {"A2GSPU_BRAM",                "determinism", "pin the battery RAM a run starts from: default (built-in contents, no file read or written) or a 256-byte file (fatal if unreadable, never written back); unset reads and rewrites ./gs2-bram.bin"},
    {"A2GSPU_FAKETIME",            "determinism", "freeze the RTC at that instant (bypasses localtime/timezone) so RTC-derived OS state is run-to-run stable"},
    {"A2GSPU_FORCE_KEY",           "manipulate",  "force the ADB $C000 keyboard latch to return this key (drives a $C000-polling guest headlessly)"},
    {"A2GSPU_GOLDEN",              "assert",      "hash the SHR window ($E1:2000-9FFF, house FNV-1a-64); if the file exists compare (DIFF => gate fail; corrupt/empty file => hard fail); if absent, bless it -- and a path that can be neither opened nor created is a HARD gate failure, not the silent nothing it used to be. A bless compared nothing, so it reports gate=BLESSED (not PASS) and raises a WARNING, which means A2GSPU_STRICT=1 refuses it; rc and the exit code stay 0"},
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
    {"A2GSPU_KEYRAM",              "observe",     "dump RAM once per keystroke; needs a session directory to write into (canonical name for WIZ5_KEYRAM)"},
    {"A2GSPU_KEYSCHED",            "manipulate",  "file of scheduled keystroke entries to inject. Canonical name for WIZ5_KEYS, which still works. Distinct from SPIKE_KEYS: that is a literal string injected at a frame, this is a schedule read from a file"},
    {"A2GSPU_KEYSCHED_DELAY",      "manipulate",  "frames of delay between scheduled keystrokes (canonical name for WIZ5_KEY_DELAY)"},
    {"A2GSPU_LCTRACE",             "trace",       "Language-Card soft-switch access log"},
    {"A2GSPU_LIVENESS",            "observe",     "every N frames hash the screen (IIgs: SHR $E1:2000-9FFF; II-family: HGR $2000-3FFF) and the text page, print whether they changed, and append screen_unchanged_since= and text_unchanged_since=<frame>|none to GSDIAG, tracked separately because a halted SHR workload keeps redrawing its text page. A signal, not a verdict: an idle desktop is also static"},
    {"A2GSPU_LIVENESS_OUT",        "observe",     "NDJSON sink for LIVENESS samples (default: one stdout line per sample)"},
    {"A2GSPU_LIVENESS_STALL",      "observe",     "unchanged samples in a row before the screen counts as static (default 3)"},
    {"A2GSPU_LOAD",                "run",         "multi-file deliberate RAM splice (IIe+IIgs); no PC change. Also accepts HEX@file. Separators: ; or ,. Hard-fails SPIKE on any error."},
    {"A2GSPU_LOADTRACE",           "trace",       "segment-overlay tracker: log contiguous write-bursts (segment loads) into the region + when they overlay prior loads"},
    {"A2GSPU_LOADTRACE_MIN",       "trace",       "minimum burst length counted as a load"},
    {"A2GSPU_LOAD_BOOT",           "run",         "frames before A2GSPU_LOAD (default 0)"},
    {"A2GSPU_MILESTONES",          "trace",       "load a boot-milestone list; print a reached / NOT-REACHED table at spike end"},
    {"A2GSPU_MMUTEST",             "run",         "short-circuit the spike: run one MMU+video (FPI mapping / SHR decode) micro-test, exit with its rc"},
    {"A2GSPU_MODETRACE",           "trace",       "CPU mode-transition events (XCE / REP / SEP -> e/M/X changes)"},
    {"A2GSPU_MOUSETRACE",          "trace",       "append each ADB mouse injection to kg_mouse_trace.txt -- proves a mouse delta reached the micro, not just the host."},
    {"A2GSPU_NO_WRITEBACK",        "manipulate",  "suppress write-back even when WRITEBACK is set. Honoured across BOTH spellings, so the guard cannot be bypassed by using the other name (canonical for WIZ5_NO_WRITEBACK)"},
    {"A2GSPU_OUT_DIR",             "meta",        "directory prefix applied to rail output filenames. An absolute path (or Windows drive letter) is honoured verbatim, so an explicit caller-chosen path is never rewritten."},
    {"A2GSPU_PCTRAP",              "break",       "one-shot dump of registers + recent-PC ring on first entry to a PC range"},
    {"A2GSPU_POKE",                "manipulate",  "one-shot deliberate splice at a PC. Actions: A/X/Y/S/D/P/DBR/PB=<hex> (reg/flag), PC=<hex> (force-branch), M<hex24>=<hexbyte> (poke mem), RTS/RTL (force-return), SKIP=<n> (skip bytes)"},
    {"A2GSPU_POKE_NTH",            "manipulate",  "fire the POKE on the Nth hit of the PC (default 1)"},
    {"A2GSPU_RAMDISK",             "device",      "serve a mounted image from a copy-on-write RAM buffer; writes never touch the host file"},
    {"A2GSPU_RECORDS",             "observe",     "the record contract's global NDJSON sink: every rail record (event=<rail>.<kind>, frame, cycles, then the rail's own fields) whose rail has no sink of its own (A2GSPU_TRAP_OUT/out=, A2GSPU_LIVENESS_OUT, A2GSPU_WATCH_OUT, A2GSPU_CALLSTREAM, A2GSPU_TAP) lands here; without it those rails print their stdout line. rail read A2GSPU_RECORDS reports the sink, and per rail how many records reached THAT sink and how many went to the rail's own sink or to stdout -- two numbers, because one number beside sink= read as the sink's count and was not"},
    {"A2GSPU_RESTORE",             "snapshot",    "alias for SNAP_LOAD (load-at-start)"},
    {"A2GSPU_RETGUARD",            "break",     "return-target guard: flag an RTS/RTL that returns into $00/$01 (1), or also into a gap between symbols (strict)"},
    {"A2GSPU_ROM",                 "observe",     "ROM image path for the lazy-loaded ROM-vs-kernel PC classifier (default resources/roms/apple2gs/main.rom)"},
    {"A2GSPU_ROM_SYMBOLS",         "observe",     "name ROM-resident PCs (else <ROM>)"},
    {"A2GSPU_RUN_BIN",             "run",         "after boot frames, inject a flat binary into IIgs RAM (default load/exec $7E0000), reset the traces to isolate it, set PC to it. Writes are VERIFIED by read-back and confined to ONE bank; a binary that would cross the bank end is refused before anything is written, and a bank-0 $D000+ load banks the language card in (two reads of $C08B) so the write can land at all"},
    {"A2GSPU_RUN_BOOT",            "run",         "boot frames to run before RUN_BIN injection (default spike_frames/2)"},
    {"A2GSPU_SAVE_AT",             "break",       "snapshot + halt at the first hit of an arbitrary 24-bit PC (a breakpoint that also checkpoints)"},
    {"A2GSPU_SEED",                "determinism", "seed the cold-boot SP entropy (srand) so a from-scratch spike is byte-reproducible (warm restores overwrite SP anyway)"},
    {"A2GSPU_SERIAL_DEBUG",        "device",      "verbose real-card serial/reconnect logging"},
    {"A2GSPU_SERIAL_PORT",         "device",      "override the real-card CDC probe port (default tries COM10 first)"},
    {"A2GSPU_SNAP_HI",             "trace",     "high bound of the SNAP region logger."},
    {"A2GSPU_SNAP_LO",             "trace",     "low bound of the SNAP region logger; with SNAP_HI and SNAP_PCS, dumps [LO,HI] at up to 8 trigger PCs."},
    {"A2GSPU_SNAP_LOAD",           "snapshot",    "restore that snapshot at start and skip the ~1200-frame boot (the frame loops self-heal excluded device/scanner/clock state)"},
    {"A2GSPU_SNAP_MAX",            "trace",     "cap on SNAP records emitted."},
    {"A2GSPU_SNAP_OUT",            "trace",       "destination file for the SNAP region-logger (requires LO/HI + PCS)"},
    {"A2GSPU_SNAP_PCS",            "trace",       "up to 8 trigger PCs at which to dump the SNAP window"},
    {"A2GSPU_SNAP_SAVE",           "snapshot",    "at spike end, save CPU regs + full MMU snapshot (128K + page tables + soft-switch state) + trailing sentinel"},
    {"A2GSPU_SNAP_ZP",             "trace",     "zero-page pointer correlation key, recorded as an annotation -- the emitter keys on PC, so this labels rather than triggers."},
    {"A2GSPU_SPIKE_FRAMES",        "run",         "run N deterministic frames headless (with -n), then evaluate the dump/assert/golden gates and exit. The master headless switch."},
    {"A2GSPU_SPIKE_KEYS",          "run",         "inject keystrokes via the keyboard paste buffer (\\n->\\r), paced by the guest's own $C000 polls"},
    {"A2GSPU_SPIKE_KEYS_AT",       "run",         "frame at which SPIKE_KEYS is injected (default spike_frames/2)"},
    {"A2GSPU_SP_NDEV",             "device",      "override the SmartPort device count (A/B faithfulness; 32 reproduces pre-fix enumeration)"},
    {"A2GSPU_STRICT",              "assert",      "exit 3 at spike end if the run printed any WARNING (a skipped injection, an unmountable disk in windowed mode, a rail that could not do what it was asked). GSDIAG carries warnings=N either way"},
    {"A2GSPU_STACKTRAP",           "break",       "one-shot dump when S first enters the range"},
    {"A2GSPU_STACKWATCH",          "break",     "continuous stack-pointer tripwire for garbage-S crashes: 1 = imbalance only, LO-HI = over/underflow window"},
    {"A2GSPU_STACKWATCH_JUMP",     "break",       "imbalance threshold (single-step S delta that trips STACKWATCH)"},
    {"A2GSPU_STOP_ON_FAULT",       "break",       "halt the spike on BRK/COP/SysFail instead of burning remaining frames"},
    {"A2GSPU_SYMBOLS",             "observe",     "load a symbol table (annotate trace/PCs with names). TWO accepted forms: the linkiigs .final.dbg 'symbol \"NAME\" type LABEL addr $XXXXXX' line, and a plain 'HEXADDR name' line (# comments skipped). A file that cannot be opened, or that yields 0 symbols in either form, is a WARNING -> GSDIAG warnings=N, and A2GSPU_STRICT=1 exits 3. rail read A2GSPU_SYMBOLS reports path/format/count/base"},
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
    {"A2GSPU_TRAP",                "break",       "traps from configuration: ';'-separated specs of trigger (at=PC[-PC] with exit=PC, store=ADDR[-ADDR] for a byte written there, sp=LO-HI for S entering a range), condition (when=), policy (once|every|first=N/key), payload (do=/heavy=/xdo=/xheavy= regs,ring,zp,stack,mem,field,text,scan) and sink (out=). NDJSON. The general form of PCTRAP and the wiz5 traps; grammar in Docs/A2GSPU_TRAP.md"},
    {"A2GSPU_TRAPDUMP",            "break",       "extra memory region (hex) dumped by PCTRAP (default 48 bytes)"},
    {"A2GSPU_TRAP_FILE",           "break",       "file of A2GSPU_TRAP specs, one per line, '#' comments"},
    {"A2GSPU_TRAP_OUT",            "break",       "default NDJSON sink for A2GSPU_TRAP specs that give no out=; without it records go to stdout prefixed 'IIGS TRAP:'"},
    {"A2GSPU_VALTRAP",             "break",       "value-provenance store trap: bind the PC that stores VALUE into memory (LE reassembled across consecutive bytes; w=1-4, default 3). \"Who wrote value V\""},
    {"A2GSPU_VALTRAP_MAX",         "break",       "VALTRAP hit cap (default 64)"},
    {"A2GSPU_VIDEOMAP",            "observe",     "a 40x25 ASCII dominant-index map of the Super Hi-Res window (SEE layout headless)"},
    {"A2GSPU_VIDEOSUM",            "observe",     "per-line SCB (320/640) + palette histogram (RGB888) + mode-correct pixel-index histogram"},
    {"A2GSPU_VRAM_LOAD",           "run",         "inject 16K aux||main (profile=vram-raw) into HGR page flat image -- a2tile offline paint path"},
    {"A2GSPU_VRAM_PAGE",           "run",     "HGR page A2GSPU_VRAM_LOAD injects into: 1 or 2 (default 1)"},
    {"A2GSPU_WATCH",               "break",       "write-watchpoint on address range(s): log each write with writer PC/S/D/DBR (\"who wrote address X\"). Warns on a bare 24-bit addr (missing :) that would never fire"},
    {"A2GSPU_WATCH_CHANGE",        "break",       "log only when the value differs from the last seen for that range"},
    {"A2GSPU_WATCH_IPC_ZP",        "break",       "generic interpreter-IP attribution: makes the NDJSON ipc field read a 2-byte LE ZP pointer (observation-free) -- attributes a store to the interpreter's segment. Default-off (ipc=0). For UCSD p-code (Wizardry) set 9E"},
    {"A2GSPU_WATCH_MAX",           "break",       "WATCH hit cap (default 256; 0 = unlimited)"},
    {"A2GSPU_WATCH_OUT",           "break",       "emit WATCH as NDJSON to a file instead of stdout (adds ts frame-stamp + ipc fields)"},
    {"A2GSPU_WATCH_READ",          "break",       "also watch reads of the WATCH ranges"},
    {"A2GSPU_WRITEBACK",           "manipulate",  "write in-memory (possibly modified) disks back to their FILES at spike end. OFF by default under mutation-guard discipline -- use only on writable scratch images. Canonical name for WIZ5_WRITEBACK"},
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
    {"WIZ5_KEYRAM",                "alias",       "legacy alias for A2GSPU_KEYRAM"},
    {"WIZ5_KEYS",                  "alias",       "legacy alias for A2GSPU_KEYSCHED"},
    {"WIZ5_KEY_DELAY",             "alias",       "legacy alias for A2GSPU_KEYSCHED_DELAY"},
    {"WIZ5_NO_WRITEBACK",          "alias",       "legacy alias for A2GSPU_NO_WRITEBACK"},
    {"WIZ5_SESSION",               "alias",       "legacy alias for A2GSPU_TAP_SESSION."},
    {"WIZ5_TAP",                   "alias",       "legacy alias for A2GSPU_TAP. A2GSPU_TAP* is canonical; the alias lets one merged tool serve the wiz5/pascal projects with no script change."},
    {"WIZ5_WRITEBACK",             "alias",       "legacy alias for A2GSPU_WRITEBACK"},
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

// ---- reachability, declared -----------------------------------------------------
// Which CTRL verb reaches the same capability live, and how a rail's state is
// read back while the machine runs. Declared per rail by someone who read the
// verb's implementation, never inferred from names: inferring it produced
// three confident falsehoods in one afternoon (2026-09-02). `rail live`
// checks every row against the rail table and the verb table at runtime, so a
// rename fails loud instead of leaving a stale claim. A rail absent from this
// table is launch-only with no readback, and `rail find` says exactly that.
//
//   verb : the CTRL verb (its first word) that arms or drives the same thing
//          live, or "" when there is none
//   read : "fn"          the rail registers its own reader (a2rails::Registrar
//                        in the rail's header); `rail read NAME` calls it, and
//                        `rail live` fails a row whose rail registered none
//          "verb:<text>" the CTRL verb that reads it back
//          ""            no readback; the rail speaks only at spike end
struct Reach { const char *rail; const char *verb; const char *read; };

// ---- readback, registered by the rail itself ------------------------------
// A rail that can be read back supplies the function WITH the rail, in its own
// header, so a reader cannot be declared here and left unimplemented, and a
// rename fails at `rail live` (and offline in railparity.ps1) on both sides: a
// "fn" row with no registration, or a registration with no "fn" row. The
// first version kept "fn:<key>" strings here and a string-compare chain in the
// CTRL verb -- a table beside the rail, which is the shape that drifts.
using Reader = void (*)(char *buf, size_t n);
struct ReaderReg { const char *rail; Reader fn; };
inline ReaderReg g_readers[48] = {};
inline int       g_nreaders = 0;
struct Registrar {
    Registrar(const char *rail, Reader fn) {
        if (g_nreaders < (int)(sizeof g_readers / sizeof g_readers[0])) g_readers[g_nreaders++] = {rail, fn};
    }
};
inline Reader reader_find(const char *rail) {
    for (int i = 0; i < g_nreaders; i++)
        if (!strcmp(g_readers[i].rail, rail)) return g_readers[i].fn;
    return nullptr;
}

inline const Reach *reach(int &n) {
    static const Reach r[] = {
    {"A2GSPU_ASSERT",        "assert",      ""},
    {"A2GSPU_BRAM",          "",            "fn"},
    {"A2GSPU_BREAK",         "bp",          "verb:bp"},
    {"A2GSPU_CALLSTREAM",    "callstream",  "verb:callstream status"},
    {"A2GSPU_COVERAGE",      "cov",         "verb:cov status"},
    {"A2GSPU_COVERAGE_OUT",  "cov",         ""},
    {"A2GSPU_DHGR_GOLDEN",   "dhgr-golden", ""},
    {"A2GSPU_DUMP",          "read",        ""},
    {"A2GSPU_EXPECT_HASH",   "shr-golden",  ""},
    {"A2GSPU_FAKETIME",      "",            "fn"},
    {"A2GSPU_FORCE_KEY",     "holdkey",     ""},
    {"A2GSPU_GOLDEN",        "shr-golden",  ""},
    {"A2GSPU_ITRACE_FROM",   "itrace",      ""},
    {"A2GSPU_ITRACE_N",      "itrace",      ""},
    {"A2GSPU_LIVENESS",      "",            "fn"},
    {"A2GSPU_LOAD",          "load",        ""},
    {"A2GSPU_POKE",          "poke",        ""},
    {"A2GSPU_RAMDISK",       "",            "fn"},
    {"A2GSPU_RECORDS",       "",            "fn"},
    {"A2GSPU_RESTORE",       "restore",     ""},
    {"A2GSPU_SEED",          "",            "fn"},
    {"A2GSPU_SNAP_LOAD",     "restore",     ""},
    {"A2GSPU_SNAP_SAVE",     "save",        ""},
    {"A2GSPU_SPIKE_KEYS",    "keys",        ""},
    {"A2GSPU_STRICT",        "",            "fn"},
    {"A2GSPU_SYMBOLS",       "",            "fn"},
    {"A2GSPU_TBTRACE",       "tbtrace",     ""},
    {"A2GSPU_TBTRACE_BANK",  "tbtrace",     ""},
    {"A2GSPU_TEXT40",        "text",        ""},
    {"A2GSPU_TEXTDUMP",      "text",        ""},
    {"A2GSPU_TRAP",          "",            "fn"},
    {"A2GSPU_TRAP_FILE",     "",            "fn"},
    {"A2GSPU_TRAP_OUT",      "",            "fn"},
    {"A2GSPU_VALTRAP",       "valtrap",     "verb:valtrap status"},
    {"A2GSPU_VRAM_LOAD",     "vram",        ""},
    {"A2GSPU_WATCH",         "watch",       "verb:watch bus hits"},
    {"MEMVU_LOADVIS",        "",            "fn"},
    {"MEMVU_STOREVIS",       "",            "fn"},
    };
    n = (int)(sizeof(r) / sizeof(r[0]));
    return r;
}

inline const Reach *reach_find(const char *rail) {
    int n = 0;
    const Reach *r = reach(n);
    for (int i = 0; i < n; i++)
        if (!strcmp(r[i].rail, rail)) return &r[i];
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
