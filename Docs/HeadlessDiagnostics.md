# Headless Diagnostics — moved

This page was a second, shorter catalogue of the headless spike's rails, written before
[`A2GSPU_RAILS.md`](A2GSPU_RAILS.md) existed. Two catalogues of one surface drift apart, and
this one had: its `GSDIAG:` taxonomy lacked `HANG`, `STALLED`, `gate=BLESSED`, `warnings=`
and `cycles=`, and its note on `probe_peek` described the probe before it resolved banks $00/$01, without the handler-page hazard its callers must check.

Everything it covered is in the rail catalogue now:

| Topic | Where |
|-------|-------|
| Running a spike (`A2GSPU_SPIKE_FRAMES`, `-n`) | section *Run* of [`A2GSPU_RAILS.md`](A2GSPU_RAILS.md) |
| Tracing and breakpoints (`TBTRACE`, `ERRHOOK`, `BRKDUMP`, `BREAK`, …) | sections *Trace* and *Break* |
| State dumps (`DUMP`, `VIDEOSUM`, `VIDEOMAP`) | section *Observe* |
| Who wrote address X / value V (`WATCH`, `VALTRAP`), a frozen clock (`FAKETIME`) | [Worked examples](A2GSPU_RAILS.md#worked-examples) |
| `A2GSPU_ASSERT`, `A2GSPU_GOLDEN`, the `GSDIAG:` line and exit codes | section *Assert* |
| `probe_peek` and the observer read, for developers | [For developers: the observer read](A2GSPU_RAILS.md#for-developers-the-observer-read) |

This file stays so that existing links land somewhere that says where to go.
