#!/usr/bin/env python3
"""gdiff.watchspec -- WATCH spec format-checker + probe_peek/$EE lying-sensor lint.

TWO campaign lying sensors, mechanized away:

1. THE "WATCH DIDN'T FIRE" TRAP.  A2GSPU_WATCH wants `bank:lo-hi` (e.g. E1:19A0-19A3).
   The emulator parses it with strtoul-bank / ':' / strtoul-lo / '-' / strtoul-hi.
   Pass a BARE 24-bit address like `E119A0` and strtoul eats the whole value as
   `bank`, lo/hi default to 0, and the armed range becomes ($19A00000) which no real
   address ever hits -> the watch silently never fires and you conclude "the write
   never happened" (a false graveyard). This checker reproduces the emulator's parse
   EXACTLY and refuses/warns on the bare-24-bit (and bank>$FF, lo>hi, >16-bit offset)
   cases before you waste a boot on a dead watchpoint.

2. THE probe_peek $EE LIAR.  probe_peek of banks $00/$01 can return floating-bus $EE
   for handler/relocated pages that read_raw cannot see. A sensor that reads $EE from
   bank $00/$01 and reports it as fact lied ~6x in the campaign. probe_suspect()/the
   --probe-lint mode flag such reads as SUSPECT rather than trusting them.

Standalone:
    python watchspec.py "E1:19A0-19A3"          # validate (exit 1 if any warning)
    python watchspec.py "E119A0"                # -> BARE-24-BIT trap, exit 1
    python watchspec.py --probe-lint watch.ndjson
As a lib:
    from watchspec import validate_watch_spec, probe_suspect
"""
import sys, os, json, argparse

VERSION = "1.0"


def _strtoul_hex(s, i):
    """Mimic C strtoul(base=16): skip leading spaces, consume hex digits.
    Returns (value, new_index, consumed_any)."""
    n = len(s)
    while i < n and s[i] in " \t":
        i += 1
    j = i
    val = 0
    while j < n and s[j] in "0123456789abcdefABCDEF":
        val = val * 16 + int(s[j], 16)
        j += 1
    return (val, j, j > i)


def parse_watch_like_emulator(spec):
    """Reproduce gs2.cpp's A2GSPU_WATCH parse byte-for-byte, but RECORD whether a
    ':' and '-' were actually seen for each range (the emulator throws that away --
    which is exactly why the bare-24-bit trap is silent). Returns a list of dicts:
       {bank, lo, hi, saw_colon, saw_dash, lo_present, armed_lo, armed_hi}
    armed_* are the 32-bit values the emulator would actually load."""
    ranges = []
    p = 0
    n = len(spec)
    while p < n and len(ranges) < 8:
        bank, p, _ = _strtoul_hex(spec, p)
        saw_colon = p < n and spec[p] == ":"
        if saw_colon:
            p += 1
        lo, p, lo_present = _strtoul_hex(spec, p)
        saw_dash = p < n and spec[p] == "-"
        if saw_dash:
            p += 1
        hi, p, _ = _strtoul_hex(spec, p)
        armed_lo = ((bank << 16) | (lo & 0xFFFF)) & 0xFFFFFFFF
        armed_hi = ((bank << 16) | (hi & 0xFFFF)) & 0xFFFFFFFF
        ranges.append(dict(bank=bank, lo=lo, hi=hi, saw_colon=saw_colon,
                           saw_dash=saw_dash, lo_present=lo_present,
                           armed_lo=armed_lo, armed_hi=armed_hi))
        while p < n and spec[p] in ", ":
            p += 1
    return ranges


