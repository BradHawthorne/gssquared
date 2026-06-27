#pragma once
// ============================================================================
// iigs_toolbox.hpp — headless Apple IIgs Tool Locator / GS-OS call trace +
// closed-loop hooks (result retention, trace scoping, stop-on-fault, headless
// breakpoint, loader-error surfacing, symbolization).
//
// Traces calls through the Tool Locator dispatch ($E10000) and the GS/OS
// dispatchers ($E100A8 / $E100B0): logs call->name on entry, carry + A (the
// error code) on return. Retains the last result per call so an assertion gate
// can check e.g. QDStartUp carry==0. All env-gated, stdout, canonical IIgs
// naming. Hooked from the CPU core's execute_next (one cheap branch when off;
// if constexpr-compiled out of the 6502/65C02 cores).
// ============================================================================
#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>
#include <utility>
#include "cpu.hpp"
#include "iigs_symbols.hpp"
#include "debugger/trace_opcodes.hpp"

inline bool g_iigs_tbtrace_enabled = false;   // toolbox call/return trace
inline bool g_iigs_errhook_enabled = false;   // also trace GS/OS dispatch returns
inline bool g_iigs_brkdump_enabled = false;   // dump CPU state on BRK (crash)
inline bool g_iigs_stop_on_fault   = false;   // halt the spike on BRK/COP/SysFail
inline bool g_iigs_break_enabled   = false;   // headless breakpoint
inline uint32_t g_iigs_break_addr  = 0;       // 24-bit full-PC breakpoint
inline int  g_iigs_tbtrace_bank    = -1;      // only trace calls from this bank (-1 = all)
inline uint32_t g_iigs_trace_from  = 0;       // start tracing when PC first hits this (0 = always)
inline bool g_iigs_trace_armed     = false;   // trace_from has fired
inline int  g_iigs_brk_count       = 0;       // BRK/crash count (exit-taxonomy canary)
inline bool g_brkmem_on            = false;   // A2GSPU_BRKMEM: crash-path mem/PC ring dump
inline uint32_t g_pchist[64]       = {0};     // ring of last bank-2 PCs
inline int  g_pchist_i             = 0;

// ---- A2GSPU_ITRACE: additive, env-gated, per-instruction execution trace ----
// A standalone full-instruction trace (distinct from the toolbox-scoped trace
// above): logs PC(bank:addr), opcode byte, decoded mnemonic+operand, and the
// full register file, for a bounded window, so a far-RTL-into-garbage crash can
// be pinned to the exact faulting routine. Two arm modes (OR'd):
//   A2GSPU_ITRACE_FROM=<hexPC> : arm when full_pc first equals this 24-bit PC.
//   A2GSPU_ITRACE_FRAME=<N>    : arm at the start of headless frame N.
// A2GSPU_ITRACE_N=<count> caps the number of logged instructions (default 256).
// OFF by default (g_iigs_itrace_enabled gates the per-instruction call site to a
// single cheap branch when off; mirrors the BRKDUMP gating). Logs to stderr so
// it does not corrupt the SHR/golden stdout stream the gates parse.
inline bool     g_iigs_itrace_enabled = false; // master gate (any arm mode set)
inline uint32_t g_iigs_itrace_from    = 0;     // PC arm address (0 = no PC arm)
inline bool     g_iigs_itrace_use_pc  = false; // FROM was supplied
inline int      g_iigs_itrace_frame   = -1;    // frame arm (-1 = no frame arm)
inline bool     g_iigs_itrace_armed   = false; // window is open
inline int      g_iigs_itrace_n       = 256;   // max instructions to log
inline int      g_iigs_itrace_logged  = 0;     // logged so far
inline int      g_iigs_cur_frame      = 0;     // updated by the headless spike loop

inline void iigs_cpu_state_dump_regs(cpu_state *cpu, const char *why);  // fwd

