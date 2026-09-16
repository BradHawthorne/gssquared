#include <cstring>

#include "mmus/mmu.hpp"
#include "debugger/disasm.hpp"
#include "debugger/trace_opcodes.hpp"
#include "debugger/trace_format.hpp"
#include "debugger/line_buffer.hpp"

Disassembler::Disassembler(MMU *mmu, processor_type cputype) : mmu(mmu), cpu_type(cputype) {
    address = 0;
    cpu_mask = 0;
    if (cputype == PROCESSOR_65816) {
        cpu_mask = CPU_65816;
    } else if (cputype == PROCESSOR_65C02) {
        cpu_mask = CPU_65C02;
    } else if (cputype == PROCESSOR_6502) {
        cpu_mask = CPU_6502;
    }
}

Disassembler::~Disassembler() {
}

void Disassembler::setAddress(uint32_t address) {
    this->address = address;
}

void Disassembler::set_mx(bool m_8bit, bool x_8bit) {
    this->m_8bit = m_8bit;
    this->x_8bit = x_8bit;
}

void Disassembler::set_format(const trace_column_layout &layout, bool show_opbytes) {
    layout_ = layout;
    show_opbytes_ = show_opbytes;
    layout_set_ = true;
}

// FORK: AN OBSERVE VERB MUST NOT ACTUATE THE MACHINE.
//
// Upstream's guard compared the FULL 24-bit address, so it only ever fired in
// bank zero. `dis E1C055 1 <file>` therefore fell through to mmu->read() -- the
// CPU's read path -- and physically flipped PAGE2 and (by reading the operand
// bytes at $C056/$C057) HIRES. One observe command changed what the machine was
// displaying. The low 16 bits are what decides whether an address is I/O, in
// every bank; and the bound was exclusive, so $C0FF itself fell through too.
//
// The guard MUST stay in front of the probe below: MMU_IIgs::probe_peek takes
// an early exit for banks $E0/$E1 straight into the Mega II image with no
// $C0xx test, so probe_readable($E1C055) is true and probe_peek would answer
// with the RAM under the I/O space rather than refusing.
//
// Everything else reads through probe_readable/probe_peek instead of read():
// same bank/language-card/Mega-II mapping the CPU sees, and no side effects at
// all -- which is also why the $C400 mockingboard hack is gone rather than
// widened. That hack returned $DD for $xxC4xx in EVERY bank, so it hid real RAM
// at $7EC4xx; probe_peek cannot disturb a 6522 and needs no hack. Known
// imperfection, pre-existing and left as one: at $E1:C100-CFFF probe_peek
// returns Mega II RAM rather than the slot ROM the CPU would fetch.
uint8_t Disassembler::read_mem(uint32_t address) {
    const uint16_t lo = (uint16_t)(address & 0xFFFF);
    if (lo >= 0xC000 && lo <= 0xC0FF) {
        last_read_ok = false;
        n_unreadable++;
        return 0xEE; // I/O: reading it IS the side effect. Never touch it.
    }
    if (!mmu->probe_readable(address)) {
        last_read_ok = false;
        n_unreadable++;
        return 0xEE; // handler-backed: probe_peek would answer with floating bus
    }
    return mmu->probe_peek(address);
}

int Disassembler::resolve_insn_size(const disasm_entry *da, const address_mode_entry *am) const {
    if (cpu_type != PROCESSOR_65816) {
        return am->size;
    }
    if (da->flags & (DISASM_SEP | DISASM_REP)) {
        return 2; // always opcode + 8-bit mask
    }
    if (da->flags & DISASM_IMM_M) {
        return m_8bit ? 2 : 3;
    }
    if (da->flags & DISASM_IMM_X) {
        return x_8bit ? 2 : 3;
    }
    return am->size;
}

