#!/usr/bin/env python3
"""gdiff.overlay -- overlay-aware OMF segment symbolizer (the SIG-031 anti-liar core).

WHY THIS EXISTS (the lying-sensor lesson, mechanized)
-----------------------------------------------------
During the owned-GS/OS boot campaign a *dest-keyed dict* silently collapsed two
segments that both load to $01D000 (seg1 Loader_LC and seg12 init3). The rig then
mislabeled init3's bytes as `check_express_seg` (SIG-031), burned a /debug cycle and
spawned two graveyards (G-015/G-016). The failure was NOT the analysis -- it was a
sensor that answered a question ("what lives at $01D000?") with a single confident
name when the honest answer is "TWO things, and only one is live".

This module is the durable, index-keyed replacement for that dict:
  * walk_omf()      -- POSITIONAL walk. Segments are keyed by their load INDEX, never
                       by dest. Two segments at one dest stay two distinct records.
  * find_dup_dests()-- the dup-destination OVERLAY detector. Any dest that appears
                       more than once is surfaced LOUDLY -- the caller must not pick one.
  * overlay_symbolize()/name_at() -- overlay-aware symbolization: for a runtime address
                       covered by >1 segment, return BOTH names and, if an ITRACE PC set
                       is supplied, FLAG the live overlay (the one actually executing
                       there) instead of silently guessing.

Never key a lookup on dest. If you find yourself writing `d[dest] = seg`, you are
re-introducing the SIG-031 liar; use the index-keyed records here instead.

Standalone:  python overlay.py <ours.gsos> [golden.gsos] [--addr BANK:OFF ...] [--itrace file]
As a lib:    from overlay import walk_omf, find_dup_dests, overlay_symbolize
"""
import sys, os, json, argparse, re

VERSION = "1.0"

# ---------------------------------------------------------------------------
# Known GS.OS segment names, keyed by (dest) -> either a single name or, for the
# KNOWN OVERLAYS, a list keyed by load index. This table NEVER collapses an
# overlay: a colliding dest maps to a per-index list so both names survive.
# Source: design/signatures.md + m0 scratchpad segdiff/orgmap (owned GS/OS 6.0.1).
# ---------------------------------------------------------------------------
_SINGLE_NAMES = {
    0x01A600: "Loader",     0x009A00: "oscall_seg", 0x00B300: "misc_seg",
    0x00D000: "scm_main",   0x01FC00: "system_svc", 0xE1D980: "bank_e1",
    0x00AA00: "b00segr",    0xE0E000: "be0segr",    0x00A280: "cache",
    0x00B200: "init1",      0x00D400: "init2",      0xE0D400: "init4",
}
# dest -> {load_index: name}. The canonical overlay is $01D000 (SIG-031).
_OVERLAY_NAMES = {
    0x01D000: {1: "Loader_LC (seg1)", 12: "init3 (seg12)"},
}


def seg_name(dest, idx):
    """Return the human name for a segment, disambiguated by load index when the
    dest is a known overlay. Never collapses an overlay to one name."""
    if dest in _OVERLAY_NAMES:
        return _OVERLAY_NAMES[dest].get(idx, f"overlay@${dest:06X}#idx{idx}")
    return _SINGLE_NAMES.get(dest, f"seg@${dest:06X}")


def walk_omf(path):
    """POSITIONAL walk of a GS.OS restart-header image. Returns a list of segment
    records, one per load, keyed by list position (== load index). Never dest-keyed.

    Each record: dict(idx, dest, length, off, hdr, data).
    """
    d = open(path, "rb").read()
    def u16(o): return d[o] | (d[o + 1] << 8)
    def u32(o): return d[o] | (d[o + 1] << 8) | (d[o + 2] << 16) | (d[o + 3] << 24)
    segs = []
    p = 0
    idx = 0
    while p + 0x30 <= len(d):
        length = u16(p)
        if length == 0xFFFF:          # terminator
            break
        dest = u32(p + 2) & 0xFFFFFF
        segs.append(dict(idx=idx, dest=dest, length=length, off=p,
                         hdr=d[p:p + 0x30], data=d[p + 0x30:p + 0x30 + length]))
        p += 0x30 + length
        idx += 1
    return segs


