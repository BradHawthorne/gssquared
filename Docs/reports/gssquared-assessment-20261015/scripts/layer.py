import sys
rows = [l.rstrip("\n").split("\t") for l in open(sys.argv[1], encoding="utf-8") if l.strip()]
st = {}
for l in open(sys.argv[2], encoding="utf-8"):
    parts = l.rstrip("\n").split("\t")
    st[parts[-1]] = parts[0]
mods = [(int(a) if a != "-" else 0, int(d) if d != "-" else 0, p) for a, d, p in rows if st.get(p, "M").startswith("M")]
adds = [(int(a) if a != "-" else 0, p) for a, d, p in rows if st.get(p) == "A"]
print("added (fork-owned) files:", len(adds), "lines", sum(a for a, p in adds))
print("modified upstream files:", len(mods), "+", sum(x[0] for x in mods), "-", sum(x[1] for x in mods))
src = [x for x in mods if x[2].startswith("src/")]
print("  src/:", len(src), "+", sum(x[0] for x in src), "-", sum(x[1] for x in src))
print("  thin (<=20 lines changed):", sum(1 for x in src if x[0] + x[1] <= 20), " medium (21-150):", sum(1 for x in src if 20 < x[0] + x[1] <= 150), " heavy (>150):", sum(1 for x in src if x[0] + x[1] > 150))
print("  heaviest edits to upstream src files:")
for a, d, p in sorted(src, key=lambda x: -(x[0] + x[1]))[:20]:
    print("   +%d -%d %s" % (a, d, p))
