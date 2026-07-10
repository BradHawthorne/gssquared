#pragma once
// ============================================================================
// obs_signal.hpp — the Observatory spine: one signal registry, one timeline,
// many sinks.
//
// Generalizes the three ad-hoc cycle-stamped traces already proven in this tree
//   - bus_trace.hpp        (EVENT, per-write : SHR bus transactions)
//   - slot_bus.hpp         (EVENT, per-cycle : slot-visible bus cycles)
//   - mmu_state_trace.hpp  (LEVEL, on-change : the MMU mapping-state deltas)
// into ONE addressable model: every observable value in the machine has a stable
// packed sigid + a text path, is emitted onto a single ordered timeline keyed by
// the emulator's master cycle counter (NClock::get_cycles), and is dumped/hashed
// through the same house-FNV path so the existing determinism goldens are a
// projection of this stream rather than a separate mechanism.
//
// DESIGN INVARIANTS (why this is safe to land additively):
//   * Header-only (C++17 inline variables) — zero build-graph surface, like the
//     three traces it subsumes.
//   * Default-OFF (g_obs_enabled=false) — every emit early-returns, so an unarmed
//     build is byte-identical to before and the goldens are untouched.
//   * The EVENT record is a strict SUPERSET of BusTraceRecord / slot_bus_cycle_t /
//     MMUStateRecord: for the SHR bus subset (addr, val, rw, provenance) it hashes
//     the identical tuple bus_trace_dump() hashes, so a bus projection of this
//     stream reproduces the E1 golden by construction.
//   * Generic naming only — this file models a real Apple IIgs; no board- or
//     project-specific terms (it is a public surface).
//
// This is the "no black boxes" substrate: total observability is the precondition
// for safely re-architecting any backend against the machine's true floor/ceiling.
// ============================================================================
#include <cstdint>
#include <vector>
#include <cstdio>
#include <cstring>
#include "house_fnv.hpp"

// ----------------------------------------------------------------------------
// 1. sigid packing — (subsystem:8 | signal:12 | index:12), collision-free by
//    construction. The subsystem byte doubles as the per-subsystem arm bit index.
// ----------------------------------------------------------------------------
static constexpr uint32_t OBS_SIG_BITS = 12;
static constexpr uint32_t OBS_IDX_BITS = 12;
static constexpr uint32_t OBS_IDX_MASK = (1u << OBS_IDX_BITS) - 1;          // 0x0FFF
static constexpr uint32_t OBS_SIG_MASK = (1u << OBS_SIG_BITS) - 1;          // 0x0FFF

inline constexpr uint32_t obs_sigid(uint8_t subsys, uint32_t signal, uint32_t index = 0) {
    return ((uint32_t)subsys << (OBS_SIG_BITS + OBS_IDX_BITS))
         | ((signal & OBS_SIG_MASK) << OBS_IDX_BITS)
         | (index & OBS_IDX_MASK);
}
inline constexpr uint8_t  obs_sigid_subsys(uint32_t sigid) { return (uint8_t)(sigid >> (OBS_SIG_BITS + OBS_IDX_BITS)); }
inline constexpr uint32_t obs_sigid_signal(uint32_t sigid) { return (sigid >> OBS_IDX_BITS) & OBS_SIG_MASK; }
inline constexpr uint32_t obs_sigid_index (uint32_t sigid) { return sigid & OBS_IDX_MASK; }

// Subsystem ids (== the arm-mask bit index). One byte, so 256 subsystems, but a
// 32-bit arm mask covers the first 32 — the ones that exist. Extend as needed.
enum obs_subsys : uint8_t {
    OBS_SUB_CPU = 0, OBS_SUB_CLOCK, OBS_SUB_MMU, OBS_SUB_BUS, OBS_SUB_VGC,
    OBS_SUB_DOC,     OBS_SUB_ADB,   OBS_SUB_IWM, OBS_SUB_SCC, OBS_SUB_IRQ,
    OBS_SUB_MEM,     OBS_SUB_TOOL,  OBS_SUB_SLOT,
    OBS_SUB__COUNT
};

