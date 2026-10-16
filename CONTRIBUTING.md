# Contributing to GSSquared

## This development fork

This is the owner's `a2gspu` fork. Contributions and local changes follow
[AGENTS.md](AGENTS.md), [the handoff](Docs/HANDOFF.md) and the
[shared-foundation contracts](Docs/SHARED_FOUNDATION.md). Keep public interfaces
general, preserve both IIe and IIgs behavior, and run the applicable tests and
full fork gates before closing shared behavioral work. State which claims are
supported by emulator tests and which by physical-hardware evidence.

Completed reviewed phases are synced to the owner's `origin/a2gspu`. The fork
does not push changes or open PRs upstream; its upstream integration ledger is
[UPSTREAM_SYNC.md](Docs/UPSTREAM_SYNC.md). The inherited contribution guidance
below describes upstream's hardware-evidence preference. It does not replace
this fork's gate stack or authorize upstream publication.

## Inherited upstream guidance

We love collaboration and pull requests. If you want to help make Apple II
emulation better, you are welcome here.

## Pull requests

Please open a PR. Bug fixes, features, docs, and "I noticed this while using a
real machine" reports are all appreciated.

### Changes to emulator behavior

GSSquared aims to match real hardware. If a change alters current emulator
behavior (CPU, MMU, video, interrupts, I/O, timing, and so on), we would like
to see it tested against real hardware first — or against a test that has
already been run on real hardware.

A short note of what you ran, on which machine and ROM, and what you observed
is enough. We do not need a lab notebook; we do need to know the change is not
only matching another emulator or a plausible reading of the code.

Documentation, UI, build, and other changes that do not affect guest behavior
do not need hardware tests.