def find_dup_dests(segs):
    """Return {dest: [idx, ...]} for every dest that >1 segment loads to.
    A non-empty result means OVERLAYS are present -- callers must disambiguate by
    index (+ ITRACE), never pick one silently."""
    by_dest = {}
    for s in segs:
        by_dest.setdefault(s["dest"], []).append(s["idx"])
    return {dest: idxs for dest, idxs in by_dest.items() if len(idxs) > 1}


def warn_dups(segs, stream=sys.stdout):
    """Print the loud dup-destination OVERLAY WARNING (returns True if any)."""
    dups = find_dup_dests(segs)
    if dups:
        pretty = ", ".join(
            f"${dest:06X} <- segs {idxs} [{ ' | '.join(seg_name(dest, i) for i in idxs) }]"
            for dest, idxs in sorted(dups.items()))
        stream.write(
            f"** DUP-DEST OVERLAYS (index-keyed; do NOT key a lookup on dest): {pretty} **\n")
        stream.write(
            "   -> name BOTH; pass --itrace to flag the LIVE overlay. (SIG-031 liar class.)\n")
    return bool(dups)


def segs_covering(segs, addr):
    """All segment records whose [dest, dest+length) window contains `addr`
    (an overlay yields >1). Positional, so overlays are never merged."""
    return [s for s in segs if s["dest"] <= addr < s["dest"] + s["length"]]


def load_itrace_ops(path):
    """Collect {pc24 -> opcode_byte} from an ITRACE file: the byte actually FETCHED
    and executed at each PC. This is the evidence that flags the LIVE overlay --
    two overlays share an address WINDOW, so only the executed CONTENT distinguishes
    them (window-containment cannot: both windows contain the same PCs)."""
    ops = {}
    rx = re.compile(r"^ITRACE ([0-9A-Fa-f]{2})/([0-9A-Fa-f]{4}): ([0-9A-Fa-f]{2}) ")
    with open(path) as f:
        for ln in f:
            m = rx.match(ln)
            if m:
                pc = (int(m.group(1), 16) << 16) | int(m.group(2), 16)
                ops[pc] = int(m.group(3), 16)
    return ops


def load_itrace_pcs(path):
    """Set of full-24-bit PCs in an ITRACE file (kept for callers that only need
    coverage; liveness uses load_itrace_ops -- content, not window)."""
    return set(load_itrace_ops(path).keys())


def overlay_symbolize(segs, addr, live_ops=None):
    """Overlay-aware symbolization of a runtime address.

    Returns dict(addr, covers=[{idx,dest,name,off,live,evidence}], ambiguous, live_idx).
    * covers lists EVERY segment covering addr (both, for an overlay) -- never picks one.
    * live_ops = {pc24: opcode} from the ITRACE. A covering overlay is LIVE when the
      bytes it holds at the executed offsets MATCH the opcodes the ITRACE ran there,
      with no mismatch -- i.e. the code that actually ran IS this overlay's content.
      (Content match, not window containment: overlapping windows cannot disambiguate.)
    * ambiguous is True when >1 segment covers addr AND no single overlay is proven
      live -- the honest "two answers, suspect a liar" state, never silently collapsed.
    """
    covers = []
    for s in segs_covering(segs, addr):
        live = None
        matches = mismatches = 0
        if live_ops:
            lo, hi = s["dest"], s["dest"] + s["length"]
            for pc, op in live_ops.items():
                if lo <= pc < hi and (pc - lo) < len(s["data"]):
                    if s["data"][pc - lo] == op:
                        matches += 1
                    else:
                        mismatches += 1
            live = (matches > 0 and mismatches == 0)
        covers.append(dict(idx=s["idx"], dest=s["dest"], off=addr - s["dest"],
                           name=seg_name(s["dest"], s["idx"]), live=live,
                           evidence=(matches, mismatches)))
    live_idxs = [c["idx"] for c in covers if c["live"]]
    live_idx = live_idxs[0] if len(live_idxs) == 1 else None
    ambiguous = len(covers) > 1 and live_idx is None
    return dict(addr=addr, covers=covers, ambiguous=ambiguous, live_idx=live_idx)