// ----------------------------------------------------------------------------
// 2. value type / kind / flags — the descriptor vocabulary from the plan.
// ----------------------------------------------------------------------------
enum obs_type : uint8_t {
    OBS_T_U8 = 0, OBS_T_U16, OBS_T_U32, OBS_T_U64,
    OBS_T_I8,     OBS_T_I16, OBS_T_I32, OBS_T_I64,
    OBS_T_BOOL,   OBS_T_BITFIELD, OBS_T_ENUM, OBS_T_RGB12, OBS_T_BYTES
};

// KIND taxonomy — unifies every current + dark instrument (plan §2.3).
enum obs_kind : uint8_t {
    OBS_K_BUS_TXN = 0,   // bus_trace: a bus transaction
    OBS_K_REG_WRITE,     // WATCH: a soft-switch / device register write
    OBS_K_REG_READ,      // WATCH: a register read
    OBS_K_CYCLE_COST,    // the reclaimed NClock per-cycle 14M timing cost
    OBS_K_IRQ_EDGE,      // InterruptController: an IRQ assert/clear edge
    OBS_K_STATE_DELTA,   // mmu_state_trace: a mapping-state transition
    OBS_K_PALETTE,       // VGC palette write
    OBS_K_SCB,           // VGC scanline-control-byte write
    OBS_K_MODE,          // VGC video-mode change
    OBS_K_OSC,           // DOC oscillator event
    OBS_K_MILESTONE,     // toolbox vector-tap milestone
    OBS_K_CALL,          // toolbox / firmware call
    OBS_K_INPUT,         // keyboard / ADB / mouse / disk input
    OBS_K_SAMPLE         // the result of a LEVEL pull
};

// Honesty + record flags. The low bits ride in ObsRecord::flags; the descriptor
// carries the full 16-bit set (BUS_OBSERVABLE etc. are descriptor-static).
enum obs_flags : uint16_t {
    // --- descriptor-static honesty (plan §2.2) ---
    OBS_F_BUS_OBSERVABLE = 1u << 0,  // a real slot snoop can reproduce this signal
    OBS_F_INTERNAL_ONLY  = 1u << 1,  // software/FPGA-internal; a HW-vs-model diff filters it out
    OBS_F_EDGE_ONLY      = 1u << 2,  // value exists only mid-access — must be EVENT-observed, never peeked
    OBS_F_DURABLE        = 1u << 3,  // survives across resets (PRAM/RTC)
    OBS_F_ACTUATOR       = 1u << 4,  // writable injection point — never mixed into read-only records
    // --- per-record dynamic (ObsRecord::flags, low byte) ---
    OBS_F_READ           = 1u << 5,  // this txn was a read (0 = write) — mirrors SLOT_CTL_READ / BusTraceRecord.rw
    OBS_F_PROVENANCE_SH  = 1u << 6,  // BUS_TXN provenance: 1 = $C035-shadowed store (BusTraceRecord.src==1)
    OBS_F_PHANTOM        = 1u << 7   // a phantom/reconstructed record (not directly emitted by the model)
};

// ----------------------------------------------------------------------------
// 3. The EVENT record — 32 bytes, 8-aligned. A strict superset of the three
//    existing trace records; the SHR bus subset hashes the identical tuple.
// ----------------------------------------------------------------------------
struct ObsRecord {
    uint64_t cycle;   // NClock::get_cycles() — the ONE master timeline axis
    uint32_t sigid;   // packed (subsystem|signal|index)
    uint16_t seq;     // per-cycle sub-sequence: intra-cycle causal order
    uint8_t  kind;    // obs_kind
    uint8_t  flags;   // per-record obs_flags (low byte: READ / PROVENANCE_SH / PHANTOM)
    uint32_t addr;    // addr24 for BUS_TXN / memory; 0 for scalar
    uint32_t aux;     // BUS_TXN: (c14m_cost<<8 | cycle_type); PALETTE: (bank<<8|entry); ...
    uint64_t val;     // data / value / freq / accumulator
};
static_assert(sizeof(ObsRecord) == 32, "ObsRecord must stay a 32-byte wire envelope");

