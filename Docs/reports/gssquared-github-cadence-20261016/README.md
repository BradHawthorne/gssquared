# GitHub phase-completion cadence (2026-10-16)

Type: decision/evidence record for `Docs/HANDOFF.md` section 7. The handoff owns
publication policy; `Docs/AGENTIC_ROADMAP.md` owns catch-up task status. Retain
this record as the evidence for the policy change, not a duplicate live queue.

The owner requests bringing GitHub current at a good task-phase completion
point and making that a regular habit. This supersedes the former blanket
no-push instruction for ordinary completed, reviewed, validated work sent by
fast-forward to the existing `origin/a2gspu`. It does not authorize upstream
pushes, remote changes or published-history rewrites. Other repositories retain
their own publication rules.

## Observed starting point

Verified through the existing Git remote, without changing the working branch,
remote configuration or upstream integration:

- `origin`: `https://github.com/BradHawthorne/gssquared.git`.
- `git fetch origin` succeeded on 2026-10-16.
- `git ls-remote --symref origin HEAD refs/heads/a2gspu refs/heads/main`
  identifies `a2gspu` as the default branch; no `main` ref was returned.
- Remote `a2gspu`: `ed2c00fca3f951788d6ed6cc2a81595247f0ed88`, last commit
  2026-09-16 10:37:28 -05:00, "upstream ledger: gs2.cpp's one-time arm gained two rails".
- Local `a2gspu`: `834028cd5ab3d553a9398e0ca88f5ec72de63b68`.
- `git rev-list --left-right --count origin/a2gspu...HEAD`: **0 behind,
  68 ahead**. Modified tracked files: **90**; untracked files: **189**, before
  this policy change. The new report itself changes the later inventory.
- Recent interface/context and CIDAR design work is still uncommitted.
  Existing green working-tree evidence is useful but does not establish that
  a selected tracked publication candidate contains every dependency.

## Planned first checkpoint

The next closeout task, before another major feature phase, is to publish the
completed agent-interface/context/design work and the reviewed outgoing
history. Review all outgoing commits and selected source/client/test/docs/report
files, preserve unrelated work and exclude local-only artifacts according to
existing public-repository rules. Form coherent commits without manufacturing
broken intermediate feature boundaries. Verify the actual selected tracked
candidate, then perform a normal explicit push to `origin/a2gspu` and confirm
the remote SHA independently.

Later CIDAR ingestion/promotion, replay, drift/model work and the renamed-process
harness fix can remain named pending tasks. Publishing a completed checkpoint
does not require completing the entire roadmap. The standing procedure is in
`Docs/HANDOFF.md` section 7, including exact-candidate checks, explicit push
destination and post-push verification. No scheduler or publisher was installed;
the cadence is part of agent task-phase closeout.

## Scope and verification

This turn plans and records the cadence. No commits, pushes, merges, remote
configuration changes or history rewrites were performed. Documentation-only checks are
recorded alongside this report; no new emulator behavior or fresh runtime
validation is claimed. The preceding CIDAR design checkpoint retains the latest
full 14-gate/native-test evidence and unchanged baseline witnesses.

Verification: the live remote identity and 0/68 divergence are retained in
`remote.log`; `railparity.log` passes with all 161 rails and 74 verbs accounted
for. Selected changed-file `git diff --check` passes (`diff-check.log`). The
cadence and pending publication state are consistent across `AGENTS.md`,
the handoff, roadmap and reports index. These checks validate this policy-only
change; they do not perform the pending publication-candidate review.
