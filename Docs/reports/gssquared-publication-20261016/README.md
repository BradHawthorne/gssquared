# Integrated agent-interface/context publication (2026-10-16)

Type: evidence record for the phase-completion procedure in `Docs/HANDOFF.md`
section 7. Live closeout status belongs in `Docs/AGENTIC_ROADMAP.md`.

The owner directed execution of the next task-queue item: catch up the existing
GitHub fork before another major feature phase. The authorized destination is
`origin/a2gspu`; publication is a normal fast-forward, with no upstream push,
remote change, branch merge or history rewrite.

## Selected checkpoint and review

The local base is `834028cd5ab3d553a9398e0ca88f5ec72de63b68`. Origin was fetched
and still reports `ed2c00fca3f951788d6ed6cc2a81595247f0ed88`: 68 commits ahead,
zero behind before this checkpoint. The outgoing history consists of the
previously recorded upstream ports, fidelity/model fixes, instrumentation and
documentation; the separate engine-model branch remains unmerged.

The initial inventory selects 284 modified/new files (4,833,551 bytes) containing
the completed SCC/WDM/counting, snapshot diff/revision witness tools, shared
socket/MCP controls, structured observations, artifact handling, machine context,
experiment companion, regression clients and documentation. They are one
integrated checkpoint because native handlers, discovery, clients and gates
depend on one another. The earlier individual reports preserve their original
validation boundaries; this commit does not invent intermediate passing states.

Review covers the outgoing commit inventory and selected files, with focused
inspection of CPU/MMU/time/observation hooks, agent framing and parameter checks,
discovery/build registration and regression wiring against the documented
contracts. Existing tests and feature reports are supporting evidence, not a
claim of exhaustive semantic review of every inherited upstream line.

A high-confidence credential-pattern scan covered 960 outgoing Git blobs
(22,537,988 bytes) and every selected file, with no matches. This heuristic is a
review aid, not a complete secret/content audit. No newly selected files are
binary, ROM/disk/snapshot inputs, compiled objects or executables. Vendored
working trees and the protected consumer binary are not selected. Upstream ROM
assets already introduced by the reviewed upstream-port history retain their
upstream lineage; for example `assets/roms/apple2gs_rom3/main.rom` has the same
blob in this tree and cached upstream main. Local ignored ROM 3/4 test pins and
the corpus remain local prerequisites, never new publication artifacts.

Source/document whitespace checks pass. Raw report `.log` evidence retains
the original table padding/trailing whitespace; it is excluded from that
formatting check rather than rewriting captured evidence and its identities.
No functional test or baseline is weakened by this formatting exception.

The archive/CIDAR and full-machine replay goals remain open. Drift B-E, the
engine merge, baseline re-blessing and protected consumer replacement retain
their separate owner decisions. Rosetta's local-only changes belong to its own
repository and are not staged or pushed here.

## Validation and publication

**Published source checkpoint:** `bd276467a2cd1f989dae9bc3c062441af9fad172`,
tree `a8106f71cbd32d72146801f18393f09a76e502fe`. The explicit selection committed
287 source/client/test/document/evidence files. A stale zero-byte index lock
from the preceding day was removed only after confirming no Git writer was
active. The original working tree was clean after the commit.

Fresh validation used `D:/gsqs/pub-20261016/src` exported from that commit and
`D:/gsqs/pub-20261016/build`, configured for Release/UCRT64 GCC/Ninja. No untracked
implementation files were borrowed. Root dependency source came from the exact
gitlink objects; SDL_ttf's four nested revisions were fetched from their declared
public URLs into scratch because local nested metadata was unavailable. Original
vendored source/configuration remained untouched. Dependency tracked content
was checked after validation, with Windows file modes ignored and the recorded
macOS-framework symlinks omitted. This is Windows validation, not a macOS build.
Eight ignored local ROM/corpus inputs were explicitly copied and digest-checked.

| Check | Result | Evidence |
| --- | --- | --- |
| Fresh configure and build | PASS; 88 warning lines retained | `configure.log`, `build.log` |
| Native CTest | 14/14 PASS | `gates.log`, dualsmoke |
| Exported Python package | 21/21 PASS | `python-tests.log` |
| Full fork gates | 14/14 PASS, no skips | `gates.log`, `gates.ndjson` |
| Pinned stock/armed boots | 42 checks over four boots PASS; baseline hashes/cycles unchanged | `gates.log`, unarmedhash |
| Affected Rosetta integration | CTRL session, external CPU differential and coverage mapping: 3/3 PASS | `rosetta-integration.log`, `rosetta-coverage.log` |
| CPU classification | 103 vectors: 100 identical; three known COP/BRK stop-policy differences; zero cycle-only differences | `rosetta-compare.txt` |
| Candidate, dependencies, local pins and protected consumer identity | PASS | `validation.json`, `final-identity-check.log` |

Executable SHA256:
`9b6a866d851855d4f6fa49d47468a13ca295058ad218c47841c6fccefbfd7342`.
Protected consumer SHA256 remains
`af7e0bbd72bee417ffddb5e86a00597df280fba904e8b2ed1ff9ad0738e2956c`.
No new full Rosetta run, manual stock-windowed check, physical-IIgs measurement
or replay completeness claim is made. Rosetta's pre-existing local modifications
remain in that repository.

The final outgoing scan covered 1,242 blobs / 27,405,363 bytes through the
candidate, with no high-confidence credential-pattern matches. The MSYS2 Git
push failed noninteractively for missing credentials (`push-candidate.log`).
The installed Windows Git's existing `manager` helper then completed the normal
fast-forward `ed2c00fc..bd276467` (`push-candidate-windows.log`). No credentials
or remote configuration were changed. An independent `git ls-remote` confirmed
the exact candidate SHA on `refs/heads/a2gspu` (`remote-candidate.log`).

This documentation-only closeout receipt follows the same ordinary commit/push
cadence. It retains the published candidate's runtime evidence; implementation
files remain unchanged from that validated commit. The final branch SHA is
verified again after syncing this receipt and reported at task closeout.
