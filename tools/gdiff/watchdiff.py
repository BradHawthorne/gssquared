#!/usr/bin/env python3
"""gdiff.wdiff -- WATCH-sequence ($E1 queue) positional align + divergence decode.

Aligns two A2GSPU_WATCH NDJSON streams (ours vs golden/pristine) POSITIONALLY -- the
Nth watched write on each side -- and reports the first divergence plus the write-count
delta. This is the tool that localized the SCM notify-queue bug: ours writes 32 nodes,
pristine 64, and the streams are byte-identical through write-23 then diverge at
write-24 where ours links a record at $E06014 (bogus bank-$E0 SHR RAM) vs pristine
$E119D0.

Value-add over the scratchpad one-off:
  * skips non-"watch" NDJSON events (a {"event":"suppressed"} cap line used to KeyError);
  * --field lo-hi (default: auto from the divergence addr) reconstructs the little-endian
    POINTER being built across consecutive byte-writes and prints it for BOTH sides at the
    divergence -> you get "$E06014 vs $E119D0" without decoding bytes by hand;
  * robust CLI (explicit files or a scratchpad dir with the legacy names).

Standalone:
    python watchdiff.py ours.ndjson pristine.ndjson
    python watchdiff.py <dir>            # dir/watch_e119a0.ndjson vs dir/watch_pris.ndjson
As a lib:
    from watchdiff import load_watch, align_report
"""
import sys, os, json, argparse

VERSION = "1.0"


def load_watch(path):
    """Load a WATCH NDJSON -> list of (pc24, addr24, data). Skips non-watch events."""
    out = []
    with open(path) as f:
        for ln in f:
            ln = ln.strip()
            if not ln:
                continue
            try:
                j = json.loads(ln)
            except Exception:
                continue
            if j.get("event") != "watch":       # skip 'suppressed'/other -> no KeyError
                continue
            out.append((int(j["pc"]) & 0xFFFFFF, int(j["addr"]) & 0xFFFFFF, int(j["data"]) & 0xFF))
    return out


def _decode_field(writes, start, lo16, hi16):
    """Assemble the little-endian value of the byte field [lo16,hi16] using the FIRST
    write to each offset at/after index `start`. Returns (value, nbytes, complete)."""
    seen = {}
    for pc, addr, data in writes[start:]:
        off = addr & 0xFFFF
        if lo16 <= off <= hi16 and off not in seen:
            seen[off] = data
            if len(seen) == (hi16 - lo16 + 1):
                break
    val = 0
    for k in range(hi16 - lo16 + 1):
        val |= seen.get(lo16 + k, 0) << (8 * k)
    return val, len(seen), len(seen) == (hi16 - lo16 + 1)


def align_report(ours, pris, field="auto", ctx=3, out=sys.stdout):
    """Positional align + first-divergence report. Returns the divergence index
    (or None if identical up to min length)."""
    out.write(f"# gdiff wdiff v{VERSION}\n")
    out.write(f"ours {len(ours)} writes   pristine {len(pris)} writes"
              f"   (delta {len(ours) - len(pris):+d})\n")
    n = max(len(ours), len(pris))
    first = None
    def fmt(x):
        return f"{x[0]:06X}/{x[1] & 0xFFFF:04X}={x[2]:02X}" if x else "----------"
    for i in range(n):
        o = ours[i] if i < len(ours) else None
        p = pris[i] if i < len(pris) else None
        diff = (o != p)
        if diff and first is None:
            first = i
        # print a compact window around the divergence + the tail delta
        show = (first is None) or abs(i - first) <= max(ctx, 6) or i >= min(len(ours), len(pris))
        if show:
            out.write(f"{i:>3}  OURS {fmt(o):<18} PRIS {fmt(p):<18}{'  <-- DIFF' if diff else ''}\n")
    if first is None:
        out.write("NO divergence in the aligned prefix (identical up to min length).\n")
        return None

    # --- record-pointer decode at the divergence (the by-hand step, automated) ---
    o = ours[first] if first < len(ours) else None
    p = pris[first] if first < len(pris) else None
    da = (o or p)[1] & 0xFFFF
    if field == "auto":
        lo16 = da & ~0x3
        hi16 = lo16 + 3
    elif field:
        a, b = field.split("-")
        lo16, hi16 = int(a, 16), int(b, 16)
    else:
        lo16 = hi16 = None
    out.write(f"\n#### FIRST DIVERGENCE at aligned write {first}\n")
    if o: out.write(f"OURS[{first}]: {fmt(o)}\n")
    if p: out.write(f"PRIS[{first}]: {fmt(p)}\n")
    if lo16 is not None:
        ov, on, oc = _decode_field(ours, first, lo16, hi16)
        pv, pn, pc = _decode_field(pris, first, lo16, hi16)
        out.write(f"RECORD-POINTER DECODE of field ${lo16:04X}-${hi16:04X} "
                  f"(little-endian, first writes at/after the divergence):\n")
        out.write(f"    OURS = ${ov & 0xFFFFFF:06X}"
                  f"{'' if oc else ' (INCOMPLETE)'}\n")
        out.write(f"    PRIS = ${pv & 0xFFFFFF:06X}"
                  f"{'' if pc else ' (INCOMPLETE)'}\n")
        if oc and pc and ov != pv:
            out.write(f"    -> ours links a record at ${ov & 0xFFFFFF:06X} "
                      f"vs pristine ${pv & 0xFFFFFF:06X}\n")
    return first


def cmd_wdiff(argv):
    ap = argparse.ArgumentParser(prog="gdiff wdiff",
        description="Positional align of two WATCH NDJSON streams + divergence decode.")
    ap.add_argument("a", help="ours NDJSON, or a directory holding the legacy pair")
    ap.add_argument("b", nargs="?", help="pristine/golden NDJSON")
    ap.add_argument("--field", default="auto",
                    help='pointer field lo-hi hex (default auto = divergence addr &~3), '
                         'e.g. 19A0-19A3; "none" to skip decode')
    ap.add_argument("--ctx", type=int, default=3, help="context rows around divergence")
    a = ap.parse_args(argv)

    if a.b is None and os.path.isdir(a.a):
        ours_p = os.path.join(a.a, "watch_e119a0.ndjson")
        pris_p = os.path.join(a.a, "watch_pris.ndjson")
    else:
        if a.b is None:
            ap.error("need two NDJSON files (or one directory with the legacy pair)")
        ours_p, pris_p = a.a, a.b
    for pth in (ours_p, pris_p):
        if not os.path.isfile(pth):
            print(f"gdiff wdiff: missing file '{pth}'", file=sys.stderr)
            return 2
    field = None if a.field == "none" else a.field
    align_report(load_watch(ours_p), load_watch(pris_p), field=field, ctx=a.ctx)
    return 0


if __name__ == "__main__":
    sys.exit(cmd_wdiff(sys.argv[1:]))