// Per-instruction trace step. Called at the landing PC (cpu->full_pc), BEFORE
// fetch, identically to iigs_tb_on_landing. Reads the 3 operand bytes straight
// off the MMU so the decode reflects the live image (catches a corrupted byte).
// The operand width of IMM/REP/SEP-affected ops is approximated from the disasm
// table's fixed size (this is a crash post-mortem, not a cycle model).
inline void iigs_itrace_step(cpu_state *cpu) {
    // Arm on PC match (one-shot) — independent of the frame arm.
    if (!g_iigs_itrace_armed && g_iigs_itrace_use_pc &&
        cpu->full_pc == g_iigs_itrace_from) {
        g_iigs_itrace_armed = true;
        fprintf(stderr, "IIGS ITRACE: armed at PC=%02X/%04X (frame %d)\n",
                cpu->pb, cpu->pc, g_iigs_cur_frame);
    }
    if (!g_iigs_itrace_armed) return;
    if (g_iigs_itrace_logged >= g_iigs_itrace_n) return;

    uint32_t pc = cpu->full_pc;
    uint8_t  op = cpu->mmu->read(pc);
    uint8_t  b1 = cpu->mmu->read((pc & 0xFF0000) | ((pc + 1) & 0xFFFF));
    uint8_t  b2 = cpu->mmu->read((pc & 0xFF0000) | ((pc + 2) & 0xFFFF));
    uint8_t  b3 = cpu->mmu->read((pc & 0xFF0000) | ((pc + 3) & 0xFFFF));

    const disasm_entry       *de = &disasm_table[op];
    const address_mode_entry *am = &address_mode_formats[de->mode];

    // Build the operand string from the format + the live bytes. REL/REL_L
    // also show the resolved branch target.
    char operand[48]; operand[0] = '\0';
    char extra[40];   extra[0] = '\0';
    switch (de->mode) {
        case IMP: case ACC:
            break;
        case IMM: case REL: case ZP: case ZP_X: case ZP_Y:
        case INDEX_INDIR: case INDIR_INDEX: case ZP_IND:
        case REL_S: case REL_S_Y: case IND_LONG: case IND_Y_LONG:
            snprintf(operand, sizeof(operand), am->format, b1);
            if (de->mode == REL) {
                int8_t d = (int8_t)b1;
                uint16_t tgt = (uint16_t)((pc & 0xFFFF) + 2 + d);
                snprintf(extra, sizeof(extra), " ->%02X/%04X",
                         (unsigned)(pc >> 16), tgt);
            }
            break;
        case ABS: case ABS_X: case ABS_Y: case INDIR: case ABS_IND_X:
        case REL_L: case ABS_IND_LONG: case IMM_S:
            snprintf(operand, sizeof(operand), am->format,
                     (unsigned)(b1 | (b2 << 8)));
            if (de->mode == REL_L) {
                int16_t d = (int16_t)(b1 | (b2 << 8));
                uint16_t tgt = (uint16_t)((pc & 0xFFFF) + 3 + d);
                snprintf(extra, sizeof(extra), " ->%02X/%04X",
                         (unsigned)(pc >> 16), tgt);
            }
            break;
        case ABSL: case ABSL_X:
            snprintf(operand, sizeof(operand), am->format,
                     (unsigned)(b1 | (b2 << 8) | (b3 << 16)));
            break;
        case MOVE:
            snprintf(operand, sizeof(operand), am->format, b1, b2);
            break;
        default:
            snprintf(operand, sizeof(operand), "?%02X", b1);
            break;
    }

    char sym[80]; iigs_sym_resolve(pc, sym, sizeof(sym));
    fprintf(stderr,
        "ITRACE %02X/%04X: %02X %-4s %-12s%s "
        "A=%04X X=%04X Y=%04X S=%04X D=%04X DBR=%02X P=%02X e=%d%s%s\n",
        cpu->pb, cpu->pc, op, de->opcode, operand, extra,
        cpu->a, cpu->x, cpu->y, cpu->sp, cpu->d, cpu->db, cpu->p, (int)cpu->E,
        sym[0] ? "  " : "", sym);
    g_iigs_itrace_logged++;
    if (g_iigs_itrace_logged == g_iigs_itrace_n)
        fprintf(stderr, "IIGS ITRACE: window full (%d instrs)\n",
                g_iigs_itrace_n);
}