void Disassembler::apply_sep_rep(const disasm_entry *da, uint8_t mask) {
    if (da->flags & DISASM_SEP) {
        if (mask & 0x20) {
            m_8bit = true;
        }
        if (mask & 0x10) {
            x_8bit = true;
        }
    } else if (da->flags & DISASM_REP) {
        if (mask & 0x20) {
            m_8bit = false;
        }
        if (mask & 0x10) {
            x_8bit = false;
        }
    }
}

void Disassembler::disassemble_one() {
    line_buffer buffer;
    buffer.reset();

    // Fallback: PC at column 0 with Bytes/Op/Operand spacing (montest / no set_format).
    trace_column_layout layout = layout_;
    if (!layout_set_) {
        const bool is_65816 = (cpu_type == PROCESSOR_65816);
        // cycle_w,a,x,y,sp,p unused when PC starts at 0
        if (is_65816) {
            layout = {0, 0, 0, 0, 0, 0, /*pc*/ 0, /*opbytes*/ 9, /*opcode*/ 21, /*operand*/ 26,
                      0, 0, 0, 0, 12};
        } else {
            layout = {0, 0, 0, 0, 0, 0, /*pc*/ 0, /*opbytes*/ 6, /*opcode*/ 18, /*operand*/ 23,
                      0, 0, 0, 0, 12};
        }
    }

    const bool is_65816 = (cpu_type == PROCESSOR_65816);
    last_read_ok = true;
    uint8_t opcode = read_mem(address);
    if (!last_read_ok) {
        // FORK: NAME THE HOLE RATHER THAN DECODE THE PLACEHOLDER. The
        // placeholder is $EE, so the listing used to read "EE EE EE  INC $EEEE"
        // over an I/O page -- an instruction that is not there, presented as
        // though it were read. Advance one byte: over a $C0xx page that is one
        // honest line per byte, which is what the reader needs to see.
        emit_insn_pc(buffer, layout, is_65816, (uint8_t)((address >> 16) & 0xFF),
                     (uint16_t)(address & 0xFFFF));
        buffer.pos(layout.opcode);
        buffer.put("<unreadable>");
        address += 1;
        output_buffer.push_back(buffer.get());
        return;
    }
    const disasm_entry *da = &disasm_table[opcode];
    const address_mode_entry *am = &address_mode_formats[da->mode];

    int insn_size = resolve_insn_size(da, am);
    uint8_t op_sz = 1;
    if (da->mode == IMM) {
        op_sz = (insn_size == 3) ? 2 : 1;
    }

    uint32_t operand = 0;
    for (int i = 1; i < insn_size; i++) {
        uint8_t b = read_mem(address + i);
        operand |= ((uint32_t)b) << ((i - 1) * 8);
    }

    uint8_t pb = (uint8_t)((address >> 16) & 0xFF);
    uint16_t pc = (uint16_t)(address & 0xFFFF);

    emit_insn_pc(buffer, layout, is_65816, pb, pc);
    emit_insn_opbytes(buffer, layout, show_opbytes_, opcode, operand, insn_size);
    emit_insn_mnemonic_operand(buffer, layout, da, am, cpu_mask, pb, pc, operand, op_sz, nullptr);

    if (da->flags & (DISASM_SEP | DISASM_REP)) {
        apply_sep_rep(da, (uint8_t)(operand & 0xFF));
    }
    // FORK: XCE is the one mode change a static walk cannot follow -- the new E
    // is whatever carry held -- so it takes the conservative reading (8-bit),
    // and the CTRL `dis` ack says the widths are an assumption.
    if (is_65816 && opcode == 0xFB) {
        m_8bit = true;
        x_8bit = true;
    }

    address += insn_size;
    output_buffer.push_back(buffer.get());
}

std::vector<std::string> Disassembler::disassemble(int count) {
    for (int i = 0; i < count; i++) {
        disassemble_one();
    }
    std::vector<std::string> ret(std::move(output_buffer));
    output_buffer.clear();
    return ret;
}
