#!/usr/bin/env python3
"""scout_sym -- wall symbolizer + classifier for the golden-splice-scout (G4).

Two jobs, both READ-ONLY:
  resolve <pc24hex>      nearest kernel symbol at-or-below a PC (name+offset), from
                         kernel.sym.dbg. Names a raw BRK/stall PC the emulator did
                         not already symbolize.
  classify               read a wall-telemetry JSON blob on stdin (produced by
                         scout.ps1) and print a WALL CLASS + a next-step class hint,
                         cross-referencing the milestone ledger (on-path next).

The class hint is the "spike the path, then pave" compass: for a STALL it names the
first on-path milestone NOT yet reached (what the splice must unblock); for a BRK/HANG
/WILD it names the fault class and the instrument to reach for next.

Usage:  python scout_sym.py resolve 00FA42 [--sym <kernel.sym.dbg>]
        python scout_sym.py classify [--sym <..>] [--milestones <..>] < telemetry.json
"""
import sys, os, re, json

DEF_SYM  = "D:/projects/a2vga/m0/_kernel/kernel.sym.dbg"
DEF_MILE = "D:/projects/a2vga/m0/_kernel/kernel.milestones.txt"

# kernel.sym.dbg lines look like:
#   symbol "map_vrn" type LABEL addr $00FA42 file 0 line 0
# (the emulator's loader also accepts a bare "HHHHHH name"; support both.)
_SYM_RE  = re.compile(r'symbol\s+"([^"]+)"\s+type\s+\S+\s+addr\s+\$?([0-9A-Fa-f]+)')
_BARE_RE = re.compile(r'^\s*([0-9A-Fa-f]{4,6})\s+(\S+)')


def load_syms(path):
    syms = []
    try:
        with open(path, "r", errors="replace") as f:
            for ln in f:
                m = _SYM_RE.search(ln)
                if m:
                    syms.append((int(m.group(2), 16) & 0xFFFFFF, m.group(1)))
                    continue
                m = _BARE_RE.match(ln)
                if m:
                    syms.append((int(m.group(1), 16) & 0xFFFFFF, m.group(2)))
    except OSError:
        return []
    syms.sort()
    return syms


def resolve(syms, pc):
    """Nearest symbol at-or-below pc within the same bank; returns 'name+$off' or ''."""
    pc &= 0xFFFFFF
    best = None
    for addr, name in syms:
        if addr <= pc and (addr >> 16) == (pc >> 16):
            if best is None or addr > best[0]:
                best = (addr, name)
    if best is None:
        return ""
    off = pc - best[0]
    return best[1] if off == 0 else f"{best[1]}+${off:X}"


def load_milestones(path):
    """[(addr, name)] in boot order (file order == execution order)."""
    out = []
    try:
        with open(path, "r", errors="replace") as f:
            for ln in f:
                m = re.match(r'\s*([0-9A-Fa-f]+)\s+(\S+)', ln)
                if m:
                    out.append((int(m.group(1), 16) & 0xFFFFFF, m.group(2)))
    except OSError:
        pass
    return out


def _is_death(name):
    return "death" in name.lower() or "fatal" in name.lower()