def format_symbol(sym):
    """One-line human rendering of an overlay_symbolize() result."""
    a = sym["addr"]
    if not sym["covers"]:
        return f"${a:06X}: <no segment covers this addr>"
    parts = []
    for c in sym["covers"]:
        tag = ""
        if c["live"] is True:  tag = " [LIVE]"
        elif c["live"] is False: tag = " [dormant]"
        parts.append(f"{c['name']}+${c['off']:X}{tag}")
    lead = "AMBIGUOUS-OVERLAY " if sym["ambiguous"] else ""
    joined = "  ||  ".join(parts)
    if sym["ambiguous"]:
        joined += "   <-- suspect a liar: TWO segments here, none proven live by ITRACE"
    return f"{lead}${a:06X}: {joined}"


def _hexrow(b):
    return b.hex(" ")


def cmd_overlay(argv):
    ap = argparse.ArgumentParser(prog="gdiff overlay",
        description="Overlay-aware OMF symbolizer + dup-dest warning (SIG-031 anti-liar).")
    ap.add_argument("ours", help="GS.OS image (restart-header OMF)")
    ap.add_argument("golden", nargs="?", help="optional golden image for a byte window diff")
    ap.add_argument("--addr", action="append", default=[], metavar="BANK:OFF",
                    help="runtime addr to symbolize/diff, e.g. 01:DB2D (repeatable)")
    ap.add_argument("--win", type=lambda x: int(x, 0), default=16,
                    help="bytes to show for each --addr (default 16)")
    ap.add_argument("--itrace", help="ITRACE file: flag the LIVE overlay at each addr")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    a = ap.parse_args(argv)

    segs = walk_omf(a.ours)
    live_ops = load_itrace_ops(a.itrace) if a.itrace else None
    dups = find_dup_dests(segs)

    if a.json:
        out = dict(version=VERSION, image=a.ours, nsegs=len(segs),
                   dup_dests={f"${k:06X}": v for k, v in dups.items()},
                   addrs=[])
        for spec in a.addr:
            bank, off = spec.split(":")
            addr = (int(bank, 16) << 16) | int(off, 16)
            out["addrs"].append(overlay_symbolize(segs, addr, live_ops))
        print(json.dumps(out, indent=2))
        return 0

    print(f"# gdiff overlay v{VERSION}  image={a.ours}  segs={len(segs)}")
    warn_dups(segs)
    gsegs = walk_omf(a.golden) if a.golden else None
    for spec in a.addr:
        bank, off = spec.split(":")
        addr = (int(bank, 16) << 16) | int(off, 16)
        sym = overlay_symbolize(segs, addr, live_ops)
        print(format_symbol(sym))
        for c in sym["covers"]:
            s = segs[c["idx"]]
            ob = s["data"][c["off"]:c["off"] + a.win]
            print(f"    ours[{c['name']}] {_hexrow(ob)}")
            if gsegs is not None and c["idx"] < len(gsegs):
                gb = gsegs[c["idx"]]["data"][c["off"]:c["off"] + a.win]
                marks = "".join("^^ " if i < len(gb) and ob[i] != gb[i] else "   "
                                for i in range(len(ob)))
                print(f"    gold[{c['name']}] {_hexrow(gb)}")
                print(f"    {'':{len(c['name']) + 10}}{marks}")
    return 0


if __name__ == "__main__":
    sys.exit(cmd_overlay(sys.argv[1:]))
