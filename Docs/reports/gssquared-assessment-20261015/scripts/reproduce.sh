#!/usr/bin/env bash
# Reproduce the gssquared-fork assessment (2026-10-15) WITHOUT touching the fork's tree.
# Everything happens in scratch clones under $SCR (a short path: SDL_ttf's nested checkouts
# exceed MAX_PATH under a deep directory). Git Bash on Windows, MSYS2 UCRT64 + Ninja.
set -euo pipefail
FORK=/d/Projects/gssquared-fork          # read only
SCR=${SCR:-/d/gsqs}
export PATH="/c/msys64/ucrt64/bin:/d/dev/ninja:$PATH"
mkdir -p "$SCR/logs"

# 1. A scratch clone of the fork, plus the untracked inputs the gates need: the vendored SDL
#    trees (submodules, copied without .git), the git-ignored ROM 3/ROM 4 images, the corpus.
rm -rf "$SCR/fork"
git clone -q -c core.longpaths=true "$FORK" "$SCR/fork"
cd "$SCR/fork"
for m in SDL SDL_image SDL_net SDL_ttf libslirp; do
  rm -rf vendored/$m; mkdir -p vendored/$m
  (cd "$FORK/vendored/$m" && tar --exclude=.git -cf - .) | (cd vendored/$m && tar -xf -)
done
for r in apple2gs3 apple2gs4; do mkdir -p assets/roms/$r; cp "$FORK"/assets/roms/$r/*.rom assets/roms/$r/; done
for f in "$FORK"/corpus/*; do [ "$(basename "$f")" = README.md ] || cp "$f" corpus/; done

# 2. Build (Release) and run the fork's own ctest; then, from PowerShell in $SCR/fork:
#    .\tools\a2rail\gates.ps1      (12 gates, ~3.5-5 min)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe \
  -DCMAKE_MAKE_PROGRAM=D:/dev/ninja/ninja.exe > "$SCR/logs/configure.log" 2>&1
cmake --build build --parallel 8 > "$SCR/logs/build.log" 2>&1
(cd build && ctest -j1 > "$SCR/logs/ctest.log" 2>&1) || true

# 3. Upstream drift: fetch upstream into the scratch clone only, map every commit since the
#    fork's last by-hand sync point (d380e055) against the files the fork's layer modifies.
git remote add upstream https://github.com/jawaidbazyar2/gssquared.git
git fetch -q upstream main
git diff --name-status d380e055 HEAD -- . ':!vendored' > "$SCR/logs/fork_vs_d380.txt"
git diff --numstat     d380e055 HEAD -- . ':!vendored' > "$SCR/logs/numstat.txt"
python -I "$(dirname "$0")/layer.py" "$SCR/logs/numstat.txt" "$SCR/logs/fork_vs_d380.txt"
python -I "$(dirname "$0")/drift.py"   # writes D:/gsqs/logs/drift.json (edit paths if $SCR differs)

# 4. Trial cherry-picks, individually, in a second scratch clone (never the fork).
rm -rf "$SCR/trial"; git clone -q "$SCR/fork" "$SCR/trial"; cd "$SCR/trial"
git fetch -q "$SCR/fork" 'refs/remotes/upstream/*:refs/remotes/upstream/*'
for c in 7634b151 ae574e1b b22a2345 86527159 3947c859 0f774f48 599db0ae cc0a9eec 87f56ac9 7eb29076 \
         d5b63752 293e0fc1 c7046029 9dbe1138 4a14e989 58751766 cb8ff63a d9b67a89 49ca2a34; do
  git reset -q --hard HEAD; git clean -qfd
  if git -c user.name=t -c user.email=t@t cherry-pick --no-commit $c >/dev/null 2>&1
  then echo "$c CLEAN"; else echo "$c CONFLICT $(git diff --name-only --diff-filter=U | tr '\n' ' ')"; fi
  git cherry-pick --abort >/dev/null 2>&1 || true
done
git reset -q --hard HEAD; git clean -qfd

# 5. The take-now set in sequence, then build + ctest + gates (copy vendored/ROMs/corpus as in 1).
git checkout -q -b takenow-trial
for c in 7634b151 b22a2345 87f56ac9 cc0a9eec d5b63752 599db0ae 7eb29076 293e0fc1 cb8ff63a; do
  git -c user.name=trial -c user.email=trial@local cherry-pick -x $c
done
