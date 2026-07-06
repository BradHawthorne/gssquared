#!/usr/bin/env python3
"""gdiff.itrdiff -- execution-trace resync diff (ours vs pristine ITRACE).

Aligns two A2GSPU_ITRACE streams and reports the first structural divergences.
Compares PC + opcode + mnemonic + operand + @ea (effective address AND the memory
byte it reads) so a control-flow change, a wrong branch target, or a wrong DATA VALUE
each classify distinctly. Registers are shown for context.

Two robustness features that the campaign needed:
  * SHIFT-TOLERANT (--nopc): drop the absolute code PC and branch->target so a pure
    LAYOUT shift (same logic at a different address) does not read as a divergence --
    only opcode/mnemonic/operand/@ea are compared.
  * INTERRUPT-SKEW RESYNC: a two-pointer walk that, on a divergence, searches a small
    window for a skip on either side that re-establishes a run of matching lines --
    so ONE extra instruction (an IRQ/COP entry, an extra poll) is reported as "N extra
    line(s)" instead of cascading into thousands of false diffs.

Classification at each divergence:
  DATA-VAL    same PC+instr+@addr, different byte read  -> a data/state difference
  DATA-ADDR   same PC+instr, different effective address-> a pointer/index difference
  CONTROL/PC  different PC or instruction               -> a control-flow difference

Standalone:
    python itracediff.py ours.itr pristine.itr [--nopc] [--max N] [--max-skip K]
As a lib:
    from itracediff import load_itrace, resync_report
"""
import sys, os, re, argparse

VERSION = "1.0"

_RX = re.compile(r"ITRACE (\S+): (\S\S) (\S+)\s+(\S*)")
_RXEA = re.compile(r"@([0-9A-Fa-f]+)=([0-9A-Fa-f]+)")


def load_itrace(path):
    return [l.rstrip("\n") for l in open(path) if l.startswith("ITRACE")]


def key(line, nopc):
    m = _RX.match(line)
    if not m:
        return (line,)                       # unparseable -> compare raw
    pc, op, mn, opnd = m.group(1), m.group(2), m.group(3), m.group(4)
    me = _RXEA.search(line)
    ea = (me.group(1) + "=" + me.group(2)) if me else ""
    if nopc:
        return (op, mn, opnd, ea)            # layout-shift tolerant
    return (pc, op, mn, opnd, ea)


def eaval(line):
    me = _RXEA.search(line)
    return me.groups() if me else (None, None)


def classify(o, p, nopc):
    ko, kp = key(o, nopc), key(p, nopc)
    # instruction identity = every key field EXCEPT the trailing @ea (last element).
    # Same identity, different @ea => a DATA divergence; else a CONTROL/PC divergence.
    if len(ko) == len(kp) and ko[:-1] == kp[:-1]:
        oa, ov = eaval(o)
        pa, pv = eaval(p)
        return "DATA-VAL" if oa == pa else "DATA-ADDR"
    return "CONTROL/PC"


def _find_resync(O, i, P, j, max_skip, run):
    """Return (skip_o, skip_p) that re-establishes a run of `run` matching keys, with
    the smallest total skip; (0,0) if none found in the window. nopc is applied by the
    caller through the key cache."""
    def match_run(a, ia, b, jb):
        if ia + run > len(a) or jb + run > len(b):
            return False
        return all(a[ia + k] == b[jb + k] for k in range(run))
    for total in range(1, max_skip + 1):
        for sp in range(0, total + 1):
            so = total - sp
            if match_run(O, i + so, P, j + sp):
                return so, sp
    return 0, 0


def resync_report(O, P, nopc=False, max_div=3, max_skip=20, run=4, ctx=4, out=sys.stdout):
    """Two-pointer resync align. Reports up to max_div divergences with context and
    interrupt-skew notes. Returns the count of divergences reported."""
    out.write(f"# gdiff itrdiff v{VERSION}  ours {len(O)} lines, pris {len(P)} lines"
              f"  ({'shift-tolerant --nopc' if nopc else 'PC-exact'})\n")
    # precompute keys (also lets _find_resync compare cheaply)
    KO = [key(l, nopc) for l in O]
    KP = [key(l, nopc) for l in P]
    i = j = shown = 0
    while i < len(O) and j < len(P):
        if KO[i] == KP[j]:
            i += 1; j += 1
            continue
        tag = classify(O[i], P[j], nopc)
        shown += 1
        out.write(f"\n#### DIVERGENCE #{shown} at ours[{i}] / pris[{j}]  [{tag}]\n")
        out.write(f"OURS[{i}]: {O[i]}\n")
        out.write(f"PRIS[{j}]: {P[j]}\n")
        out.write("--- ours context:\n")
        for k in range(max(0, i - ctx), min(len(O), i + ctx + 1)):
            out.write(("  * " if k == i else "    ") + O[k] + "\n")
        out.write("--- pris context:\n")
        for k in range(max(0, j - ctx), min(len(P), j + ctx + 1)):
            out.write(("  * " if k == j else "    ") + P[k] + "\n")
        # try to resync so we don't cascade
        so, sp = _find_resync(KO, i, KP, j, max_skip, run)
        if so == 0 and sp == 0:
            out.write("   (no resync within window -- streams structurally diverged here)\n")
            if shown >= max_div:
                break
            i += 1; j += 1
        else:
            if so and not sp:
                out.write(f"   >> resync: {so} extra line(s) on OURS (interrupt/skew?), skip them\n")
            elif sp and not so:
                out.write(f"   >> resync: {sp} extra line(s) on PRIS (interrupt/skew?), skip them\n")
            else:
                out.write(f"   >> resync: skip OURS+{so} / PRIS+{sp} to re-align\n")
            i += so; j += sp
            if shown >= max_div:
                out.write(f"\n(stopping after {max_div} divergences; raise --max to see more)\n")
                break
    if shown == 0:
        out.write("NO divergence -- streams align (up to a resync of the tail).\n")
    return shown


def cmd_itrdiff(argv):
    ap = argparse.ArgumentParser(prog="gdiff itrdiff",
        description="Execution-trace resync diff of two ITRACE streams.")
    ap.add_argument("ours"); ap.add_argument("pris")
    ap.add_argument("--nopc", action="store_true", help="layout-shift tolerant (drop PC)")
    ap.add_argument("--max", type=int, default=3, help="max divergences to report")
    ap.add_argument("--max-skip", type=int, default=20, help="resync search window")
    ap.add_argument("--run", type=int, default=4, help="matching run length to confirm resync")
    ap.add_argument("--ctx", type=int, default=4, help="context lines")
    a = ap.parse_args(argv)
    for pth in (a.ours, a.pris):
        if not os.path.isfile(pth):
            print(f"gdiff itrdiff: missing file '{pth}'", file=sys.stderr)
            return 2
    resync_report(load_itrace(a.ours), load_itrace(a.pris), nopc=a.nopc,
                  max_div=a.max, max_skip=a.max_skip, run=a.run, ctx=a.ctx)
    return 0


if __name__ == "__main__":
    sys.exit(cmd_itrdiff(sys.argv[1:]))
