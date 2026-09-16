// a2gspu_manifest.hpp -- self-describing control surface for agentic AI.
//
// North star (Docs/AGENTIC_ORACLE.md): nothing is a black box; agents can
// discover capabilities, observation models, and contracts from the running
// process without reading C++.

#pragma once
#include <cstdio>
#include <cstring>
#include "systemconfig.hpp"
#include "a2gspu_rails_registry.hpp"

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
        "  snapshot= CPU+MMU+softswitches+RAM, plus the ADB micro, the Ensoniq, the interrupt registers, the disk controller and the block device's RAM-disk overlay on a IIgs; the manifest names the rest (floppy media, scanner self-heal).\n"
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

/* WHEN TO REACH FOR EACH VERB.
 *
 * The synopsis says what a verb IS and what its syntax is. It does not say what
 * question the verb answers, and that is the thing a caller arriving cold
 * actually needs -- an agent that cannot tell `valtrap` from `bp` will hand-
 * bisect a problem one of them answers directly. (Measured: it did. Three
 * rounds of manual bisection went into "what wrote this byte" while a
 * provenance trap sat unused in this table.)
 *
 * Kept as a separate lookup rather than a field in Verb so the synopsis strings
 * -- some of them long -- are not disturbed, and so a verb with no entry yet
 * degrades to "-" instead of failing to compile.
 */
struct VerbUse { const char *name; const char *when; };

inline const char *verb_when(const char *name) {
    static const VerbUse u[] = {
        {"assert",     "a memory expectation checked INSIDE the emulator; returns PASS/FAIL, no host round trip"},
        {"audio",      "proving a device makes SOUND. Register round-trips pass on a chip emitting silence"},
        {"boot",       "enter a SLOT's firmware at $Cs00 the way autoboot would -- for a block device the ROM's own autoboot cannot reach"},
        {"bp",         "stop at a PC you can name. A hit HALTS in front of that instruction (not yet executed) -- run/step/resume execute it once and continue"},
        {"callstream", "capture toolbox/GS-OS calls as NDJSON while software runs (toolbox ABI work)"},
        {"copilot",    "discover the windowed agent rail (observe + pause/key/mount/quit); instrumentation stays off"},
        {"cov",        "which addresses executed AT ALL -- dead code, unreached branches, coverage of a run"},
        {"cpu",        "registers and halt state. The first thing to read when execution went somewhere unexpected"},
        {"cycles",     "the machine's clock rate, before trusting any timing a test assumes"},
        {"devstate",   "what devices actually exist and in what state, before assuming a slot is populated"},
        {"dis",        "disassemble where a PC landed. Needs a filename -- it will not name a file after the address"},
        {"help",       "enumerate verbs from the RUNNING process rather than from documentation that may have drifted"},
        {"hgr",        "a coarse ASCII view of an HGR page: 'is anything drawn'. Not pixel-exact -- see pngc"},
        {"holdkey",    "hold a IIgs key DOWN across a boot (Open-Apple style options) rather than tapping it"},
        {"iolog",      "the ordered $C0xx ring: WHICH soft switches were touched, in what order, read vs write"},
        {"itrace",     "a live per-instruction trace from a chosen PC. Heavier than tbuf; use for a whole path"},
        {"json",       "wrap any command reply in a stable JSON envelope for agents that must not scrape human-oriented text"},
        {"key",        "inject one keystroke headlessly"},
        {"keys",       "inject a string headlessly -- typing a command line with no keyboard"},
        {"load",       "write a host file into guest memory, including aux as bank 01"},
        {"limitations","enumerate known fidelity gaps from the running binary so unsupported hardware cannot be mistaken for working"},
        {"manifest",   "the full machine-readable capability record, for a client discovering this rail cold"},
        {"memvu",      "how much of a workload's memory traffic an agent OUTSIDE the CPU can see. Read it live and zero it, so a measurement can be a bounded window instead of a whole process lifetime"},
        {"mount",      "swap media at runtime. FAILS on a missing path and does not eject the current disk to find out"},
        {"oracle",     "the contracts governing every other verb. Read FIRST -- especially the cycles= rule"},
        {"paddle",     "inject an analogue paddle value; seeds the 558 decay so PREAD counts the value back"},
        {"pause",      "freeze the machine on the WINDOWED copilot rail, where frames run on their own; headless CTRL is already stopped between commands"},
        {"pause-status","ask whether the machine is paused, stepping or running, before reading a rail whose meaning depends on which"},
        {"pbutton",    "press a game-controller button headlessly"},
        {"png",        "pixel capture of a graphics page"},
        {"pngc",       "pixel-exact COLOUR capture against a named profile -- this is the art oracle, not shot"},
        {"protocol",   "negotiate the stable CTRL contract and discover the JSON reply wrapper before parsing other acknowledgements"},
        {"poke",       "write bytes at an address. The fastest way to plant a stub and drive it"},
        {"press",      "press a key with modifiers held, where a bare key injection is not enough"},
        {"quit",       "end the session. A suite that does not is a paused machine that reads as a hang"},
        {"rail",       "what can be OBSERVED here at all. Verbs are the live surface; rails are the launch-time one, and asking the binary beats reading a catalogue that has drifted before (128 rails read, 96 described, when this was added)"},
        {"read",       "dump guest memory to a host file, including aux as bank 01"},
        {"reset",      "recover a wedged machine; cold with the argument"},
        {"restore",    "reload a snapshot: CPU+MMU+softswitches+RAM plus whichever device blocks the file holds (the ack carries its manifest). Media and scanner self-heal -- not full time travel. A file that does not fit is rolled back, never half-applied"},
        {"resume",     "continue past a breakpoint hit without losing the breakpoint"},
        {"run",        "advance N video frames. Use run-until when you can name a destination instead"},
        {"run-until",  "run to a named PC with an instruction budget. THE timing primitive: its cycles= is an in-command delta, the only form the oracle certifies. Arriving proves this run executed that address -- a value in RAM cannot imitate a program counter"},
        {"dhgr-calibrate", "derive the DHGR colour profile from the running machine rather than assuming one"},
        {"dhgr-export",    "write the calibrated DHGR profile out for the art pipeline to compile against"},
        {"dhgr-golden",    "capture or check a DHGR reference frame -- the art oracle for colour work"},
        {"save",       "take a snapshot before an experiment you expect to be destructive: CPU+MMU+softswitches+RAM plus the device blocks this machine has; the manifest names what it holds"},
        {"screen",     "text-page helper"},
        {"session",    "starting or ending a suite. Ask what is armed before trusting a measurement; reset before handing the machine on, or the next run inherits your probes"},
        {"systems",    "enumerate every platform, built-in configuration, ROM personality and numeric slot-device map from the running binary"},
        {"setreg",     "set PC or a register before running a stub"},
        {"shot",       "an SDL backbuffer BMP for a HUMAN to look at. Never an art oracle -- use pngc"},
        {"shr",        "SHR framebuffer observation on a IIgs"},
        {"shr-golden", "bless or compare the live IIgs SHR memory window without relaunching into SPIKE mode"},
        {"speaker",    "$C030 toggle counts and their timing -- proves the speaker moved and when"},
        {"stack",      "stack contents. Read this when a runaway wound SP down and you need to know from where"},
        {"step",       "execute N instructions. cycles= is IN-COMMAND and must not be subtracted across acks"},
        {"tbtrace",    "live toolbox/GS-OS trace filtered by bank"},
        {"tbuf",       "the CPU trace ring: per-instruction registers, effective address, data, read vs write"},
        {"text",       "dump both text pages"},
        {"trap",       "record what the machine looked like when it reached a PC, stored or read an address, or moved S into a range -- a spec armed live, with conditions and payloads; the same grammar as A2GSPU_TRAP"},
        {"valtrap",    "find where a VALUE came from. NOT an address watchpoint -- for 'stop at this address' use bp"},
        {"verify",     "compare guest memory to a host file IN the emulator; answers with the first differing offset"},
        {"vid",        "soft-switch video state -- the truth about what the display is actually doing"},
        {"vram",       "raw 8K aux + 8K main video memory"},
        {"watch",      "memory watch"},
        {"joymode",    "pick the controller shape at $C061-$C063. The only way to reach Atari/mouse modes headlessly"},
    };
    for (const VerbUse &e : u) if (!strcmp(e.name, name)) return e.when;
    return "-";
}

