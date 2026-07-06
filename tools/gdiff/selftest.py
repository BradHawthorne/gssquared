#!/usr/bin/env python3
"""gdiff.selftest -- self-validating checks against the campaign's known-truth cases.

Every check reproduces a REAL lying-sensor incident (or the tool logic that catches it)
and asserts the durable, first-class tool gives the right answer. Run any time:

    python gdiff.py selftest            # or: python selftest.py

Fixtures in ./fixtures are the ACTUAL captures from the owned-GS/OS boot (the real
$E119A0 32-vs-64 SCM-queue divergence and the real $01D000 Loader_LC/init3 overlay),
so this is validation against the current boot state, reproducible offline forever.
"""
import sys, os, io, struct

HERE = os.path.dirname(os.path.abspath(__file__))
FX = os.path.join(HERE, "fixtures")
sys.path.insert(0, HERE)

import watchdiff, overlay, watchspec, itracediff, bearings

_results = []
def check(name, cond, detail=""):
    _results.append((name, bool(cond), detail))
    print(f"  [{'PASS' if cond else 'FAIL'}] {name}" + (f" -- {detail}" if detail else ""))
    return bool(cond)


# --------------------------------------------------------------------------
def t_wdiff():
    print("wdiff  ($E119A0 SCM notify-queue: 32 vs 64 nodes; write-24 $E06014 vs $E119D0)")
    ours = watchdiff.load_watch(os.path.join(FX, "watch_ours.ndjson"))
    pris = watchdiff.load_watch(os.path.join(FX, "watch_pris.ndjson"))
    check("wdiff: ours=32 writes", len(ours) == 32, f"got {len(ours)}")
    check("wdiff: pris=64 writes", len(pris) == 64, f"got {len(pris)}")
    buf = io.StringIO()
    first = watchdiff.align_report(ours, pris, field="19A0-19A3", out=buf)
    check("wdiff: first divergence at write-24", first == 24, f"got {first}")
    ov, _, oc = watchdiff._decode_field(ours, first, 0x19A0, 0x19A3)
    pv, _, pc = watchdiff._decode_field(pris, first, 0x19A0, 0x19A3)
    check("wdiff: ours record ptr = $E06014", oc and (ov & 0xFFFFFF) == 0xE06014,
          f"got ${ov & 0xFFFFFF:06X}")
    check("wdiff: pris record ptr = $E119D0", pc and (pv & 0xFFFFFF) == 0xE119D0,
          f"got ${pv & 0xFFFFFF:06X}")
    check("wdiff: decode line present in report",
          "ours links a record at $E06014 vs pristine $E119D0" in buf.getvalue())


# --------------------------------------------------------------------------
def t_overlay_real():
    print("overlay  (the SIG-031 dup-dest liar: two segs both load to $01D000)")
    omf = os.path.join(FX, "gsos_ours.omf")
    if not os.path.exists(omf):
        print("  [SKIP] gsos_ours.omf absent (GS/OS-derived binary, not redistributed;")
        print("         see fixtures/README.md). t_overlay_synth covers the logic IP-free.")
        return
    segs = overlay.walk_omf(omf)
    dups = overlay.find_dup_dests(segs)
    check("overlay: $01D000 dup-dest detected", 0x01D000 in dups,
          f"dups={ {hex(k): v for k, v in dups.items()} }")
    check("overlay: $01D000 <- seg indices [1,12]", dups.get(0x01D000) == [1, 12],
          f"got {dups.get(0x01D000)}")
    buf = io.StringIO()
    fired = overlay.warn_dups(segs, stream=buf)
    check("overlay: dup-dest WARNING fires (loud)",
          fired and "DUP-DEST OVERLAYS" in buf.getvalue())
    # overlay-aware symbolization of the exact SIG-031 address ($01DB2D)
    sym = overlay.overlay_symbolize(segs, 0x01DB2D)
    names = {c["name"].split(" ")[0] for c in sym["covers"]}
    check("overlay: $01DB2D names BOTH overlays (Loader_LC + init3)",
          len(sym["covers"]) == 2 and "Loader_LC" in names and "init3" in names,
          f"covers={[c['name'] for c in sym['covers']]}")
    check("overlay: $01DB2D is AMBIGUOUS without ITRACE (does not silently pick one)",
          sym["ambiguous"] is True)
    # Content-based liveness: overlays SHARE the address window, so only the executed
    # bytes distinguish them. Find an offset where seg1 and seg12 differ, then feed the
    # ITRACE opcode that seg1 (Loader_LC) actually holds there -> flag seg1 LIVE.
    s1, s12 = segs[1], segs[12]
    off = next(o for o in range(min(len(s1["data"]), len(s12["data"])))
               if s1["data"][o] != s12["data"][o])
    addr = 0x01D000 + off
    sym2 = overlay.overlay_symbolize(segs, addr, live_ops={addr: s1["data"][off]})
    check("overlay: ITRACE opcode matching seg1 flags Loader_LC LIVE, resolves ambiguity",
          sym2["live_idx"] == 1 and not sym2["ambiguous"],
          f"addr=${addr:06X} live_idx={sym2['live_idx']} ambiguous={sym2['ambiguous']}")
    sym3 = overlay.overlay_symbolize(segs, addr, live_ops={addr: s12["data"][off]})
    check("overlay: ITRACE opcode matching seg12 flags init3 LIVE instead",
          sym3["live_idx"] == 12 and not sym3["ambiguous"],
          f"live_idx={sym3['live_idx']}")


