import subprocess, sys, json
def git(*a):
    return subprocess.run(["git", *a], capture_output=True, text=True, encoding="utf-8", errors="replace").stdout
fork_mod = set()
fork_add = set()
for l in open("D:/gsqs/logs/fork_vs_d380.txt", encoding="utf-8"):
    s, p = l.rstrip("\n").split("\t")[0], l.rstrip("\n").split("\t")[-1]
    (fork_mod if s.startswith("M") else fork_add).add(p)
commits = git("rev-list", "--reverse", "--first-parent", "d380e055..upstream/main").split()
allc = git("rev-list", "--reverse", "d380e055..upstream/main").split()
out = []
for c in allc:
    parents = git("rev-list", "--parents", "-n1", c).split()[1:]
    subj = git("log", "-1", "--format=%ad|%s", "--date=short", c).strip()
    files = [f for f in git("diff-tree", "--no-commit-id", "--name-only", "-r", c).split("\n") if f] if len(parents) == 1 else []
    stat = git("show", "--shortstat", "--format=", c).strip() if len(parents) == 1 else "merge"
    overlap = sorted(f for f in files if f in fork_mod)
    out.append({"sha": c[:8], "date": subj.split("|")[0], "subject": subj.split("|", 1)[1], "merge": len(parents) > 1,
                "files": files, "stat": stat, "overlap": overlap})
json.dump(out, open("D:/gsqs/logs/drift.json", "w", encoding="utf-8"), indent=1)
for o in out:
    if o["merge"]:
        print(o["sha"], o["date"], "MERGE", o["subject"][:70]); continue
    print(o["sha"], o["date"], "| files %d | overlap %d %s | %s" % (len(o["files"]), len(o["overlap"]), ",".join(x.split("/")[-1] for x in o["overlap"])[:90], o["subject"][:70]))
