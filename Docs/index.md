# Using GSSquared - User Documentation

* [Menus](Menus.md)

* [Selecting a system](Select.md)

* [OSD - On Screen Display](OSD.md)

* [Using a Keyboard](KeyboardShortcuts.md)

* [Using a Mouse](Mouse.md)

* [Displays](Displays.md)

* [Storage & Disks](Storage.md)

* [Host FST](HostFST.md) — share a host folder with GS/OS

* [Joysticks](Joysticks.md)

* [Serial & Parallel Connections](SerialConnections.md)

* [Serial / Modem](Serial_Modem.md)

* [Creating Custom System Configs](ConfigEditor.md)

* [Writing Config Files Manually](ConfigFiles.md)

* [Using the Debugger](UsingTheDebugger.md)

## Cards

Slot cards and how to use them:

* [Uthernet II](Cards_UthernetII.md) — Ethernet / TCP/IP

* [Super Serial Card](Cards_SuperSerial.md) — serial port: modem, file, clipboard, or host serial port

* [Apple Mouse III](Cards_AppleMouse.md) — mouse card for //e

* [Parallel Interface](Cards_Parallel.md) — printer / parallel capture to file or clipboard

* [Video Overlay Card](Cards_VOC.md) — IIgs 640×400 SHR interlace

# Instrumentation & the Agentic Oracle

This fork is not only an emulator. It is the oracle half of a closed loop with
the Rosetta toolchain, and its mandate is that no part of its architecture is a
black box. That contract used to be reachable only by reading the repository —
none of these pages were linked from here or carried in the site navigation, so
a reader who arrived at the documentation saw a stock Apple II manual and no
route to the thing that makes this fork different. They are listed here so that
never silently happens again.

* [Agentic Oracle Contract](AGENTIC_ORACLE.md) — no black boxes; named models

* [Rails — the env instrumentation contract](A2GSPU_RAILS.md)

* [PC Traps](A2GSPU_TRAP.md)


* [Closed-Loop Integration](CLOSED_LOOP_INTEGRATION.md) — gssquared as Rosetta's oracle

* [Shared Foundation](SHARED_FOUNDATION.md) — multi-consumer rules and the do-not-break list

* [Dual-Platform Bar (IIe + IIgs)](DUAL_PLATFORM.md) — IIe-green alone is not done

* [Instrumentation Audit](INSTRUMENTATION_AUDIT.md)

* [Instrumentation Shape](INSTRUMENTATION_SHAPE.md) — the mechanisms and their status

* [Upstream Sync Ledger](UPSTREAM_SYNC.md) — what has been ported from upstream, and what is owed