def validate_watch_spec(spec):
    """Return (ok, results) where results is a list of per-range dicts with
    'warnings' (list of strings). ok is False if any range has a warning."""
    results = []
    ok = True
    for r in parse_watch_like_emulator(spec):
        w = []
        if not r["saw_colon"]:
            w.append(f"BARE-24-BIT TRAP: no ':' -- the emulator reads "
                     f"${r['bank']:X} as the BANK and arms ${r['armed_lo']:08X}, which "
                     f"never fires. Use bank:lo-hi (e.g. E1:19A0-19A3).")
        if r["bank"] > 0xFF:
            w.append(f"bank ${r['bank']:X} > $FF -- a bank is 8 bits; did you paste a "
                     f"full 24-bit address as the bank field?")
        if not r["saw_dash"] or not r["lo_present"]:
            w.append("no '-hi' -- single-address watch arms a 1-byte range "
                     "[lo,lo]; if intended, ignore; else use lo-hi.")
        elif r["lo"] > r["hi"]:
            w.append(f"lo ${r['lo']:X} > hi ${r['hi']:X} -- inverted/empty range, "
                     "never fires.")
        if r["lo"] > 0xFFFF or r["hi"] > 0xFFFF:
            w.append(f"offset > $FFFF (lo=${r['lo']:X} hi=${r['hi']:X}) -- the "
                     "in-bank offset is 16 bits; high bits are dropped.")
        if w:
            ok = False
        results.append(dict(bank=r["bank"], lo=r["lo"], hi=r["hi"],
                            armed_lo=r["armed_lo"], armed_hi=r["armed_hi"],
                            warnings=w))
    if not results:
        ok = False
        results.append(dict(bank=0, lo=0, hi=0, armed_lo=0, armed_hi=0,
                            warnings=["empty spec -- nothing to watch."]))
    return ok, results


def probe_suspect(addr, val):
    """True when a probe_peek reading is a SUSPECTED floating-bus liar: banks
    $00/$01 returning $EE (handler/relocated pages read_raw cannot see)."""
    return ((addr >> 16) & 0xFF) in (0x00, 0x01) and (val & 0xFF) == 0xEE


def probe_lint_ndjson(path):
    """Scan a WATCH/dump NDJSON for bank $00/$01 reads returning $EE and flag them
    as suspect. Returns (n_events, suspects[list])."""
    suspects = []
    n = 0
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            try:
                j = json.loads(ln)
            except Exception:
                continue
            if j.get("event") != "watch":
                continue
            n += 1
            if probe_suspect(int(j.get("addr", 0)), int(j.get("data", 0))):
                suspects.append(j)
    return n, suspects


def cmd_watchspec(argv):
    ap = argparse.ArgumentParser(prog="gdiff watchspec",
        description="Validate an A2GSPU_WATCH spec (bank:lo-hi) or lint a probe NDJSON.")
    ap.add_argument("spec", nargs="?", help='WATCH spec, e.g. "E1:19A0-19A3"')
    ap.add_argument("--probe-lint", metavar="NDJSON",
                    help="scan a WATCH NDJSON for bank $00/$01 $EE floating-bus liars")
    ap.add_argument("--quiet", action="store_true", help="only print on problems")
    a = ap.parse_args(argv)

    rc = 0
    if a.probe_lint:
        n, suspects = probe_lint_ndjson(a.probe_lint)
        print(f"# gdiff watchspec v{VERSION} probe-lint {a.probe_lint}: {n} watch events")
        if suspects:
            rc = 1
            print(f"** {len(suspects)} SUSPECT probe read(s): bank $00/$01 returned $EE "
                  "(floating bus) -- do NOT trust these as memory truth **")
            for s in suspects[:20]:
                print(f"   addr=${int(s['addr']):06X} data=$EE pc=${int(s['pc']):06X}")
        else:
            print("   no bank $00/$01 $EE floating-bus reads -- clean")
    if a.spec:
        ok, results = validate_watch_spec(a.spec)
        if not (a.quiet and ok):
            print(f"# gdiff watchspec v{VERSION} spec='{a.spec}'  {'OK' if ok else 'PROBLEMS'}")
            for r in results:
                print(f"  range bank=${r['bank']:X} lo=${r['lo']:X} hi=${r['hi']:X} "
                      f"-> armed [{r['armed_lo']:08X}..{r['armed_hi']:08X}]")
                for w in r["warnings"]:
                    print(f"    !! {w}")
        if not ok:
            rc = 1
    if not a.spec and not a.probe_lint:
        ap.print_help()
        rc = 2
    return rc


if __name__ == "__main__":
    sys.exit(cmd_watchspec(sys.argv[1:]))
