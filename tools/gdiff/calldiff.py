#!/usr/bin/env python3
# calldiff.py -- first-divergence differ for two A2GSPU_CALLSTREAM NDJSON files.
#
#   GSSquared -p 5 -d s7d1=ours.po -n   with A2GSPU_CALLSTREAM=ours.ndjson
#   GSSquared -p 5 -d s7d1=pris.po -n   with A2GSPU_CALLSTREAM=pris.ndjson
#   python calldiff.py ours.ndjson pris.ndjson
#
# Reports the FIRST toolbox/GS-OS call where the (word,kind) sequence diverges, with
# both RAW callers -- the automated "call #260 finder". Immune to symbol aliasing: the
# stream carries only raw hex (word/kind/caller/S/D/DBR), never a (possibly-wrong)
# symbol.  '-r' also flags the first return whose carry/err diverges (same code path,
# different result) -- e.g. a routing carry that flips ours vs pristine.
import sys, json

def load(p):
    calls, rets = [], []
    with open(p) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                o = json.loads(line)
            except Exception:
                continue
            if o.get("ev") == "c":
                calls.append(o)
            elif o.get("ev") == "r":
                rets.append(o)
    return calls, rets

def caddr(o):
    c = o.get("caller", 0)
    return f"{(c >> 16) & 0xFF:02X}/{c & 0xFFFF:04X}"

def fmt(o):
    return (f"word=${o['word']:04X} kind={o['kind']} caller={caddr(o)} "
            f"S=${o.get('s',0):04X} D=${o.get('d',0):04X} DBR=${o.get('dbr',0):02X}")

def main(argv):
    want_ret = "-r" in argv
    args = [a for a in argv[1:] if not a.startswith("-")]
    if len(args) < 2:
        print("usage: calldiff.py [-r] OURS.ndjson PRIS.ndjson")
        return 2
    a_calls, a_rets = load(args[0])
    b_calls, b_rets = load(args[1])
    print(f"ours: {len(a_calls)} calls / {len(a_rets)} rets   "
          f"pris: {len(b_calls)} calls / {len(b_rets)} rets")

    n = min(len(a_calls), len(b_calls))
    div = -1
    for i in range(n):
        if a_calls[i]["word"] != b_calls[i]["word"] or a_calls[i]["kind"] != b_calls[i]["kind"]:
            div = i
            break
    if div < 0:
        print(f"CALL SEQUENCE IDENTICAL for the first {n} calls "
              f"(ours={len(a_calls)} pris={len(b_calls)})")
    else:
        print(f"\nFIRST CALL DIVERGENCE at call #{div}:")
        for j in range(max(0, div - 4), min(n, div + 6)):
            mk = "  <== DIVERGE" if j == div else ""
            print(f"  #{j:<5} OURS {fmt(a_calls[j])}")
            print(f"  #{j:<5} PRIS {fmt(b_calls[j])}{mk}")

    if want_ret:
        m = min(len(a_rets), len(b_rets))
        rdiv = -1
        for i in range(m):
            if (a_rets[i]["word"] != b_rets[i]["word"]
                    or a_rets[i]["carry"] != b_rets[i]["carry"]
                    or a_rets[i]["err"] != b_rets[i]["err"]):
                rdiv = i
                break
        if rdiv < 0:
            print(f"\nRETURN OUTCOMES IDENTICAL for the first {m} returns")
        else:
            print(f"\nFIRST RETURN DIVERGENCE at return #{rdiv}:")
            for j in range(max(0, rdiv - 2), min(m, rdiv + 3)):
                r = a_rets[j]; s = b_rets[j]
                mk = "  <== DIVERGE" if j == rdiv else ""
                print(f"  #{j:<5} OURS word=${r['word']:04X} carry={r['carry']} err=${r['err']:04X}")
                print(f"  #{j:<5} PRIS word=${s['word']:04X} carry={s['carry']} err=${s['err']:04X}{mk}")
    return 0

if __name__ == "__main__":
    sys.exit(main(sys.argv))