// Frame-arm hook: called from the headless spike loop once per frame so an
// A2GSPU_ITRACE_FRAME=N arm opens the window at the right frame even when no
// single PC is known. Idempotent; only opens, never closes.
inline void iigs_itrace_frame_tick(int frame) {
    g_iigs_cur_frame = frame;
    if (!g_iigs_itrace_armed && g_iigs_itrace_frame >= 0 &&
        frame >= g_iigs_itrace_frame) {
        g_iigs_itrace_armed = true;
        fprintf(stderr, "IIGS ITRACE: armed at frame %d\n", frame);
    }
}

struct IigsPendingCall {
    uint32_t ret_addr;    // full 24-bit address the matching RTL returns to
    uint16_t callword;    // tool word (func<<8 | toolset); 0 for a GS/OS call
    uint8_t  kind;        // 0 = toolbox, 1 = GS/OS class-1, 2 = GS/OS class-0
    uint8_t  caller_bank; // bank of the caller (for trace scoping)
};
inline std::vector<IigsPendingCall> g_iigs_pending;

// Last (carry, A) result per tool word — consumed by the assertion gate.
inline std::map<uint16_t, std::pair<bool, uint16_t>> g_iigs_last_result;
// Last GS/OS dispatch carry-set error code (surfaces $1104 / $110A loader errors).
inline uint16_t g_iigs_last_gsos_err = 0;

inline const char *iigs_tool_name(uint16_t w) {
    switch (w) {
        case 0x0201: return "TLStartUp";      case 0x0301: return "TLShutDown";
        case 0x0E01: return "LoadTools";      case 0x0F01: return "LoadOneTool";
        case 0x0202: return "MMStartUp";      case 0x0302: return "MMShutDown";
        case 0x0902: return "NewHandle";      case 0x1002: return "DisposeHandle";
        case 0x2003: return "GetNewID";       case 0x1D03: return "DeleteID";
        case 0x0303: return "MTStartUp";      case 0x0403: return "MTShutDown";
        case 0x0204: return "QDStartUp";      case 0x0304: return "QDShutDown";
        case 0x1504: return "ClearScreen";    case 0x5404: return "PaintRect";
        case 0x3704: return "SetSolidPenPat"; case 0x0206: return "EMStartUp";
        default: return "(tool)";
    }
}