// The LEVEL descriptor. Raw-pointer accessor (NO std::function — the plan forbids
// it on the hot path). NOTE the owner MUST be STABLE for the run: CPU, clock, and
// device-chip objects live for the whole session, so a raw pointer is safe. A
// TRANSIENT owner (e.g. VideoScannerII, destroyed/recreated on a video-mode change)
// dangles — those need the deferred OwnerHandle indirection layer (plan §4.1), NOT
// this raw-owner path. Seams 3/4 register only stable-lifetime owners.
struct SigDesc {
    uint32_t    sigid;
    const char* path;         // e.g. "doc.osc[3].freq"
    uint8_t     type;         // obs_type
    uint8_t     kind;         // obs_kind
    uint8_t     cls;          // 0=SCALAR 1=ARRAY 2=MEMWINDOW 3=EVENT
    uint8_t     vol;          // 0=PER_CYCLE 1=ON_CHANGE 2=ON_EVENT 3=ON_DEMAND
    uint16_t    flags;        // obs_flags (descriptor-static honesty bits)
    const void* owner;        // STABLE base pointer for a LEVEL pull (nullptr for pure EVENT)
    uint32_t    off;          // byte offset within *owner
    uint8_t     shift;        // bit position (for a BOOL/flag pull)
    uint8_t     width;        // element size in bytes (SCALAR/ARRAY); 1 for a flag
    uint32_t    len;          // MEMWINDOW total byte length (0 for SCALAR/flag)
    const char* enum_tbl;     // optional enum-name table
};
enum obs_cls : uint8_t { OBS_C_SCALAR = 0, OBS_C_ARRAY, OBS_C_MEMWINDOW, OBS_C_EVENT };
enum obs_vol : uint8_t { OBS_V_PER_CYCLE = 0, OBS_V_ON_CHANGE, OBS_V_ON_EVENT, OBS_V_ON_DEMAND };

// ----------------------------------------------------------------------------
// 4. State: the master arm, the per-subsystem arm mask, the EVENT ring, the
//    registry, the per-cycle sub-sequence, and the keystone-reclaim globals.
// ----------------------------------------------------------------------------
inline bool                    g_obs_enabled     = false;          // master arm (default OFF)
inline uint32_t                g_obs_subsys_mask = 0xFFFFFFFFu;     // per-subsystem arm (bit == obs_subsys id)
inline std::vector<ObsRecord>  g_obs_ring;                         // the EVENT spine (grow; cap-wrap optional)
inline uint64_t                g_obs_cap         = 0;               // 0 = unbounded grow; else wrap at cap records
inline uint16_t                g_obs_seq         = 0;               // per-cycle sub-sequence counter
inline uint64_t                g_obs_seq_cycle   = ~0ull;           // cycle the seq counter belongs to
inline uint64_t                g_obs_ring_head   = 0;               // cap-wrap write head (reset with the ring)
inline std::vector<SigDesc>    g_obs_registry;                     // LEVEL/EVENT descriptors

// --- THE KEYSTONE RECLAIM (plan §2.4 aux, §2.3 CYCLE_COST) ------------------
// NClockIIgs::slow_incr_cycles() computes the TRUE 14M cost of each CPU cycle
// (SYNC ~14+, FAST_ROM/FAST 5, +5 on a refresh cycle) and its cycle_type, then
// DISCARDS both (c14m_this_cycle is a local; cycle_type is reset to FAST). That
// discarded per-cycle cost IS the machine's measured floor/ceiling timing. The
// Observatory owns the reclaim targets here; NClock stamps them when armed
// (default OFF, so an unarmed build is byte-identical). A BUS_TXN's aux field
// then carries (c14m_cost<<8 | cycle_type) — the true cost of the cycle it rode.
inline bool     g_obs_clock_cost_enabled = false;  // NClock stamps the three below only when true
inline uint32_t g_obs_c14m_cost          = 0;      // true 14M cost of the current CPU cycle
inline uint8_t  g_obs_cycle_type         = 2;      // CYCLE_TYPE_* of the current cycle (2=FAST)
inline uint64_t g_obs_now_cycle          = 0;      // the master CPU cycle count, mirrored here so ANY
                                                   // emit/view site (IRQ edges, the fault view) can
                                                   // stamp the ONE timeline axis without a clock handle
                                                   // (NClock::cycles is protected); also the stable
                                                   // owner backing the clock.cycle LEVEL signal.