# --------------------------------------------------------------------------
def _mk_omf(segs):
    """Build a minimal restart-header OMF image from [(dest,bytes)] for a durable,
    IP-free synthetic overlay case."""
    out = bytearray()
    for dest, data in segs:
        hdr = bytearray(0x30)
        struct.pack_into("<H", hdr, 0, len(data))     # length (2 bytes)
        struct.pack_into("<I", hdr, 2, dest)          # dest (4 bytes)
        out += hdr + data
    out += b"\xFF\xFF"                                  # terminator
    return bytes(out)


def t_overlay_synth():
    print("overlay  (synthetic minimal 2-seg-at-one-dest -- durable, IP-free)")
    img = _mk_omf([(0x010000, b"\xAA" * 8), (0x02D000, b"\xBB" * 8), (0x02D000, b"\xCC" * 8)])
    p = os.path.join(FX, "_synth_overlay.omf")
    open(p, "wb").write(img)
    segs = overlay.walk_omf(p)
    dups = overlay.find_dup_dests(segs)
    check("overlay(synth): dup at $02D000 <- [1,2]", dups.get(0x02D000) == [1, 2],
          f"got {dups.get(0x02D000)}")
    sym = overlay.overlay_symbolize(segs, 0x02D004)
    check("overlay(synth): 2 covers, ambiguous w/o ITRACE",
          len(sym["covers"]) == 2 and sym["ambiguous"], "")
    os.remove(p)


# --------------------------------------------------------------------------
def t_watchspec():
    print("watchspec  (the 'WATCH didn't fire' bare-24-bit trap + probe_peek $EE liar)")
    ok, res = watchspec.validate_watch_spec("E119A0")
    bare = any("BARE-24-BIT" in w for r in res for w in r["warnings"])
    check("watchspec: bare 'E119A0' REJECTED as the trap", (not ok) and bare)
    ok2, _ = watchspec.validate_watch_spec("E1:19A0-19A3")
    check("watchspec: 'E1:19A0-19A3' accepted (clean)", ok2)
    ok3, res3 = watchspec.validate_watch_spec("E1:19A3-19A0")
    inv = any("inverted" in w for r in res3 for w in r["warnings"])
    check("watchspec: inverted 'E1:19A3-19A0' warned", (not ok3) and inv)
    check("watchspec: probe_suspect bank $00 $EE = liar", watchspec.probe_suspect(0x00BA00, 0xEE))
    check("watchspec: probe_suspect bank $01 $EE = liar", watchspec.probe_suspect(0x01D000, 0xEE))
    check("watchspec: probe_suspect bank $E1 $EE = NOT a bank0/1 liar",
          not watchspec.probe_suspect(0xE11858, 0xEE))
    check("watchspec: probe_suspect bank $00 non-$EE = fine",
          not watchspec.probe_suspect(0x00BA00, 0x33))


