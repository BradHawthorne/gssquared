# Queue iteration: file identity and isolated campaign runs — 2026-10-16

The owner requested iteration through all open tasks. This phase closes the
known file-session executable-name bug and isolates scout runs; the remaining
CIDAR/native/tooling slices continue in the roadmap. Separate drift B–E,
engine merge, baseline and protected-consumer decisions remain unchanged.

## File sessions

The pre-fix fixture drives a real renamed executable far enough to receive its
startup CPU acknowledgement, then records the old client falsely calling that
live process gone. The fix consolidates duplicated liveness functions in
`runtime.ps1`: exact executable path and native process-start UTC ticks identify
one process instance. Session receipts keep their v1 schema and add a named
identity model/tick string. Missing fields require a fresh session; there is no
process-name or coarse-time fallback.

Known-fail fixtures alter executable path, one start tick, PID or metadata.
They must refuse without creating any command. Real alias control works on
both IIe and IIgs; restoring the genuine receipt restores control, while a
genuinely exited process is still reported as gone. Live session-directory
reuse refuses without replacing its receipt. The fixture also caught a prior
successful-ack path that inherited `$LASTEXITCODE=2`; it now explicitly exits 0.
Declared gate cleanup uses the same instance check before treating a PID as
owned. Environment rails are restored even when process creation fails.

## Scout

Each invocation reserves a new output directory, then gives each baseline/hop
its own directory and staged image/symbol/milestone bytes. Input hashes and
requested rails, native PINS/GSDIAG, logs, exit/timeout and comparison outcomes
are retained in `a2scout-run-v1`. Existing output refuses; invalid input refuses
before launch. Inherited instrumentation is cleared for the child and restored
for its caller, including explicit provider setup. Read/write experiments use
staged media plus RAM disk/no-writeback pins, preserving the source image.

The isolation fixture checks repeated roots, separate child directories,
preserved source/evidence and caller pins, explicit-provider hygiene and
negative inputs. It uses generated raw media with unreachable synthetic
milestones; this is ROM execution/staging proof, not guest OS acceptance.
A separate invocation used the owner's local GS/OS image with the same checks;
no media is redistributed. A deliberate splice remains a forecast, never a
faithful program/hardware result.

## Source-variant disposition

The active Mockingboard library is `mb2.cpp` + `AY8910-2.hpp`/`W6522.hpp`.
`devices.cpp` still includes `mb.hpp`; `w6522.cpp` is the enabled test target.
`n6522.cpp`/`N6522.hpp` belong to a disabled test target. `mb.cpp`/`AY8910.hpp`
are alternate source; the two suffixed `N6522.hpp-*` copies have no current
compiled include edge. These are classified, not blindly removed. Consolidating
reference-only files still needs an explicit compatibility/retirement boundary;
classification does not imply an unknown external consumer cannot include them.

## Validation

Final: **14/14 gates**, including **14/14 native CTests**, **40 file-identity
checks** across IIe/IIgs, **20 scout checks** and **42 unchanged boot-witness
checks**. Exact identities are recorded in `checks.json`. The current README
was assessed and retains accurate high-level interface/build guidance; detailed
process/scout contracts and handoff are updated here.

The mandated a2engine copy refresh used its existing `sync-a2rail.ps1`, which
backed up replaced files to the recorded temporary backup. All nine shared
files match the fork byte-for-byte; no a2engine commit/push, full product suite
or binary replacement is claimed. Native emulator sources, baseline witnesses
and the protected consumer executable are unchanged.