// ----------------------------------------------------------------------------
// 5. EVENT emit — one gated push. Generalizes bus_trace_note / slot_bus_note /
//    mmu_state_trace_note. Per-subsystem arm honored via the sigid subsystem.
// ----------------------------------------------------------------------------
inline uint16_t obs_next_seq(uint64_t cycle) {
    if (cycle != g_obs_seq_cycle) { g_obs_seq_cycle = cycle; g_obs_seq = 0; }
    return g_obs_seq++;
}

inline void obs_note(uint64_t cycle, uint32_t sigid, uint8_t kind,
                     uint32_t addr, uint64_t val, uint32_t aux = 0, uint8_t flags = 0) {
    if (!g_obs_enabled) return;
    if (!((g_obs_subsys_mask >> obs_sigid_subsys(sigid)) & 1u)) return;
    ObsRecord r{ cycle, sigid, obs_next_seq(cycle), kind, flags, addr, aux, val };
    if (g_obs_cap && g_obs_ring.size() >= g_obs_cap) {
        // wrap: overwrite oldest (a bounded ring for per-cycle full-trace modes)
        g_obs_ring[g_obs_ring_head % g_obs_cap] = r;
        g_obs_ring_head++;
    } else {
        g_obs_ring.push_back(r);
    }
}

inline void obs_reset() {
    g_obs_ring.clear();
    g_obs_seq = 0;
    g_obs_seq_cycle = ~0ull;
    g_obs_ring_head = 0;
}

// ----------------------------------------------------------------------------
// 6. Registry — register a LEVEL/EVENT descriptor; enumerate by subsystem/glob.
//    (Wiring the emit sites + LEVEL pulls happens in later seams.)
// ----------------------------------------------------------------------------
inline void obs_register(const SigDesc& d) { g_obs_registry.push_back(d); }
inline const SigDesc* obs_find(uint32_t sigid) {
    for (const SigDesc& d : g_obs_registry) if (d.sigid == sigid) return &d;
    return nullptr;
}

// --- LEVEL registration helpers (the plan's "one line makes it observable" contract).
//     Field order matches SigDesc exactly. A registered signal is instantly
//     read/enumerate/snapshot-able with zero code at the observation site. ---
inline void obs_add_scalar(uint8_t sub, uint16_t sig, uint16_t idx, const char* path,
                           uint8_t type, const void* base, uint32_t off, uint8_t width,
                           uint16_t flags = 0, const char* enum_tbl = nullptr) {
    obs_register(SigDesc{ obs_sigid(sub, sig, idx), path, type, OBS_K_SAMPLE,
                          OBS_C_SCALAR, OBS_V_ON_DEMAND, flags, base, off, 0, width, 0, enum_tbl });
}
inline void obs_add_flag(uint8_t sub, uint16_t sig, const char* path,
                         const void* pbyte, uint32_t off, uint8_t bit, uint16_t flags = 0) {
    obs_register(SigDesc{ obs_sigid(sub, sig, 0), path, OBS_T_BOOL, OBS_K_SAMPLE,
                          OBS_C_SCALAR, OBS_V_ON_DEMAND, flags, pbyte, off, bit, 1, 0, nullptr });
}
inline void obs_add_array(uint8_t sub, uint16_t sig, const char* path, uint8_t type,
                          const void* base, uint32_t off, uint8_t elem_width,
                          uint16_t flags = 0) {
    obs_register(SigDesc{ obs_sigid(sub, sig, 0), path, type, OBS_K_SAMPLE,
                          OBS_C_ARRAY, OBS_V_ON_DEMAND, flags, base, off, 0, elem_width, 0, nullptr });
}
inline void obs_add_memwindow(uint8_t sub, uint16_t sig, const char* path,
                              const void* base, uint32_t len, uint16_t flags = 0) {
    obs_register(SigDesc{ obs_sigid(sub, sig, 0), path, OBS_T_BYTES, OBS_K_SAMPLE,
                          OBS_C_MEMWINDOW, OBS_V_ON_DEMAND, flags, base, 0, 0, 1, len, nullptr });
}