def classify(tel, syms, miles):
    """tel: dict from scout.ps1. Returns (class, [report lines])."""
    reached = set(tel.get("reachedNames", []) or [])
    ms_r = int(tel.get("msReached", -1))
    ms_t = int(tel.get("msTotal", -1))
    brk = int(tel.get("brk", -1))
    status = (tel.get("status") or "?").upper()
    hang = bool(tel.get("hang"))
    wild = tel.get("wildPc") or ""
    wall = tel.get("wallPc") or ""       # already bank/addr, e.g. "E0/EC08"
    wallsym = tel.get("wallSym") or ""

    # first on-path milestone not yet reached (skip death milestones for the "next" pick)
    next_ms = None
    for addr, name in miles:
        if name not in reached:
            if _is_death(name):
                continue
            next_ms = (addr, name)
            break

    def pcsym(pcstr):
        if not pcstr:
            return ""
        m = re.match(r'([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4})', pcstr)
        if not m:
            return ""
        return resolve(syms, (int(m.group(1), 16) << 16) | int(m.group(2), 16))

    lines = []
    # ---- pick the wall class ----
    if status == "OK" and (ms_t < 0 or ms_r >= ms_t):
        cls = "COMPLETE"
        lines.append("no wall -- boot ran to completion (all milestones, status=OK).")
        return cls, lines

    if brk > 0 or status == "CRASH_BRK":
        cls = "BRK"
        sym = wallsym or pcsym(wall)
        lines.append(f"crash BRK/COP at {wall or '?'} {sym}".rstrip())
        lines.append("class hint: a crash wall -- symbolize the BRK PC (above); if it is a "
                     "$00 return target, RETGUARD names the bad RTS/RTL caller. Base-loss / "
                     "wild-return family. Splice: POKE the golden operand bytes at the faulting site.")
    elif hang or status == "HANG":
        cls = "HANG"
        sym = pcsym(wall)
        lines.append(f"degenerate loop / wild code at {wall or '?'} {sym}".rstrip())
        lines.append("class hint: no-BRK spin. Splice: POKE past the loop (force-branch PC=, or "
                     "correct the operand the branch tests).")
    elif wild and not (next_ms):
        cls = "WILD"
        lines.append(f"wild jump into non-code bank at {wild}")
        lines.append("class hint: a bad vector / return. RETGUARD + the BRKHIST ring name the caller.")
    else:
        cls = "STALL"
        sym = wallsym or pcsym(wall)
        lines.append(f"no-BRK stall at {wall or '?'} {sym}  (milestones {ms_r}/{ms_t})".rstrip())
        if next_ms:
            lines.append(f"on-path NEXT milestone (not reached): {next_ms[1]} (${next_ms[0]:06X})")
            lines.append(f"class hint: the boot is wedged BEFORE {next_ms[1]}. Splice: spike the "
                         f"path -- POKE force PB/PC to ${next_ms[0]:06X} to map what {next_ms[1]} "
                         f"needs, or wdiff the data feeding it (e.g. the $E1 SCM queue) and POKE "
                         f"the golden bytes. The durable fix pairs with a build-time base-loss audit.")
        else:
            lines.append("class hint: milestones incomplete but no clear on-path next -- inspect "
                         "the NOT-REACHED list + SPIKE-END PC.")
        if wild:
            lines.append(f"(secondary signal: a transient WILDPC at {wild} was logged this run.)")
    return cls, lines


def cmd_resolve(argv):
    sympath = DEF_SYM
    pcs = []
    i = 0
    while i < len(argv):
        if argv[i] == "--sym":
            sympath = argv[i + 1]; i += 2
        else:
            pcs.append(argv[i]); i += 1
    if not pcs:
        print("usage: scout_sym.py resolve <pc24hex> [--sym file]", file=sys.stderr); return 2
    syms = load_syms(sympath)
    for p in pcs:
        pc = int(p.replace("$", "").replace("/", ""), 16)
        r = resolve(syms, pc)
        print(f"{pc:06X}  {r or '<no symbol>'}")
    return 0


def cmd_classify(argv):
    sympath, milepath = DEF_SYM, DEF_MILE
    i = 0
    while i < len(argv):
        if argv[i] == "--sym":
            sympath = argv[i + 1]; i += 2
        elif argv[i] == "--milestones":
            milepath = argv[i + 1]; i += 2
        else:
            i += 1
    raw = sys.stdin.read()
    try:
        tel = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError as e:
        print(f"scout_sym: bad telemetry JSON: {e}", file=sys.stderr); return 2
    syms = load_syms(sympath)
    miles = load_milestones(milepath)
    cls, lines = classify(tel, syms, miles)
    print(f"WALL CLASS: {cls}")
    for ln in lines:
        print(f"  {ln}")
    return 0


def main(argv):
    if not argv or argv[0] in ("-h", "--help"):
        print(__doc__); return 0
    sub, rest = argv[0], argv[1:]
    if sub == "resolve":
        return cmd_resolve(rest)
    if sub == "classify":
        return cmd_classify(rest)
    print(f"scout_sym: unknown subcommand '{sub}' (resolve|classify)", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