inline void iigs_tb_on_landing(cpu_state *cpu) {
    uint32_t lpc = cpu->full_pc;

    // Headless breakpoint + the instant-stop substrate (#10/#3): a full 24-bit
    // PC compare (not the GUI's 16-bit-only matcher). Set halt -> the in-flight
    // frame finishes, then run_one_frame's top-of-frame guard exits the spike.
    if (g_iigs_break_enabled && lpc == g_iigs_break_addr) {
        iigs_cpu_state_dump_regs(cpu, "BREAK");
        cpu->halt = 2 /* HLT_USER */;
    }

    if (!g_iigs_tbtrace_enabled) return;

    // trace_from: begin logging only once PC first reaches the trigger address.
    if (g_iigs_trace_from && !g_iigs_trace_armed && lpc == g_iigs_trace_from)
        g_iigs_trace_armed = true;
    bool log_window = (g_iigs_trace_from == 0) || g_iigs_trace_armed;

    char sym[80];

    // (1) Return match.
    if (!g_iigs_pending.empty() && lpc == g_iigs_pending.back().ret_addr) {
        IigsPendingCall pend = g_iigs_pending.back();
        g_iigs_pending.pop_back();
        bool m8 = cpu->E || (cpu->p & 0x20);
        uint16_t aval = m8 ? (uint16_t)(cpu->a & 0xFF) : cpu->a;
        bool carry = (cpu->p & FLAG_C) != 0;
        bool bank_ok = (g_iigs_tbtrace_bank < 0) || (pend.caller_bank == g_iigs_tbtrace_bank);
        if (pend.kind == 0) {
            g_iigs_last_result[pend.callword] = {carry, aval};   // for the assert gate
            if (log_window && bank_ok)
                printf("IIGS TOOLBOX: ret %s ($%04X) carry=%d err=$%04X\n",
                       iigs_tool_name(pend.callword), pend.callword, carry ? 1 : 0, aval);
        } else {
            if (carry) g_iigs_last_gsos_err = aval;
            if (log_window && bank_ok && (g_iigs_errhook_enabled || carry))
                printf("IIGS GSOS: ret (class-%d) carry=%d err=$%04X%s\n",
                       pend.kind == 1 ? 1 : 0, carry ? 1 : 0, aval,
                       carry ? "  <-- SYSTEM/LOADER ERROR" : "");
        }
    }

    // (2) Call detect.
    uint8_t kind = 0xFF;
    if (lpc == 0xE10000) kind = 0;
    else if (lpc == 0xE100A8) kind = 1;
    else if (lpc == 0xE100B0) kind = 2;
    if (kind == 0xFF) return;

    uint16_t sp = cpu->sp;
    uint32_t pcl = cpu->mmu->read((sp + 1) & 0xFFFF);
    uint32_t pch = cpu->mmu->read((sp + 2) & 0xFFFF);
    uint32_t pbr = cpu->mmu->read((sp + 3) & 0xFFFF);
    uint32_t ret = (pbr << 16) | ((((pch << 8) | pcl) + 1) & 0xFFFF);
    uint8_t caller_bank = (uint8_t)pbr;
    bool bank_ok = (g_iigs_tbtrace_bank < 0) || (caller_bank == g_iigs_tbtrace_bank);

    // Auto-bridge the symbolizer load base (best-effort): a GS/OS-loaded app's
    // first toolbox call reveals the runtime bank GS/OS relocated it to. Correct
    // to the bank; may be off by the within-bank load offset. A2GSPU_SYM_BASE
    // pins it explicitly (locked). App region = banks $02..$DF (not ROM/Mega II).
    if (g_iigs_syms_loaded && !g_iigs_sym_base_locked && g_iigs_sym_base == 0 &&
        caller_bank >= 0x02 && caller_bank < 0xE0) {
        g_iigs_sym_base = (uint32_t)caller_bank << 16;
        printf("IIGS SYM: auto-base $%06X (from bank-$%02X toolbox call)\n",
               g_iigs_sym_base, caller_bank);
    }

    uint16_t callword = 0;
    if (kind == 0) {
        bool x8 = cpu->E || (cpu->p & 0x10);
        callword = x8 ? (uint16_t)(cpu->x & 0xFF) : cpu->x;
        if (log_window && bank_ok) {
            iigs_sym_resolve(ret - 1, sym, sizeof(sym));
            printf("IIGS TOOLBOX: call %s ($%04X)%s%s\n",
                   iigs_tool_name(callword), callword, sym[0] ? "  from " : "", sym);
        }
    } else if (log_window && bank_ok && g_iigs_errhook_enabled) {
        printf("IIGS GSOS: call class-%d dispatch @ $%06X\n", kind == 1 ? 1 : 0, lpc);
    }
    g_iigs_pending.push_back({ret, callword, kind, caller_bank});
    if (g_iigs_pending.size() > 128) g_iigs_pending.erase(g_iigs_pending.begin());
}

// Regs-only CPU-state dump (no MMU/disassembler dependency) — safe from the
// CPU core (BRK/break). Symbolizes the PC when a symbol table is loaded.
inline void iigs_cpu_state_dump_regs(cpu_state *cpu, const char *why) {
    char sym[80]; iigs_sym_resolve(cpu->full_pc, sym, sizeof(sym));
    printf("IIGS CPU [%s]: PC=%02X/%04X%s%s A=%04X X=%04X Y=%04X S=%04X D=%04X "
           "DBR=%02X P=%02X e=%d\n",
           why ? why : "", cpu->pb, cpu->pc, sym[0] ? " " : "", sym,
           cpu->a, cpu->x, cpu->y, cpu->sp, cpu->d, cpu->db, cpu->p, (int)cpu->E);
}
