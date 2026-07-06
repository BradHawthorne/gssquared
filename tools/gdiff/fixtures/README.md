# gdiff selftest fixtures

`selftest.py` validates each subcommand against real, reproducible captures.

## Tracked (committed)

- `watch_ours.ndjson`, `watch_pris.ndjson` — WATCH telemetry (our own NDJSON event
  format) from the `$E119A0` SCM notify-queue divergence: 32 vs 64 nodes, the record
  pointer splitting at write-24. Drives the `wdiff` selftest. Small text, no Apple
  binary content.

## Not redistributed (regenerate locally)

- `gsos_gold.omf` — Apple's *shipped* GS/OS 6.0.1 object, from the reference disk.
- `gsos_ours.omf` — our toolchain's build of Apple's GS/OS 6.0.1 source. Our build, but a
  **derived work of Apple's source** — a compilation, not yet independently-authored code.

  Both are Apple-derived GS/OS **binary** object images, so neither is committed to this
  fork — the same posture the top-level `.gitignore` takes for ROM and disk images
  (`*.rom`, `*.po`, `*.dsk`, `*.woz`). They are `.gitignore`d via `*.omf`. `t_overlay_real`
  used them to reproduce the real `$01D000` two-segments-at-one-dest overlay case.

  `t_overlay_real` **skips** cleanly when they are absent. The overlay logic it checks is
  also validated IP-free by `t_overlay_synth`, which builds a minimal synthetic
  restart-header OMF at runtime — so `selftest` stays green on a fresh clone.

  To exercise `t_overlay_real` locally, drop your own build's OMF images in here as
  `gsos_ours.omf` (and `gsos_gold.omf` for the golden side).
