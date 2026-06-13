#pragma once
// ============================================================================
// slot_bus.hpp — faithful Apple IIgs slot-3 bus model (the "virtual slot").
//
// Per Mega-II-side bus cycle, capture the exact signal view a slot-3 peripheral
// card latches (per IIgs HW Ref ch.8 + Tech Note #68): {A0-A15, D0-D7, R/W,
// M2B0, /M2SEL, /DEVSEL, /IOSEL, /IOSTRB, /INH}. Only Mega-II-side (1.024 MHz,
// CYCLE_TYPE_SYNC) cycles are slot-visible — the FPI 2.8 MHz fast cycles never
// reach the slot bus, so they are excluded by FIDELITY, not bandwidth. The slot
// card sees only A0-A15 + M2B0 (NOT A16-A23); $E0-vs-$E1 is reconstructed from
// /M2SEL + M2B0, exactly as TN#68 describes.
//
// This is the stream a snooping bus device receives over USB CDC (the slot stand-in)
// and decodes. Captured here first for VALIDATION: the SHR-write subset of this stream
// must byte-match the bus-trace oracle ($E1:$2000-$9FFF) before any downstream code
// exists. Header-only (C++17 inline vars), generic naming.
// ============================================================================
#include <vector>
#include <cstdint>
#include <cstdio>

// One slot-visible bus cycle, as a slot-3 card's pins latch it.
struct slot_bus_cycle_t {
    uint64_t cycle;   // emulator cycle counter (ordering / pacing)
    uint16_t addr;    // A0-A15 (the slot sees only the low 16 + M2B0)
    uint8_t  data;    // D0-D7
    uint8_t  ctl;     // SLOT_CTL_* asserted-flags (real pins active-low; here 1 = asserted/true)
};

enum {
    SLOT_CTL_READ   = 1u << 0, // R/W: 1 = read cycle, 0 = write cycle
    SLOT_CTL_M2B0   = 1u << 1, // bank LSB (slot pin 35): 1 = $E1/aux, 0 = $E0/main
    SLOT_CTL_M2SEL  = 1u << 2, // /M2SEL asserted: a valid Mega-II cycle (always 1 in this stream)
    SLOT_CTL_DEVSEL = 1u << 3, // /DEVSEL: CPU addressing this slot's $C0n0-$C0nF
    SLOT_CTL_IOSEL  = 1u << 4, // /IOSEL : $Cn00-$CnFF (this slot's I/O ROM)
    SLOT_CTL_IOSTRB = 1u << 5, // /IOSTRB: $C800-$CFFF (expansion ROM strobe)
    SLOT_CTL_INH    = 1u << 6, // /INH asserted (IOLC shadow disabled)
    // bit 7 reserved for a future PHI0-phase / DMA field
};

// The card's slot in a real IIgs (slot 3 on ROM 0/1, where M2B0 is wired — see TN#68 /
// the A2Fusion jumper). Drives the /DEVSEL//IOSEL//IOSTRB decode.
static constexpr int SLOT_BUS_SLOT = 3;

inline bool                          g_slot_bus_enabled = false;
inline std::vector<slot_bus_cycle_t> g_slot_bus;

// Tap: call once per Mega-II-side (slot-visible) bus cycle. The caller decides
// slot-visibility (/M2SEL) and packs ctl; this only records.
inline void slot_bus_note(uint64_t cycle, uint16_t addr, uint8_t data, uint8_t ctl) {
    if (!g_slot_bus_enabled) return;
    g_slot_bus.push_back(slot_bus_cycle_t{cycle, addr, data, ctl});
}

inline void slot_bus_reset() { g_slot_bus.clear(); }

// Dump the stream to a flat file; return an FNV-1a content hash over
// (addr, data, ctl) — the snoop invariant (cycle excluded).
inline uint64_t slot_bus_dump(const char *path, uint64_t *out_count) {
    FILE *f = fopen(path, "wb");
    if (f) {
        if (!g_slot_bus.empty())
            fwrite(g_slot_bus.data(), sizeof(slot_bus_cycle_t), g_slot_bus.size(), f);
        fclose(f);
    }
    uint64_t h = 1469598103934665603ULL;
    for (const slot_bus_cycle_t &c : g_slot_bus) {
        uint8_t b[5] = { (uint8_t)(c.addr & 0xFF), (uint8_t)(c.addr >> 8), c.data, c.ctl, 0 };
        for (uint8_t x : b) h = (h ^ x) * 1099511628211ULL;
    }
    if (out_count) *out_count = (uint64_t)g_slot_bus.size();
    return h;
}
