"""Validate a2profile-v1 and roll PCs into explicit function ranges.

Usage: python -I tools/a2rail/profile.py counts.json --symbols functions.json
Symbols: {"schema":"a2symbols-v1","functions":[{"name":"loop","start":4096,"end":4102}]}
Ranges are half-open, absolute 24-bit addresses. A2MAP1 supplies binary segment
identity, not function names; --map checks its identity and mapped PC ranges.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path


def integer(value, limit, label):
    if type(value) is not int or not 0 <= value <= limit:
        raise ValueError(f"invalid {label}: {value!r}")
    return value


def read_profile(value):
    if not isinstance(value, dict) or value.get("schema") != "a2profile-v1" or value.get("model") != "instruction-cycles":
        raise ValueError("unrecognised profile schema/model")
    lo = integer(value["lo"], 0xFFFFFF, "lo")
    hi = integer(value["hi"], 0xFFFFFF, "hi")
    if hi < lo or not isinstance(value.get("pcs"), list):
        raise ValueError("invalid profile range/PC list")
    integer(value["generation"], 2**64-1, "generation")
    if not isinstance(value.get("map_hash"), str) or not re.fullmatch(r"[0-9A-F]{16}", value["map_hash"]):
        raise ValueError("invalid binary-plan map hash")
    seen = set()
    for row in value["pcs"]:
        pc = integer(row["pc"], 0xFFFFFF, "PC")
        count = integer(row["instructions"], 2**64-1, "instructions")
        integer(row["cycles"], 2**64-1, "cycles")
        if not lo <= pc <= hi or pc in seen or not count:
            raise ValueError("out-of-range, duplicate, or zero-count PC")
        seen.add(pc)
    if (integer(value["total_instructions"], 2**64-1, "instruction total") != sum(r["instructions"] for r in value["pcs"])
            or integer(value["total_cycles"], 2**64-1, "cycle total") != sum(r["cycles"] for r in value["pcs"])):
        raise ValueError("profile totals do not match PC rows")
    return value


def read_map(path):
    data = Path(path).read_bytes()
    if len(data) < 56 or data[:8] != b"A2MAP1\0\0":
        raise ValueError("invalid A2MAP1 header")
    count, form = struct.unpack_from("<II", data, 8)
    if not 1 <= count <= 65536 or form not in (1, 2):
        raise ValueError("invalid A2MAP1 count/form")
    pos, ranges, indices = 56, [], set()
    for _ in range(count):
        if pos+12 > len(data):
            raise ValueError("truncated A2MAP1 segment")
        index, base, size = struct.unpack_from("<III", data, pos)
        pos += 12
        if (not size or base+size > 0x1000000 or pos+size > len(data) or index in indices
                or (form == 1 and (count != 1 or index != 0)) or (form == 2 and not index)):
            raise ValueError("invalid A2MAP1 segment")
        indices.add(index)
        ranges.append((base, base+size))
        pos += size
    ranges.sort()
    if pos != len(data) or any(a[1] > b[0] for a, b in zip(ranges, ranges[1:])):
        raise ValueError("trailing/overlapping A2MAP1 data")
    hash_value = 1469598103934665603
    for byte in data:
        hash_value = ((hash_value ^ byte) * 1099511628211) & (2**64-1)
    return f"{hash_value:016X}", ranges


def rollup(profile, symbols=None, map_path=None):
    read_profile(profile)
    if map_path:
        identity, segments = read_map(map_path)
        if profile.get("map_hash") != identity:
            raise ValueError("stale A2MAP1: profile map hash differs")
        if any(not any(lo <= row["pc"] < hi for lo, hi in segments) for row in profile["pcs"]):
            raise ValueError("profile PC outside A2MAP1 segments")
    functions = []
    if symbols is not None:
        if not isinstance(symbols, dict) or symbols.get("schema") != "a2symbols-v1":
            raise ValueError("unrecognised symbol schema")
        if profile.get("map_hash") and profile["map_hash"] != "0000000000000000":
            if symbols.get("map_hash") != profile["map_hash"]:
                raise ValueError("stale symbols: map_hash must match mapped profile")
        for fn in symbols["functions"]:
            start = integer(fn["start"], 0xFFFFFF, "function start")
            end = integer(fn["end"], 0x1000000, "function end")
            if start >= end or not isinstance(fn["name"], str) or not fn["name"]:
                raise ValueError("invalid function range/name")
            functions.append((start, end, fn["name"]))
        functions.sort()
        if any(a[1] > b[0] for a, b in zip(functions, functions[1:])):
            raise ValueError("overlapping function ranges")
    totals = {}
    for row in profile["pcs"]:
        name = next((name for lo, hi, name in functions if lo <= row["pc"] < hi), "<unassigned>")
        item = totals.setdefault(name, {"name": name, "instructions": 0, "cycles": 0})
        item["instructions"] += row["instructions"]
        item["cycles"] += row["cycles"]
    return {"schema": "a2profile-summary-v1", "model": "instruction-cycles",
            "instructions": sum(r["instructions"] for r in profile["pcs"]),
            "cycles": sum(r["cycles"] for r in profile["pcs"]),
            "functions": sorted(totals.values(), key=lambda r: (-r["cycles"], r["name"]))}


def selftest():
    p = {"schema": "a2profile-v1", "model": "instruction-cycles", "lo": 4096, "hi": 4111,
         "total_instructions": 4, "total_cycles": 8,
         "generation": 1, "map_hash": "0000000000000000",
         "pcs": [{"pc": 4096, "instructions": 3, "cycles": 6}, {"pc": 4100, "instructions": 1, "cycles": 2}]}
    symbols = {"schema": "a2symbols-v1", "functions": [{"name": "loop", "start": 4096, "end": 4100}]}
    result = rollup(p, symbols)
    assert result["instructions"] == 4 and result["cycles"] == 8
    assert result["functions"][0] == {"name": "loop", "instructions": 3, "cycles": 6}
    for bad in [dict(p, pcs=p["pcs"]*2), dict(p, schema="wrong"),
                dict(p, pcs=[dict(p["pcs"][0], instructions=-1)]),
                dict(p, pcs=[dict(p["pcs"][0], pc=0x1000000)]),
                dict(p, pcs=p["pcs"][:-1]),
                dict(p, map_hash="1234567890ABCDEF")]:
        try:
            rollup(bad, symbols)
        except ValueError:
            pass
        else:
            raise AssertionError("planted malformed/stale profile accepted")
    print("profile selftest: PASS (roll-up, unassigned PCs, malformed counts, duplicates, stale symbols)")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", nargs="?")
    parser.add_argument("--symbols")
    parser.add_argument("--map")
    parser.add_argument("--expect-instructions", type=int)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    if args.selftest:
        selftest()
        return 0
    if not args.profile:
        parser.error("profile file required")
    try:
        result = rollup(json.loads(Path(args.profile).read_text()),
                        json.loads(Path(args.symbols).read_text()) if args.symbols else None, args.map)
        if args.expect_instructions is not None and result["instructions"] != args.expect_instructions:
            raise ValueError(f"instruction count {result['instructions']} != expected {args.expect_instructions}")
        print(json.dumps(result, indent=2))
        return 0
    except (ValueError, KeyError, TypeError, OSError, struct.error) as exc:
        print(f"profile: FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
