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
#include "../../house_fnv.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

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
    SLOT_CTL_INH    = 1u << 6, // /INH asserted (IOLC shadow disabled) -- format 1 only
    // Format 2 only: the machine was running fast ($C036 bit 7) when this slow cycle ran, so
    // it was an isolated, synchronised Mega II cycle (about 1.5 us on average) rather than
    // one of a run at 1.024 MHz (980 ns) [TN68 Table 1]. Format 1 leaves bit 7 clear.
    SLOT_CTL_FAST   = 1u << 7,
};

// Slot 3 on ROM 0/1 — the IIgs slot wired for the Mega-II video bus (M2B0, per TN#68),
// used for the slot-visible bus tap. Drives the /DEVSEL//IOSEL//IOSTRB decode.
static constexpr int SLOT_BUS_SLOT = 3;

inline bool                          g_slot_bus_enabled = false;

// FORK: STREAM FORMAT 2 (opt-in: A2GSPU_SLOT_BUS_FORMAT=2). Format 1 is this stream as
// it has always been, byte for byte, because card firmware already decodes it; format 2
// is the one the fidelity study's slot-bus section (Docs/fidelity/slot_bus.md 2.3)
// describes, and changes only when the owner moves the default:
//   - /DEVSEL and /IOSEL decode for the card's actual slot (g_slot_bus_slot), not slot 3;
//   - $C0xx is slot-visible only in banks $00/$01 with IOLC shadowing on, or $E0/$E1 --
//     format 1 emitted $C0xx in ANY bank, fast-RAM banks included;
//   - $C100-$C7FF is emitted when that page belongs to a card (INTCXROM off and the
//     $C02D bit, or SLOTC3ROM for $C3), with /IOSEL; $C800-$CFFF when a card owns the
//     expansion ROM, with /IOSTRB;
//   - M2B0 is the CPU's bank LSB for a shadowed write, and high for every $E1 access
//     (TN68), not the aux-resolved bank;
//   - $E0 writes and $E0/$E1 RAM reads are emitted (a card sees them with /M2SEL);
//   - bit 6 (/INH) is never set: /INH is driven BY a card, and IOLC state is not it;
//   - fast cycles, which assert no /M2SEL, are not emitted: $C035-$C037, reads of
//     $C02D and $C068, reads of $C071-$C07F;
//   - bit 7 (SLOT_CTL_FAST) says the machine was in fast mode, so the cycle was an
//     isolated synchronised one;
//   - frames carry the magic "~BS2" instead of "~BUS".
inline int  g_slot_bus_format = 0;   // 0 = not probed yet
inline int  g_slot_bus_slot   = SLOT_BUS_SLOT;
inline int slot_bus_format() {
    if (g_slot_bus_format == 0) {
        const char *e = getenv("A2GSPU_SLOT_BUS_FORMAT");
        g_slot_bus_format = (e && e[0] == '2' && e[1] == 0) ? 2 : 1;
        if (e && *e && g_slot_bus_format != 2 && strcmp(e, "1") != 0) {
            fprintf(stderr, "A2GSPU SLOT BUS: FATAL A2GSPU_SLOT_BUS_FORMAT='%s' -- want 1 or 2\n", e);
            exit(2);
        }
    }
    return g_slot_bus_format;
}
// The frame magic for the stream's format: "~BUS" or "~BS2".
inline const uint8_t *slot_bus_magic() {
    static const uint8_t v1[4] = { 0x7E, 0x42, 0x55, 0x53 };
    static const uint8_t v2[4] = { 0x7E, 0x42, 0x53, 0x32 };
    return slot_bus_format() == 2 ? v2 : v1;
}
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
    uint64_t h = HOUSE_FNV_BASIS;
    for (const slot_bus_cycle_t &c : g_slot_bus) {
        uint8_t b[5] = { (uint8_t)(c.addr & 0xFF), (uint8_t)(c.addr >> 8), c.data, c.ctl, 0 };
        for (uint8_t x : b) h = (h ^ x) * HOUSE_FNV_PRIME;
    }
    if (out_count) *out_count = (uint64_t)g_slot_bus.size();
    return h;
}