// --- LEVEL pull: side-effect-free read of a registered HOST-memory signal.
//     Resolves owner+off (+ idx*width for an ARRAY), loads little-endian by type,
//     extracts the bit for a BOOL/flag. REFUSES an EDGE_ONLY signal (its true value
//     exists only mid-access — it must be EVENT-observed via obs_note, never peeked;
//     this is the "$EE lied 6x" stale-peek guard, formalized). Returns false if the
//     signal is absent / edge-only / has a null owner. Emulated bank-space reads go
//     through MMU::probe_peek at the call site, NOT here (this is host struct memory). ---
inline bool obs_read(uint32_t sigid, uint64_t* out, uint32_t idx = 0) {
    const SigDesc* d = obs_find(sigid);
    if (!d || !d->owner || (d->flags & OBS_F_EDGE_ONLY)) return false;
    const uint8_t* p = (const uint8_t*)d->owner + d->off;
    if (d->cls == OBS_C_ARRAY) p += (size_t)idx * (d->width ? d->width : 1);
    uint64_t v = 0;
    switch (d->type) {
        case OBS_T_U16:
            v = (uint64_t)p[0] | ((uint64_t)p[1] << 8); break;
        case OBS_T_I16:
            v = (uint64_t)(int64_t)(int16_t)(p[0] | (p[1] << 8)); break;
        case OBS_T_U32: case OBS_T_RGB12:
            v = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24); break;
        case OBS_T_I32:
            v = (uint64_t)(int64_t)(int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8)
                                           | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)); break;
        case OBS_T_U64: case OBS_T_I64:  // same bit pattern; sign bit already at bit 63
            for (int i = 7; i >= 0; --i) v = (v << 8) | p[i]; break;
        case OBS_T_I8:
            v = (uint64_t)(int64_t)(int8_t)p[0]; break;
        case OBS_T_BOOL:
            v = ((uint64_t)p[0] >> d->shift) & 1u; break;
        default: /* U8/ENUM/BITFIELD/BYTES */
            v = p[0]; break;
    }
    if (out) *out = v;
    return true;
}

// --- MEMWINDOW pull: copy a registered host POD block (cover-first coverage).
//     Returns bytes copied (<= maxlen); 0 if absent / edge-only / not a memwindow. ---
inline uint32_t obs_read_window(uint32_t sigid, uint8_t* dst, uint32_t maxlen) {
    const SigDesc* d = obs_find(sigid);
    if (!d || !d->owner || d->cls != OBS_C_MEMWINDOW || (d->flags & OBS_F_EDGE_ONLY)) return 0;
    uint32_t n = (d->len < maxlen) ? d->len : maxlen;
    memcpy(dst, (const uint8_t*)d->owner + d->off, n);
    return n;
}

// --- Enumerate: the discoverability primitive. A '*' in the glob matches any run
//     (so "cpu.*", "doc.osc*", "*.freq" all work). Globbing the registry is free. ---
inline bool obs_glob_match(const char* pat, const char* s) {
    if (!pat || !s) return false;
    const char *star_p = nullptr, *star_s = nullptr;
    while (*s) {
        if (*pat == '*') { star_p = pat++; star_s = s; }
        else if (*pat == *s) { pat++; s++; }
        else if (star_p) { pat = star_p + 1; s = ++star_s; }
        else return false;
    }
    while (*pat == '*') pat++;
    return *pat == '\0';
}
inline std::vector<const SigDesc*> obs_enumerate(const char* glob) {
    std::vector<const SigDesc*> out;
    for (const SigDesc& d : g_obs_registry)
        if (d.path && obs_glob_match(glob, d.path)) out.push_back(&d);
    return out;
}

