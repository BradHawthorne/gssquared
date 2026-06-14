#pragma once
// ============================================================================
// mmu_state_trace.hpp — ground-truth IIgs MMU mapping-state stream.
//
// Emits the emulator's INTERNAL soft-switch / mapping state at each CHANGE,
// cycle-aligned with slot_bus_note() (the same NClock cycle), so that an external
// bus-snoop's bus-DERIVED reconstruction of the IIgs memory map can be cross-checked
// against the model that owns the authoritative state. Captures the $C068 STATE
// register, the $C035 shadow register, the $C029 new-video register, the $C036 speed
// register, and the 80STORE/HIRES/TEXT/MIXED flags.
//
// Additive + flag-gated: zero effect when g_mmu_state_trace_enabled is false, so the
// slot-bus / bus-trace streams and their goldens are entirely untouched.
// ============================================================================
#include <cstdint>
#include <vector>
#include <cstdio>

struct MMUStateRecord {
    uint64_t cycle;          // NClock cycle (same oracle slot_bus_note stamps with)
    uint8_t  reg_state;      // $C068 STATEREG (INTCXROM,ROMBANK,LCBNK2,RDROM,RAMWRT,RAMRD,PAGE2,ALTZP)
    uint8_t  reg_shadow;     // $C035 shadow inhibit flags
    uint8_t  reg_new_video;  // $C029 (bank-latch / mono / linear / SHR-enable)
    uint8_t  reg_speed;      // $C036 (speed + shadow-all)
    uint8_t  flags;          // bit0=80STORE bit1=HIRES bit2=TEXT bit3=MIXED
    uint8_t  reg_slot;       // $C02D slot register
    uint8_t  _pad;
};

inline bool                        g_mmu_state_trace_enabled = false;
inline std::vector<MMUStateRecord> g_mmu_state_trace;
inline uint32_t                    g_mmu_state_last = 0xFFFFFFFFu;  // change-gate: packed (state,shadow,newvideo,flags)

inline void mmu_state_trace_reset() {
    g_mmu_state_trace.clear();
    g_mmu_state_last = 0xFFFFFFFFu;
}

// Change-gated push: the per-cycle slot_emit calls (~1.17M on a GS/OS boot) collapse to the
// transition count, recording only the cycles where the mapping state actually moves. The
// gate keys on the four mapping-relevant bytes (state/shadow/new-video/flags); reg_speed and
// reg_slot ride along for reference but do not by themselves trigger a record.
inline void mmu_state_trace_note(uint64_t cycle, uint8_t st, uint8_t sh, uint8_t nv,
                                 uint8_t sp, uint8_t flags, uint8_t slot) {
    if (!g_mmu_state_trace_enabled) return;
    uint32_t key = ((uint32_t)st << 24) | ((uint32_t)sh << 16) | ((uint32_t)nv << 8) | flags;
    if (key == g_mmu_state_last) return;
    g_mmu_state_last = key;
    g_mmu_state_trace.push_back(MMUStateRecord{cycle, st, sh, nv, sp, flags, slot, 0});
}

inline uint64_t mmu_state_trace_dump(const char *path, uint64_t *out_count) {
    FILE *f = fopen(path, "wb");
    if (f) {
        if (!g_mmu_state_trace.empty())
            fwrite(g_mmu_state_trace.data(), sizeof(MMUStateRecord), g_mmu_state_trace.size(), f);
        fclose(f);
    }
    uint64_t h = 1469598103934665603ULL;   // FNV-1a 64 over the changed-state bytes (a determinism check)
    for (auto &r : g_mmu_state_trace) {
        uint8_t b[5] = { r.reg_state, r.reg_shadow, r.reg_new_video, r.reg_speed, r.flags };
        for (int i = 0; i < 5; i++) h = (h ^ b[i]) * 1099511628211ULL;
    }
    if (out_count) *out_count = (uint64_t)g_mmu_state_trace.size();
    return h;
}
