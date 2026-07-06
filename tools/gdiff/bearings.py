#!/usr/bin/env python3
"""gdiff.bearings -- "bearings split -> suspect a liar" cross-check (trust-hierarchy).

The single most expensive campaign lesson: an instrument that CONFIDENTLY answers a
question can be lying (a dest-keyed dict, a probe_peek $EE, a mis-armed WATCH). The
methodology that caught every one was NOT a better single sensor -- it was noticing
that two INDEPENDENT sensors gave DIFFERENT answers to the SAME fact ("bearings
split") and refusing to trust either until the split was resolved.

This helper mechanizes that. Feed it >=2 readings of ONE fact from named sensors; it
never silently picks one. If they agree it says CONSISTENT; if they split it says
BEARINGS SPLIT -- SUSPECT A LIAR, shows every reading, and (using a default trust
hierarchy -- on-glass capture authoritative, golden-differential high, probe_peek/
floating-bus low) points at the likeliest liar WITHOUT collapsing the disagreement.

Standalone:
    python bearings.py --fact "what loads at 01D000" segdiff=Loader_LC overlay=init3
    python bearings.py --json facts.json
As a lib:
    from bearings import bearings_split
"""
import sys, os, json, argparse

VERSION = "1.0"

# Default trust ranks (higher = more authoritative). Substring match on the sensor
# name, case-insensitive. Mirrors the methodology's trust-hierarchy: glass > golden-
# differential > instruments > known floating-bus/heuristic sensors.
DEFAULT_TRUST = {
    "glass": 100, "capture": 100, "hardware": 100, "silicon": 100, "onglass": 100,
    "golden": 80, "differential": 80, "wdiff": 78, "itrdiff": 78, "segdiff": 78,
    "byteident": 78,
    "itrace": 62, "retguard": 62, "watch": 60, "milestone": 55, "calltrace": 58,
    "overlay": 52, "symbolizer": 50, "orgmap": 50, "sidecar": 50,
    "spike_frame": 22, "videomap": 30, "heuristic": 20,
    "probe_peek": 10, "peek": 10, "floating": 5, "guess": 5,
}


def trust_of(sensor, overrides=None):
    s = sensor.lower()
    if overrides and sensor in overrides:
        return overrides[sensor]
    best = None
    for k, v in DEFAULT_TRUST.items():
        if k in s:
            best = v if best is None else max(best, v)
    return best if best is not None else 40   # unknown sensor = neutral


def bearings_split(fact, readings, trust_overrides=None):
    """readings: list of dict(sensor, value[, trust]). Returns a verdict dict.
    Never collapses a split to a single value."""
    norm = []
    for r in readings:
        t = r.get("trust")
        if t is None:
            t = trust_of(r["sensor"], trust_overrides)
        norm.append(dict(sensor=r["sensor"], value=str(r["value"]), trust=t))
    values = sorted(set(r["value"] for r in norm))
    agree = len(values) <= 1
    verdict = dict(fact=fact, agree=agree, n=len(norm),
                   values=values, readings=norm, suspected_liars=[], trusted=None)
    if not agree:
        # the split is the headline. Point at the likely liar = the dissenter(s) with
        # the LOWEST trust, but keep ALL readings visible.
        min_t = min(r["trust"] for r in norm)
        max_t = max(r["trust"] for r in norm)
        verdict["suspected_liars"] = [r["sensor"] for r in norm if r["trust"] == min_t]
        top = [r for r in norm if r["trust"] == max_t]
        # only name a 'most trusted' if it is a strict, single winner
        if len(top) == 1 and max_t > min_t:
            verdict["trusted"] = top[0]["sensor"]
    return verdict


def format_verdict(v, out=sys.stdout):
    if v["agree"]:
        val = v["values"][0] if v["values"] else "(none)"
        out.write(f"CONSISTENT  '{v['fact']}' = {val}  ({v['n']} sensors agree)\n")
        return
    out.write(f"** BEARINGS SPLIT -- SUSPECT A LIAR **  fact: '{v['fact']}'\n")
    for r in sorted(v["readings"], key=lambda r: -r["trust"]):
        flag = "  <-- suspected liar" if r["sensor"] in v["suspected_liars"] else ""
        star = " *" if r["sensor"] == v["trusted"] else "  "
        out.write(f"  {star} {r['sensor']:<16} = {r['value']:<24} (trust {r['trust']}){flag}\n")
    out.write("   -> do NOT trust either reading until the split is resolved "
              "(re-measure with a third, higher-trust sensor).\n")


def cmd_bearings(argv):
    ap = argparse.ArgumentParser(prog="gdiff bearings",
        description="Cross-check >=2 sensor readings of one fact; loud on a split.")
    ap.add_argument("--fact", help="the fact under test (a short label)")
    ap.add_argument("readings", nargs="*", metavar="sensor=value",
                    help="one or more sensor=value readings")
    ap.add_argument("--json", help="JSON file: {fact, readings:[{sensor,value,trust}]} "
                                    "or a list of such objects")
    a = ap.parse_args(argv)

    facts = []
    if a.json:
        data = json.load(open(a.json))
        facts = data if isinstance(data, list) else [data]
    if a.fact or a.readings:
        rd = []
        for kv in a.readings:
            if "=" not in kv:
                ap.error(f"reading '{kv}' must be sensor=value")
            s, v = kv.split("=", 1)
            rd.append(dict(sensor=s, value=v))
        facts.append(dict(fact=a.fact or "(unnamed)", readings=rd))
    if not facts:
        ap.print_help()
        return 2
    rc = 0
    for fobj in facts:
        v = bearings_split(fobj["fact"], fobj["readings"])
        format_verdict(v)
        if not v["agree"]:
            rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(cmd_bearings(sys.argv[1:]))