// Dump every registered MEMWINDOW matching a glob: path, byte length, first bytes.
// The "light the dark subsystems" readout — a whole POD device-state block is
// visible with zero per-field work (coverage-first, granularity-on-demand).
inline void obs_view_memwindows(const char* glob) {
    for (const SigDesc* d : obs_enumerate(glob)) {
        if (d->cls != OBS_C_MEMWINDOW) continue;
        uint8_t buf[24];
        uint32_t n = obs_read_window(d->sigid, buf, sizeof(buf));
        printf("OBS WINDOW: %-14s len=%-5u first:", d->path, d->len);
        for (uint32_t i = 0; i < n; i++) printf(" %02X", buf[i]);
        printf("%s\n", (d->len > n) ? " ..." : "");
    }
}

// ----------------------------------------------------------------------------
// 7. Dump / hash — the golden path, verbatim house-FNV. For a full-ring hash we
//    fold the same (addr, data, rw)-shaped tuple the existing traces hash, so a
//    projection of this stream reproduces their goldens. `mask/match` selects a
//    subsystem/signal projection (0/0 = whole ring).
// ----------------------------------------------------------------------------
inline uint64_t obs_hash_sel(uint32_t mask, uint32_t match, uint64_t* out_count = nullptr) {
    uint64_t h = HOUSE_FNV_BASIS, n = 0;
    for (const ObsRecord& r : g_obs_ring) {
        if ((r.sigid & mask) != match) continue;
        uint8_t b[6] = {
            (uint8_t)(r.addr & 0xFF), (uint8_t)((r.addr >> 8) & 0xFF), (uint8_t)((r.addr >> 16) & 0xFF),
            (uint8_t)(r.val & 0xFF), (uint8_t)(r.flags & OBS_F_READ ? 1 : 0), r.kind
        };
        for (uint8_t x : b) h = (h ^ x) * HOUSE_FNV_PRIME;
        n++;
    }
    if (out_count) *out_count = n;
    return h;
}
inline uint64_t obs_hash(uint64_t* out_count = nullptr) { return obs_hash_sel(0, 0, out_count); }

inline uint64_t obs_dump(const char* path, uint64_t* out_count = nullptr) {
    FILE* f = fopen(path, "wb");
    if (f) {
        if (!g_obs_ring.empty())
            fwrite(g_obs_ring.data(), sizeof(ObsRecord), g_obs_ring.size(), f);
        fclose(f);
    }
    return obs_hash(out_count);
}

// ----------------------------------------------------------------------------
// 8. Bus projection — reproduce bus_trace_dump()'s EXACT hashed tuple over the
//    BUS_TXN records in the SHR window ($E1:$2000-$9FFF == Mega II index
//    0x12000-0x19FFF), the same window+tuple bus_trace.hpp hashes. This is the
//    superset proof: an armed Observatory run's obs_hash_bus() equals
//    bus_trace_dump()'s hash BY CONSTRUCTION, because the same store site feeds
//    both, in the same order — so the general spine is a faithful superset of the
//    ad-hoc trace it subsumes, and the E1 determinism golden is a projection of it.
// ----------------------------------------------------------------------------
inline uint64_t obs_hash_bus(uint64_t* out_count = nullptr) {
    uint64_t h = HOUSE_FNV_BASIS, n = 0;
    for (const ObsRecord& r : g_obs_ring) {
        if (r.kind != OBS_K_BUS_TXN) continue;
        if (r.addr < 0x12000u || r.addr > 0x19FFFu) continue;   // SHR window (matches bus_trace)
        uint8_t rw = (r.flags & OBS_F_READ) ? 0 : 1;            // bus_trace convention: writes = 1
        uint8_t b[5] = { (uint8_t)(r.addr & 0xFF), (uint8_t)((r.addr >> 8) & 0xFF),
                         (uint8_t)((r.addr >> 16) & 0xFF), (uint8_t)(r.val & 0xFF), rw };
        for (uint8_t x : b) h = (h ^ x) * HOUSE_FNV_PRIME;
        n++;
    }
    if (out_count) *out_count = n;
    return h;
}
