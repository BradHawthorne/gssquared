#!/usr/bin/env python3
"""gdiff -- the golden-differential RUNTIME toolkit (first-class, from the boot campaign).

ONE entry point for the emulator-trace differential tools that cracked most of the
owned-GS/OS boot walls. Promotes the scratchpad one-offs (reconstructed almost every
/debug cycle) to a durable, documented, self-validating module.

Subcommands:
  wdiff       positional align of two A2GSPU_WATCH NDJSON streams ($E1 queue), with an
              automatic little-endian record-pointer decode at the divergence.
  itrdiff     execution-trace resync diff of two A2GSPU_ITRACE streams (shift-tolerant,
              interrupt-skew resync, @ea=val DATA/CONTROL classification).
  overlay     overlay-aware OMF symbolizer + dup-destination OVERLAY WARNING -- the
              SIG-031 anti-liar (index-keyed, never dest-keyed; flags the LIVE overlay
              by ITRACE).
  watchspec   validate an A2GSPU_WATCH spec (bank:lo-hi; reject the bare-24-bit trap) or
              lint a WATCH NDJSON for probe_peek/$EE floating-bus liars.
  bearings    cross-check >=2 sensor readings of one fact; loud "suspect a liar" on a split.
  selftest    run the built-in validations against the campaign's known cases.

Run-until harness (G3) is the sibling PowerShell primitive `run_until.ps1`.

Usage:   python gdiff.py <subcommand> [args...]
         python gdiff.py <subcommand> --help
See README.md.  Note: use `python` on this box (python3 is a Store alias).
"""
import sys, os

VERSION = "1.0"
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    if not argv or argv[0] in ("-h", "--help", "help"):
        print(__doc__)
        return 0
    if argv[0] in ("-V", "--version", "version"):
        print(f"gdiff {VERSION}")
        return 0
    sub, rest = argv[0], argv[1:]
    if sub == "wdiff":
        from watchdiff import cmd_wdiff; return cmd_wdiff(rest)
    if sub == "itrdiff":
        from itracediff import cmd_itrdiff; return cmd_itrdiff(rest)
    if sub == "overlay":
        from overlay import cmd_overlay; return cmd_overlay(rest)
    if sub == "watchspec":
        from watchspec import cmd_watchspec; return cmd_watchspec(rest)
    if sub == "bearings":
        from bearings import cmd_bearings; return cmd_bearings(rest)
    if sub == "selftest":
        from selftest import cmd_selftest; return cmd_selftest(rest)
    print(f"gdiff: unknown subcommand '{sub}'. Try: wdiff itrdiff overlay watchspec "
          f"bearings selftest (or --help).", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
