#pragma once
// ============================================================================
// obs_iigs.hpp — IIgs-specific bindings for the Observatory spine.
//
// Registers the CPU + clock LEVEL signals against the live (stable-lifetime)
// objects, and renders the boot-fault-context view: on a fault (BRK / wild jump)
// it pulls the registered cpu.* / clock.* signals through obs_read and dumps a
// bounded, side-effect-free code window via MMU::probe_peek — one correlated
// report instead of the scattered brkdump/intlog/errhook prints. Kept separate
// from the platform-agnostic obs_signal.hpp.
//
// Golden-neutral: registration only pushes descriptors (nothing pulls them on the
// hot path), and the fault view renders ONLY when explicitly called — at a fault
// (no fault fires on the clean boot golden) or at teardown (after the golden
// hashes are already computed). Generic Apple IIgs naming only (public surface).
// ============================================================================
#include "obs_signal.hpp"
#include "cpu.hpp"
#include "mmus/mmu.hpp"    // MMU::probe_peek (the observation-free reader)
#include <cstdio>

// Signal ids within the CPU / CLOCK subsystems (the sigid 'signal' field).
enum obs_cpu_sig { OCS_A = 0, OCS_X, OCS_Y, OCS_SP, OCS_D, OCS_PC, OCS_PBR, OCS_DBR, OCS_P,
                   OCS_FN, OCS_FV, OCS_FM, OCS_FX, OCS_FD, OCS_FI, OCS_FZ, OCS_FC };
enum obs_clk_sig { OKS_C14M_COST = 0, OKS_CYCLE_TYPE };

// Register cpu.* + clock.* LEVEL signals against the STABLE live CPU + the reclaim
// globals. Uses direct member addresses as owners (owner=&member, off=0) — robust
// for cpu_state's nested anonymous unions and needs no offsetof. Call ONCE at arm
// time. The E emulation flag is a C++ bit-field (no addressable byte) so it is NOT
// registered here — the fault view reads cpu->E directly.
inline void obs_register_iigs_core(cpu_state* cpu) {
    if (!cpu) return;
    const uint16_t F = OBS_F_INTERNAL_ONLY;   // CPU/clock internals: not bus-observable on real HW
    obs_add_scalar(OBS_SUB_CPU, OCS_A,   0, "cpu.reg.a",   OBS_T_U16, &cpu->a,  0, 2, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_X,   0, "cpu.reg.x",   OBS_T_U16, &cpu->x,  0, 2, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_Y,   0, "cpu.reg.y",   OBS_T_U16, &cpu->y,  0, 2, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_SP,  0, "cpu.reg.s",   OBS_T_U16, &cpu->sp, 0, 2, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_D,   0, "cpu.reg.d",   OBS_T_U16, &cpu->d,  0, 2, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_PC,  0, "cpu.reg.pc",  OBS_T_U16, &cpu->pc, 0, 2, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_PBR, 0, "cpu.reg.pbr", OBS_T_U8,  &cpu->pb, 0, 1, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_DBR, 0, "cpu.reg.dbr", OBS_T_U8,  &cpu->db, 0, 1, F);
    obs_add_scalar(OBS_SUB_CPU, OCS_P,   0, "cpu.reg.p",   OBS_T_U8,  &cpu->p,  0, 1, F);
    // status flags = individual bits of the single P byte (owner=&cpu->p, by bit position)
    obs_add_flag(OBS_SUB_CPU, OCS_FN, "cpu.flag.n", &cpu->p, 0, 7, F);
    obs_add_flag(OBS_SUB_CPU, OCS_FV, "cpu.flag.v", &cpu->p, 0, 6, F);
    obs_add_flag(OBS_SUB_CPU, OCS_FM, "cpu.flag.m", &cpu->p, 0, 5, F);  // 65816 accumulator-width
    obs_add_flag(OBS_SUB_CPU, OCS_FX, "cpu.flag.x", &cpu->p, 0, 4, F);  // 65816 index-width
    obs_add_flag(OBS_SUB_CPU, OCS_FD, "cpu.flag.d", &cpu->p, 0, 3, F);
    obs_add_flag(OBS_SUB_CPU, OCS_FI, "cpu.flag.i", &cpu->p, 0, 2, F);
    obs_add_flag(OBS_SUB_CPU, OCS_FZ, "cpu.flag.z", &cpu->p, 0, 1, F);
    obs_add_flag(OBS_SUB_CPU, OCS_FC, "cpu.flag.c", &cpu->p, 0, 0, F);
    // clock: the reclaimed per-cycle cost (stable globals in obs_signal.hpp)
    obs_add_scalar(OBS_SUB_CLOCK, OKS_C14M_COST,  0, "clock.c14m_cost",  OBS_T_U32, &g_obs_c14m_cost,  0, 4, F);
    obs_add_scalar(OBS_SUB_CLOCK, OKS_CYCLE_TYPE, 0, "clock.cycle_type", OBS_T_U8,  &g_obs_cycle_type, 0, 1, F);
}

// The boot-fault-context view. Coherent snapshot: the 24-bit PC/DBR/E, every
// registered cpu.* / clock.* LEVEL signal (via obs_read — proves the LEVEL path),
// and a bounded code window around the faulting instruction via probe_peek. A byte
// probe_peek cannot trust (bank <= $01 reading floating-bus $EE) is flagged '?'.
inline void obs_view_fault(cpu_state* cpu, const char* why) {
    if (!cpu) return;
    if (obs_enumerate("cpu.reg.a").empty()) obs_register_iigs_core(cpu);   // self-arm if not yet registered
    printf("\n=== OBS FAULT VIEW (%s) ===\n", why ? why : "?");
    printf("OBS FAULT: PC=%02X/%04X  DBR=%02X  E=%d  cpu_type=%d\n",
           cpu->pb, cpu->pc, cpu->db, (int)cpu->E, (int)cpu->cpu_type);
    for (const SigDesc* d : obs_enumerate("cpu.*")) {
        uint64_t v = 0;
        if (obs_read(d->sigid, &v))
            printf("OBS FAULT:   %-12s = %0*llX\n", d->path, d->width * 2, (unsigned long long)v);
    }
    for (const SigDesc* d : obs_enumerate("clock.*")) {
        uint64_t v = 0;
        if (obs_read(d->sigid, &v))
            printf("OBS FAULT:   %-16s = %llu\n", d->path, (unsigned long long)v);
    }
    if (cpu->mmu) {
        uint32_t pcaddr = ((uint32_t)cpu->pb << 16) | cpu->pc;
        uint32_t lo = (pcaddr >= 8) ? (pcaddr - 8) : 0;
        printf("OBS FAULT:   code @%06X:", lo);
        for (uint32_t a = lo; a < lo + 24; a++) {
            uint8_t b = cpu->mmu->probe_peek(a);
            bool suspect = (((a >> 16) & 0xFF) <= 0x01) && (b == 0xEE);   // $EE-liar guard
            if (suspect) printf(" %02X?", b); else printf(" %02X", b);
        }
        printf("  (probe_peek, side-effect-free; '?' = bank<=01 floating-bus $EE)\n");
    }
    printf("=== end OBS FAULT VIEW ===\n");
}