// Keep sorted by name for stable manifests. Update when adding CTRL verbs.
inline const Verb *verbs(int *count) {
    static const Verb v[] = {
        {"assert", "observe", "assert peek:ADDR==VAL[;…] — probe_peek checks; status=PASS|FAIL", "memory-peek"},
        {"audio", "observe", "audio [status]|doc|sfx|on|off|reset — generated sample streams per SOURCE (doc = Ensoniq, sfx = drive sounds; never summed): count, non-silent, min/max, average level, and the same broken out per channel (ch=/l_*/r_*) so a dropped side is visible", "sample-stream"},
        {"boot", "run", "boot <slot> — enter slot firmware at $Cs00 (slot 1-7), as autoboot would", "enter-slot-firmware"},
        {"bp", "manipulate", "bp [addr|off] — interactive breakpoint; bare reports state. A hit halts before the instruction executes; run/step/resume execute it once and continue", "force-control"},
        {"callstream", "trace", "callstream on <file>|off|status — LIVE NDJSON toolbox stream", "tool_locator"},
        {"copilot", "meta", "copilot — windowed agent rail contract (A2GSPU_COPILOT)", "discover"},
        {"cov", "observe", "cov [status] | cov on <LO-HI|BANK:LO-HI> | cov map <A2MAP1-file> | cov off | cov reset | cov write <file> — coverage: raw A2COV1 or source-bound A2COV2; map/reset starts an overlay generation, mismatched executed bytes refuse write", "coverage"},
        {"cpu", "observe", "cpu — PBR:PC A X Y SP P E HALT STP RDY KBD AKD", "cpu-state"},
        {"cycles", "observe", "cycles — guest cycle counter NOW (absolute). "
         "Deltas only valid inside run/step/run-until acks.", "cycles-absolute-warn"},
        {"devstate", "observe", "devstate [name|all <file>] — per-device state the emulator already computes (ADB regs, disk head, mouse clamp, DOC, memory map); bare lists handlers", "device-debug"},
        {"dhgr-calibrate", "calibrate", "dhgr-calibrate — solid LORES 4-dot self-test", "colour-4dot"},
        {"dhgr-export", "calibrate", "dhgr-export <dir> — write GSDHGR4D+GSNTSC01 for a2tile", "profile-export"},
        {"dhgr-golden", "calibrate",
         "dhgr-golden <file> [page] [vram-raw|4dot] [bless] — FNV page golden", "vram-raw|4dot"},
        {"dis", "observe", "dis <addr> <n> <file> [m8|m16] [x8|x16] — side-effect-free disassembly (probe_peek); ack names the M/X assumption and unreadable=N", "memory-disasm"},
        {"help", "meta", "help [file] — list CTRL verbs (optional write full catalog)", "discover"},
        {"hgr", "observe", "hgr [page] [file] — coarse ASCII 'is anything drawn' view of an HGR page (not pixel-exact)", "hgr-ascii-coarse"},
        {"holdkey", "manipulate", "holdkey <hex> — sticky IIgs hold key", "inject-key"},
        {"iolog", "observe", "iolog [on [cap]|off|dump <f>|reset] — $C0xx R/W ring", "io-trace"},
        {"itrace", "trace", "itrace from <pc>|now|n <c>|clear|status — LIVE per-instr trace", "execute"},
        {"json", "meta", "json <command> — execute any CTRL command and wrap its reply as a2ctrl-reply-v2 JSON", "structured-reply"},
        {"key", "manipulate", "key <hex> [maxf] — inject key and wait for consume receipt", "inject-key"},
        {"keys", "manipulate", "keys <str> — paste buffer (no consume proof)", "inject-key"},
        {"load", "manipulate", "load <addr> <file> — splice binary into RAM via mmu->write", "inject-ram"},
        {"limitations", "meta", "limitations [file] — machine-readable known fidelity gaps and status", "discover"},
        {"manifest", "meta", "manifest <file> — full capability dump for agents", "discover"},
        {"memvu", "observe", "memvu [status]|store|load|reset — live memory-visibility counters (MEMVU_STOREVIS / MEMVU_LOADVIS). Refuses rather than reporting zeros when a rail is unarmed", "memory-visibility"},
        {"mount", "manipulate", "mount sXdY <path> — runtime media swap", "inject-media"},
        {"oracle", "meta", "oracle — north-star contracts (cycles/colour/snapshot)", "discover"},
        {"paddle", "manipulate", "paddle <0-3> <0-255> | paddle off — analogue paddle; seeds the 558 decay so PREAD counts the value back", "force-control"},
        // pause / pause-status were DISPATCHED AND UNDISCOVERABLE. Both are
        // handled in a2gspu_ctrl_cmds.hpp (try_exec) and dispatched on the same
        // line of gs2.cpp as `resume`, which has always had a row here; they had
        // none, so `help`, `help <file>` and `manifest <file>` -- all three of
        // the surfaces AGENTS.md rule 6 names -- omitted them, and nothing in
        // Docs/ mentions either. That made the process state a falsehood about
        // itself: an unrecognised word is answered "not a verb. `help` lists all
        // of them", which was not true of these two. Drift, not policy; there is
        // no internal-verb carve-out anywhere in the tree. railparity.ps1 now
        // compares the dispatcher's literals against this table in both
        // directions so the next one fails a gate instead of a user.
        {"pause", "manipulate", "pause — stop the machine (EXEC_PAUSED), the Machine>Pause equivalent; `resume` undoes it", "force-control"},
        {"pause-status", "observe", "pause-status — paused|step|running plus the raw execution_mode, without changing it", "exec-mode"},
        {"session", "meta", "session [status]|reset — what probe state is armed, and one command to disarm all of it. Names what it cannot reach rather than reporting a confident zero", "probe-state"},
        {"verify", "observe", "verify <addr|BANK:addr> <len> <file> — compare guest memory to a host file in the emulator and answer with the verdict: identical, or the first differing offset and both bytes", "compare-in-place"},
        {"joymode", "manipulate", "joymode [status]|gamepad|mouse|atari — controller shape at $C061-$C063; reports the Joyport post-reset suspend window and whether it is still open", "force-control"},
        {"pbutton", "manipulate", "pbutton <0-2> <0|1> — game switch PB0-PB2 ($C061-$C063)", "force-control"},
        {"png", "observe", "png <file> [page] [scale] [auto|hgr|dhgr] — mono dots from RAM", "bits-mono"},
        {"pngc", "observe", "pngc <file> [page] [scale] [4dot|mono] — named colour PNG", "colour-4dot"},
        {"protocol", "meta", "protocol — CTRL version, framing, ownership and json-wrapper contract", "discover"},
        {"poke", "manipulate", "poke [bank:]<addr> <hexbytes...> — write bytes (labeled splice); a byte the MMU refuses (I/O, ROM, write-inhibited) fails the ack: poke-refused", "inject-ram"},
        {"shr", "observe", "shr <file> [scale] — IIgs super hi-res as PNG; 320 or 640 per-line by SCB", "shr-linear"},
        {"shr-golden", "calibrate", "shr-golden <file> [bless] — live FNV of $E1:2000-$9FFF", "shr-window"},
        {"press", "manipulate", "press <hex> [hold] [mods] — physical key edge, mods o/c/s/k = Open-Apple/Closed-Apple/shift/control (no consume proof)", "inject-key"},
        {"quit", "run", "quit — end CTRL session", "-"},
        {"rail", "meta", "rail [list [family|PREFIX]] | find <NAME> | env-only <file> | live | read <NAME> | schema [NAME] — enumerate every env rail this binary reads, from the binary; live= which a CTRL verb reaches, read= how its state comes back, schema= the shape of the records it writes", "discover"},
        {"read", "observe", "read <addr> <len> <file> — probe_peek dump (side-effect-free)", "memory-peek"},
        {"reset", "manipulate", "reset — guest reset path", "force-control"},
        {"restore", "snapshot", "restore <file> — transactional snapshot load; ack carries the manifest, or FAIL stage= rolled-back=", "time-travel"},
        {"resume", "manipulate", "resume — clear a user halt (breakpoint/save-at) so the CPU advances again; refuses to paper over a jammed CPU", "force-control"},
        {"run", "run", "run <frames> — advance N frames; ack includes cycles= IN-COMMAND", "cycles-in-cmd"},
        {"run-until", "run", "run-until <addr> [max_instr] — stop at PC; cycles= IN-COMMAND", "cycles-in-cmd"},
        {"save", "snapshot", "save <file> — CPU+MMU+RAM snapshot plus the machine's device blocks; class= is derived from what was written", "time-travel"},
        {"screen", "observe", "screen — screen/text helper", "text-page"},
        {"setreg", "manipulate", "setreg <reg> <hex> — force CPU register", "force-control"},
        {"shot", "observe", "shot <file> — SDL backbuffer BMP (HUMAN eyeball; not art oracle)", "sdl-human"},
        {"speaker", "observe", "speaker — $C030 toggle count and c14m stamp of the last one; sound is WHEN, not whether", "speaker-toggles"},
        {"stack", "observe", "stack [n] — bytes above SP bank0", "cpu-state"},
        {"step", "run", "step [n] — execute N instructions; cycles= IN-COMMAND", "cycles-in-cmd"},
        {"systems", "meta", "systems [file] — list platform/config/ROM/device maps; file form is TSV", "discover"},
        {"tbtrace", "trace", "tbtrace on|off|status|bank <hex>|all — LIVE toolbox/GSOS trace", "tool_locator"},
        {"tbuf", "trace", "tbuf [status]|on|off|clear|dump <file> [n] — CPU trace ring: per-instruction regs, effective address and R/W", "instr-ring"},
        {"text", "observe", "text <file> — text page main+aux dump", "text-page"},
        {"trap", "break", "trap [status]|add <spec>|list|clear [<name>] — A2GSPU_TRAP armed LIVE: at=/store=/read=/sp= triggers, when= conditions, policies and payloads; a malformed spec fails the ack and arms nothing", "records"},
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
    // Syntax AND use case. A reader arriving cold needs to know which question
    // a verb answers, not only how to spell it -- that is the difference
    // between reaching for valtrap and hand-bisecting what it answers directly.
    for (int i = 0; i < n; i++) {
        fprintf(f, "  %-14s [%s] %s\n", v[i].name, v[i].kind, v[i].synopsis);
        const char *w = verb_when(v[i].name);
        if (strcmp(w, "-")) fprintf(f, "  %-14s   when: %s\n", "", w);
    }
}

// ---- record schemas ------------------------------------------------------------
// Every rail that writes machine-readable records declares their shape here:
// the tag that names the record and the fields a consumer may rely on. A
// consumer can validate what it reads against this instead of against the C++,
// and a change of shape is a diff in one table. Optional fields carry '?'.
// Declared beside the writers, checked by `rail schema` against the registry.
struct RecordSchema { const char *rail; const char *tag; const char *fields; const char *note; };

inline const RecordSchema *record_schemas(int &n) {
    static const RecordSchema r[] = {
    {"A2GSPU_RECORDS",    "envelope",
     "event(<rail>.<kind>) frame cycles + the rail's own fields",
     "the record contract (src/a2record.hpp): every rail record below has this shape, written whole; the global sink takes a record whose rail has no sink of its own"},
    {"A2GSPU_TRAP",       "event=trap.entry|trap.exit",
     "frame cycles trap t(e|x) n pc sym|null incall|null store{addr,value}? sp? heavy regs{a,x,y,s,d,dbr,pbr,p,e}? ring[]? zp? stack? mem{at,expr,d}? f{...}? text? scan[{at,ptr,d}]?",
     "one line per trigger; pc is the storing instruction for store= traps; sink: per-spec out=, else A2GSPU_TRAP_OUT, else A2GSPU_RECORDS, else stdout prefixed 'IIGS TRAP:'"},
    {"A2GSPU_LIVENESS",   "event=liveness.sample",
     "frame cycles screen text kbd changed(first|screen|text|both|none) insns screen_store|null text_store|null kbd_poll|null",
     "sink: A2GSPU_LIVENESS_OUT, else A2GSPU_RECORDS, else one stdout line per sample prefixed 'A2GSPU LIVENESS:'"},
    {"A2GSPU_WATCH_OUT",  "event=watch.write|watch.read|watch.suppressed",
     "frame cycles kind(write|read) pc addr data s d dbr ipc ts",
     "ts is the headless frame, kept for older readers; ipc is the 16-bit word at A2GSPU_WATCH_IPC_ZP or 0; a 'suppressed' record marks the WATCH_MAX cap; sink: A2GSPU_WATCH_OUT, else A2GSPU_RECORDS, else the stdout line"},
    {"A2GSPU_CALLSTREAM", "event=callstream.call|callstream.ret (ev=c|r kept)",
     "frame cycles + c: ev seq word kind caller s d dbr | r: ev word kind carry err",
     "toolbox/GS-OS call and its return; word is the call number, kind 0=toolbox 1=GS/OS class-1 2=class-0; sink: the A2GSPU_CALLSTREAM file"},
    {"A2GSPU_SNAP_OUT",   "event=snap.hit",
     "frame cycles seq pc a x y s d dbr lo data(hex of lo..hi, $C0xx read as 00)",
     "one line per hit of an A2GSPU_SNAP_PCS address, capped by A2GSPU_SNAP_MAX; sink: the A2GSPU_SNAP_OUT file"},
    {"A2GSPU_TAP",        "event=tap.hit (session files: tap.zp tap.page tap.key tap.diskwr tap.dumpmeta)",
     "frame cycles ts_ms ts_wall op pc a x y sp <A2GSPU_TAP_PTRS names>* operands[]?",
     "one line per PC hit from A2GSPU_TAP_PCS; ts_ms from A2GSPU_TAP_CYC_PER_MS; sink: the A2GSPU_TAP file"},
    {"WIZ5_EC19_OUT",     "t=e|x",
     "e: n sub src dst srcw? tbl? | x: n dst outidx dstw?",
     "SCREENDEV driver trap; the general form is Docs/A2GSPU_TRAP.md's ec19 spec"},
    {"WIZ5_DISKWR_OUT",   "op=BLKWR|BLKRD",
     "ts_ms op unit block len data",
     "UNITWRITE at $DF09 with X==1; UNITREAD logged once per block at $DF04"},
    {"WIZ5_CSP6_OUT",     "(no tag)",
     "ipc ts_ms sp page1 zp bufs[{at,ptr,d}]",
     "CSP-6 eval-stack dump; the general form is Docs/A2GSPU_TRAP.md's csp6 spec"},
    };
    n = (int)(sizeof(r) / sizeof(r[0]));
    return r;
}

inline const RecordSchema *record_schema_find(const char *rail) {
    int n = 0;
    const RecordSchema *r = record_schemas(n);
    for (int i = 0; i < n; i++) if (!strcmp(r[i].rail, rail)) return &r[i];
    return nullptr;
}

/* `rail schema [NAME]` -- bare lists the rails that write records; with a name,
 * that rail's record shape. Every row must name a registered rail, or the ack
 * is FAIL: a schema for a rail that does not exist is a stale claim. A rail
 * that writes no records says so, rather than answering with nothing. */
inline void format_rail_schema(char *buf, size_t bufsz, const char *name) {
    int n = 0;
    const RecordSchema *r = record_schemas(n);
    for (int i = 0; i < n; i++)
        if (!a2rails::rail_find(r[i].rail)) {
            snprintf(buf, bufsz, "status=FAIL schema-table-inconsistent rail=%s is not in the registry", r[i].rail);
            return;
        }
    if (!name || !*name) {
        size_t off = 0;
        int m = snprintf(buf, bufsz, "status=OK record-writing rails=%d |", n);
        if (m > 0) off = (size_t)m;
        for (int i = 0; i < n && off + 40 < bufsz; i++) {
            m = snprintf(buf + off, bufsz - off, " %s(%s)", r[i].rail, r[i].tag);
            if (m < 0) break;
            off += (size_t)m;
        }
        return;
    }
    if (!a2rails::rail_find(name)) {
        snprintf(buf, bufsz, "status=FAIL rail-unknown '%s' -- rail list names the %d this binary reads", name, a2rails::rail_count());
        return;
    }
    const RecordSchema *s = record_schema_find(name);
    if (!s) { snprintf(buf, bufsz, "status=FAIL no-records %s -- this rail writes no NDJSON records; its output is stdout lines or a binary artifact", name); return; }
    snprintf(buf, bufsz, "status=OK rail=%s tag=%s fields=%s note=%s", s->rail, s->tag, s->fields, s->note);
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
    // snapshot_class was the flat literal "A-cpu-mmu-softswitches", which stopped
    // being true once save started writing device blocks: a IIgs snapshot holds
    // adb, ensoniq, vgcint, iwm and pdblock, and this line told a consumer cold
    // that it held none of them. The class letter is now DERIVED per file
    // (a2gspu_snapshot.cpp a2gspu_snap_manifest_text) from Docs/INSTRUMENTATION_SHAPE.md M5's
    // definitions, so the only honest thing to state here is the rule and where
    // the per-file answer lives -- never a letter, which varies by platform and
    // by what the machine had when the save ran.
    fprintf(f, "snapshot_class: derived-per-file (A=cpu+mmu+softswitches+ram, B=+video/scanner, C=+adb/ensoniq/iwm)\n");
    fprintf(f, "snapshot_contract: the manifest's devices=/included=/excluded= lists, not the letter; read it from the save file's GSPM trailer or the restore ack\n");
    fprintf(f, "dual_mode: no-A2GSPU-env-is-stock-emulator\n");
    fprintf(f, "\n## observation_models\n");
    fprintf(f, "bits-mono,colour-4dot,colour-ntsc560,vram-raw,text-page,softswitch,");
    fprintf(f, "cpu-state,cycles-in-cmd,cycles-absolute-warn,shr-window,memory-peek,sdl-human\n");
    /* GENERATED FROM THE REGISTRY. This section used to be ten hand-written
     * fprintf literals naming 54 short forms, and it had done exactly what the
     * banner of Docs/A2GSPU_RAILS.md says a hand-maintained list in this file
     * did once before: drifted. It covered 53 of the 142 rails the binary reads,
     * and one name in it -- ITRACE -- was not a rail at all. The binary reads
     * A2GSPU_ITRACE_FROM/FRAME/N/LO/HI/OUT/REARM and no bare A2GSPU_ITRACE, so
     * the same process contradicted itself: `manifest <file>` listed ITRACE and
     * `rail find A2GSPU_ITRACE` answered status=FAIL rail-unknown. A declared
     * capability that does not exist is worse than an undocumented one, because
     * it will be believed -- a consumer setting A2GSPU_ITRACE=1 on the strength
     * of this list got silence and no error.
     *
     * So it is no longer a list. It is the registry's own table, grouped by the
     * registry's own family column, printed with the full A2GSPU_/MEMVU_/WIZ5_
     * names a caller must actually set. It cannot diverge from `rail env-only`
     * again because there is nothing left to diverge. */
    fprintf(f, "\n## env_rail_groups (generated from src/a2gspu_rails_registry.hpp; `rail env-only <file>` is the same table with synopses)\n");
    {
        int nr = 0;
        const a2rails::Rail *rr = a2rails::rails(nr);
        // Families in first-appearance order, so the grouping is stable across
        // builds for a consumer diffing two manifests.
        const char *seen[32]; int nseen = 0, dropped = 0;
        for (int i = 0; i < nr; i++) {
            bool have = false;
            for (int k = 0; k < nseen; k++) if (!strcmp(seen[k], rr[i].family)) { have = true; break; }
            if (have) continue;
            if (nseen < (int)(sizeof seen / sizeof seen[0])) seen[nseen++] = rr[i].family;
            else dropped++;   // never silently: see below
        }
        // A list that quietly omits part of itself is the failure this section
        // was rewritten to stop. If the family cap is ever hit, the manifest
        // says so in the manifest, where the consumer that would be misled by
        // it is looking.
        if (dropped)
            fprintf(f, "# INCOMPLETE: %d rail families beyond the %d-family cap are NOT listed below."
                       " Raise `seen[]` in a2gspu_manifest.hpp; use `rail env-only <file>` meanwhile.\n",
                    dropped, (int)(sizeof seen / sizeof seen[0]));
        for (int k = 0; k < nseen; k++) {
            fprintf(f, "%s:", seen[k]);
            for (int i = 0; i < nr; i++)
                if (!strcmp(rr[i].family, seen[k])) fprintf(f, " %s", rr[i].name);
            fprintf(f, "\n");
        }
        fprintf(f, "env_rail_total: %d\n", nr);
    }
    /* Generated from the reach table. This section used to be ten hand-written
     * lines, and its first one -- "env-only-today: GOLDEN ... DHGR_GOLDEN ...
     * ASSERT(env)" -- contradicted the reach table printed beside it, which
     * gives shr-golden, dhgr-golden and assert as live verbs for exactly those
     * rails. One table, printed two ways, cannot disagree with itself. */
    fprintf(f, "\n## env_only_rationale\n");
    fprintf(f, "# live: a CTRL verb reaches the rail mid-session (from the reach table).\n");
    fprintf(f, "# every other rail is launch-only: it arms from the environment at start.\n");
    {
        int nreach = 0;
        const a2rails::Reach *rt = a2rails::reach(nreach);
        for (int i = 0; i < nreach; i++)
            if (rt[i].verb && *rt[i].verb) fprintf(f, "live: %s via %s\n", rt[i].rail, rt[i].verb);
    }
    fprintf(f, "\n## record_schemas\n");
    fprintf(f, "# rail\ttag\tfields ('?' = optional)\tnote\n");
    {
        int ns = 0;
        const RecordSchema *rs = record_schemas(ns);
        for (int i = 0; i < ns; i++)
            fprintf(f, "%s\t%s\t%s\t%s\n", rs[i].rail, rs[i].tag, rs[i].fields, rs[i].note);
    }
    fprintf(f, "\n## ctrl_verbs\n");
    int n = 0;
    const Verb *v = verbs(&n);
    for (int i = 0; i < n; i++) {
        // Tab-separated with a when= column, so a client can parse the use case
        // rather than infer it. Absent entries emit "-" instead of being
        // omitted, so the column count is stable for a splitter.
        fprintf(f, "%s\tkind=%s\tmodel=%s\t%s\twhen=%s\n",
                v[i].name, v[i].kind, v[i].model, v[i].synopsis,
                verb_when(v[i].name));
    }
    fprintf(f, "\n## docs\n");
    fprintf(f, "Docs/AGENTIC_ORACLE.md\n");
    fprintf(f, "Docs/A2GSPU_RAILS.md\n");
    fprintf(f, "Docs/INSTRUMENTATION_AUDIT.md\n");
    fprintf(f, "Docs/DoubleHiRes.md\n");
    fprintf(f, "Docs/CLOSED_LOOP_INTEGRATION.md\n");
}

/* Every env rail this binary reads, emitted from the registry.
 *
 * This used to be ~36 hand-written fprintf lines out of the 128 rails the code
 * actually reads, with a footer pointing at Docs/A2GSPU_RAILS.md -- which was
 * itself describing only 96. Three copies of one fact, drifting independently,
 * and the two a client could reach were both short.
 *
 * Now there is one source of truth (a2gspu_rails_registry.hpp), enforced by
 * tools/a2rail/railparity.ps1, and this prints it. A rail added without a
 * registry entry fails that gate; a rail added with one appears here for free.
 *
 * WHAT THIS DOES NOT CLAIM: which rails have a live CTRL substitute. The old
 * curated list was genuinely launch-only, so it could say "not live CTRL";
 * printing all 128 makes that false, and the footer below has always named
 * substitutes that exist.
 *
 * A per-rail `live` column was tried and withdrawn, because inferring it from
 * names produces confident falsehoods: A2GSPU_LOADTRACE is a write-burst
 * tracker and has nothing to do with the `load` verb, A2GSPU_POKE_NTH fires on
 * the Nth hit of a PC where live `poke` writes immediately, and -- decisively --
 * the footer already records that the live watch is a SOFT watch, "not bus
 * WATCH", so every A2GSPU_WATCH* rail would have been marked substitutable when
 * it is not. A wrong substitute is worse than none: it sends a caller to a verb
 * that cannot do the job. If this column is wanted, derive it from the verb
 * implementations, not from the rail names.
 */
inline void write_env_only_rails(FILE *f) {
    fprintf(f, "# every env rail this binary reads, settable before start.\n");
    fprintf(f, "# Some have live CTRL substitutes (see the footer); this table\n");
    fprintf(f, "# does not claim which, because inferring it from names is wrong.\n");
    fprintf(f, "# source of truth: src/a2gspu_rails_registry.hpp"
               " (gate: tools/a2rail/railparity.ps1)\n");
    fprintf(f, "# columns: NAME <tab> FAMILY <tab> SYNOPSIS\n");
    fprintf(f, "schema\ta2rails-v1\n");
    fprintf(f, "count\t%d\n", a2rails::rail_count());
    int n = 0;
    const a2rails::Rail *r = a2rails::rails(n);
    for (int i = 0; i < n; i++)
        fprintf(f, "%s\t%s\t%s\n", r[i].name, r[i].family, r[i].synopsis);
    fprintf(f, "# CTRL live substitutes where available:\n");
    fprintf(f, "#   soft watch -> watch add peek:ADDR (not bus WATCH)\n");
    fprintf(f, "#   assert -> assert peek:ADDR==VAL\n");
    fprintf(f, "#   dhgr-golden -> same hash as A2GSPU_DHGR_GOLDEN (live mid-session)\n");
    fprintf(f, "#   load/poke/setreg -> live inject-ram\n");
    fprintf(f, "#   A2GSPU_LOAD -> SPIKE multi-file bank inject (IIe tiles.bin@6000)\n");
}


inline void write_limitations(FILE *f) {
    fprintf(f,"# a2gspu known limitations\n");
    fprintf(f,"schema\ta2limitations-v1\n");
    fprintf(f,"limitation\tid=adb-receive-bytes\tcomponent=ADB\tcommand=$48\tstatus=unimplemented\treason=argument-and-response-contract-unverified\trisk=command-stream-desync\n");
    fprintf(f,"limitation\tid=w5100-pppoe\tcomponent=Uthernet-II\tstatus=unimplemented\tregisters=accepted-readable\tsession=none\n");
    fprintf(f,"capability\tid=w5100-listen\tcomponent=Uthernet-II\tstatus=implemented\tgate=listencheck\n");
    fprintf(f,"limitation\tid=vidhd\tcomponent=VidHD\tstatus=detection-only\tfunctionality=none\n");
    fprintf(f,"limitation\tid=prodos-block-dead\tcomponent=prodos_block\tstatus=not-in-any-build-config\n");
}

inline void format_rail_ack(char *buf, size_t bufsz) {
    snprintf(buf, bufsz,
             "status=OK rails=%d families=run,observe,trace,break,manipulate,"
             "snapshot,assert,device,determinism,meta,calibrate,tap,memvu,wiz5,"
             "alias,ctrl | rail list [family|PREFIX] names them | "
             "rail find <NAME> gives one | rail env-only <file> writes the full "
             "table | rail live lists which rails a CTRL verb reaches and which read "
             "back, checked against the verb table | rail read <NAME> reads one | "
             "source: src/a2gspu_rails_registry.hpp",
             a2rails::rail_count());
}

/* `rail list [family|PREFIX]` -- names only, because 128 synopses do not fit an
 * ack. A filter that matches nothing answers FAIL rather than an empty OK: an
 * agent asking for a family that does not exist has made an error, and a bare
 * "status=OK" with no names would read as "this family is empty". */
inline void format_rail_list(char *buf, size_t bufsz, const char *filter) {
    int n = 0, shown = 0;
    const a2rails::Rail *r = a2rails::rails(n);
    bool all = !filter || !*filter || !strcmp(filter, "all");
    size_t flen = filter ? strlen(filter) : 0;
    size_t off = 0;
    int m = snprintf(buf, bufsz, "status=OK rail list %s",
                     all ? "all" : filter);
    if (m > 0) off = (size_t)m;
    for (int i = 0; i < n && off + 24 < bufsz; i++) {
        bool hit = all || !strcmp(r[i].family, filter) ||
                   (flen && !strncmp(r[i].name, filter, flen));
        if (!hit) continue;
        m = snprintf(buf + off, bufsz - off, " %s", r[i].name);
        if (m < 0) break;
        off += (size_t)m;
        shown++;
    }
    if (!shown)
        snprintf(buf, bufsz,
                 "status=FAIL rail-list-no-match '%s' -- not a family or name "
                 "prefix; try: rail list (bare) for all %d",
                 filter ? filter : "", a2rails::rail_count());
    else
        snprintf(buf + off, bufsz > off ? bufsz - off : 0, " count=%d", shown);
}

inline void format_rail_find(char *buf, size_t bufsz, const char *name) {
    const a2rails::Rail *r = a2rails::rail_find(name);
    if (!r)
        snprintf(buf, bufsz,
                 "status=FAIL rail-unknown '%s' -- this binary reads %d rails; "
                 "rail list names them", name ? name : "", a2rails::rail_count());
    else {
        const a2rails::Reach *rc = a2rails::reach_find(r->name);
        snprintf(buf, bufsz, "status=OK rail=%s family=%s live=%s read=%s synopsis=%s",
                 r->name, r->family,
                 (rc && rc->verb[0]) ? rc->verb : "launch-only",
                 (rc && rc->read[0]) ? rc->read : "none",
                 r->synopsis);
    }
}

// Is `name` the first word of a verb this binary implements?
inline bool verb_exists(const char *name) {
    int n = 0;
    const Verb *v = verbs(&n);
    for (int i = 0; i < n; i++) if (!strcmp(v[i].name, name)) return true;
    return false;
}

/* `rail live` -- the reach table, checked. Every row must name a rail the
 * registry declares and (when it names one) a verb the verb table implements;
 * one bad row fails the whole ack, because a reachability claim that might be
 * stale is the black box this table exists to remove. */
inline void format_rail_live(char *buf, size_t bufsz) {
    int n = 0, live = 0, readable = 0;
    const a2rails::Reach *r = a2rails::reach(n);
    for (int i = 0; i < n; i++) {
        if (!a2rails::rail_find(r[i].rail)) {
            snprintf(buf, bufsz, "status=FAIL reach-table-inconsistent rail=%s is not in the registry", r[i].rail);
            return;
        }
        if (r[i].verb[0] && !verb_exists(r[i].verb)) {
            snprintf(buf, bufsz, "status=FAIL reach-table-inconsistent rail=%s names verb '%s' which this binary does not implement",
                     r[i].rail, r[i].verb);
            return;
        }
        if (!strcmp(r[i].read, "fn") && !a2rails::reader_find(r[i].rail)) {
            snprintf(buf, bufsz, "status=FAIL reach-table-inconsistent rail=%s declares a reader (fn) that no a2rails::Registrar registered",
                     r[i].rail);
            return;
        }
        if (!strncmp(r[i].read, "verb:", 5)) {
            char first[32]; size_t k = 0;
            for (const char *p = r[i].read + 5; *p && *p != ' ' && k + 1 < sizeof first; p++) first[k++] = *p;
            first[k] = 0;
            if (!verb_exists(first)) {
                snprintf(buf, bufsz, "status=FAIL reach-table-inconsistent rail=%s read via verb '%s' which this binary does not implement",
                         r[i].rail, first);
                return;
            }
        }
        if (r[i].verb[0]) live++;
        if (r[i].read[0]) readable++;
    }
    // The other direction: a reader registered for a rail the table does not
    // declare readable is a reader nobody can find.
    for (int j = 0; j < a2rails::g_nreaders; j++) {
        const char *rail = a2rails::g_readers[j].rail;
        const a2rails::Reach *rc = a2rails::reach_find(rail);
        if (!a2rails::rail_find(rail) || !rc || strcmp(rc->read, "fn")) {
            snprintf(buf, bufsz, "status=FAIL reach-table-inconsistent a reader is registered for %s but the reach table does not declare fn for it", rail);
            return;
        }
    }
    size_t off = 0;
    int m = snprintf(buf, bufsz, "status=OK rails=%d live=%d readable=%d readers=%d launch-only=%d |",
                     a2rails::rail_count(), live, readable, a2rails::g_nreaders, a2rails::rail_count() - live);
    if (m > 0) off = (size_t)m;
    for (int i = 0; i < n && off + 40 < bufsz; i++) {
        m = snprintf(buf + off, bufsz - off, " %s:%s%s%s", r[i].rail,
                     r[i].verb[0] ? r[i].verb : "-", r[i].read[0] ? "/" : "", r[i].read);
        if (m < 0) break;
        off += (size_t)m;
    }
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