# --------------------------------------------------------------------------
def t_itrdiff():
    print("itrdiff  (DATA vs CONTROL classification + interrupt-skew resync)")
    base = [
        "ITRACE 00/2000: A9 LDA  #$01     A=0001 X=0000 Y=0000 S=01FF D=0000 DBR=00 P=30 e=0",
        "ITRACE 00/2002: AD LDA  $3000    @003000=14 A=0014 X=0000 Y=0000 S=01FF D=0000 DBR=00 P=30 e=0",
        "ITRACE 00/2005: 8D STA  $3002    @003002=00 A=0014 X=0000 Y=0000 S=01FF D=0000 DBR=00 P=30 e=0",
    ]
    # DATA-VAL: same instruction, @003000 reads a different byte
    ours = list(base)
    pris = list(base); pris[1] = pris[1].replace("@003000=14", "@003000=D0")
    tag = itracediff.classify(ours[1], pris[1], nopc=False)
    check("itrdiff: differing @ea value classifies DATA-VAL", tag == "DATA-VAL", f"got {tag}")
    # CONTROL/PC: a different branch target/instruction
    o2 = ["ITRACE 00/2000: 4C JMP  $2100    A=0000 X=0000 Y=0000 S=01FF D=0000 DBR=00 P=30 e=0"]
    p2 = ["ITRACE 00/2000: 4C JMP  $2200    A=0000 X=0000 Y=0000 S=01FF D=0000 DBR=00 P=30 e=0"]
    check("itrdiff: different operand classifies CONTROL/PC",
          itracediff.classify(o2[0], p2[0], nopc=False) == "CONTROL/PC")
    # interrupt-skew: OURS has ONE extra line; resync must NOT cascade
    irq = "ITRACE 00/FFE6: 48 PHA           A=0014 X=0000 Y=0000 S=01FE D=0000 DBR=00 P=30 e=0"
    ours_skew = [base[0], irq, base[1], base[2]]     # extra IRQ line injected after line0
    pris_skew = [base[0], base[1], base[2]]
    buf = io.StringIO()
    n = itracediff.resync_report(ours_skew, pris_skew, max_div=5, run=2, out=buf)
    txt = buf.getvalue()
    check("itrdiff: interrupt-skew resyncs (<=1 divergence, no cascade)", n <= 1, f"divs={n}")
    check("itrdiff: resync names the extra OURS line (interrupt/skew)",
          "extra line(s) on OURS" in txt)


# --------------------------------------------------------------------------
def t_bearings():
    print("bearings  (SIG-031 split: segdiff vs a dest-keyed liar; + probe_peek liar)")
    v = bearings.bearings_split("what loads at $01D000",
        [dict(sensor="segdiff", value="Loader_LC+init3 (two segs)"),
         dict(sensor="dest_dict", value="check_express_seg (one seg)")])
    check("bearings: SIG-031 disagreement is a SPLIT (not silently resolved)", not v["agree"])
    check("bearings: higher-trust segdiff marked trusted", v["trusted"] == "segdiff",
          f"trusted={v['trusted']}")
    vp = bearings.bearings_split("value at $00BA00",
        [dict(sensor="itrace", value="$33"), dict(sensor="probe_peek", value="$EE")])
    check("bearings: probe_peek flagged suspected liar on split",
          "probe_peek" in vp["suspected_liars"])
    va = bearings.bearings_split("scb width",
        [dict(sensor="spike", value="640"), dict(sensor="golden", value="640")])
    check("bearings: agreeing sensors report CONSISTENT", va["agree"])


def cmd_selftest(argv):
    print("=" * 72)
    print("gdiff selftest -- durable validation against the boot campaign's truth cases")
    print("=" * 72)
    for t in (t_wdiff, t_overlay_real, t_overlay_synth, t_watchspec, t_itrdiff, t_bearings):
        try:
            t()
        except Exception as e:
            check(f"{t.__name__} raised", False, repr(e))
        print()
    npass = sum(1 for _, ok, _ in _results if ok)
    ntot = len(_results)
    print("=" * 72)
    print(f"gdiff selftest: {npass}/{ntot} checks passed")
    if npass != ntot:
        print("FAILURES:")
        for name, ok, detail in _results:
            if not ok:
                print(f"  - {name} ({detail})")
    print("=" * 72)
    return 0 if npass == ntot else 1


if __name__ == "__main__":
    sys.exit(cmd_selftest(sys.argv[1:]))
