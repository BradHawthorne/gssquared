#pragma once

// Mega II $C044 / $C045 (MMDELTAX / MMDELTAY). AppleTalk LAP uses them as the
// SCC line-activity counters (ATRom lap_includes.aii MouseA / MouseB). A guest
// read returns the count since the previous read and clears it. pulse() is used by the LocalTalk-over-UDP layer (Docs/LToUDP.md) to count
// bit activity; layer 1 leaves the counters at 0 so an unconnected port idles.
void iigs_line_activity_bind(void *mmu);
void iigs_line_activity_unbind(void *mmu);
void iigs_line_activity_pulse(bool port_a);
