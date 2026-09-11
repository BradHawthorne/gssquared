#include <cstring>

#include "mmus/mmu.hpp"
#include "debugger/disasm.hpp"
#include "debugger/trace_opcodes.hpp"
#include "util/HexDecode.hpp"
#include "debugger/line_buffer.hpp"

//           11111111112222222222
// 012345678901234567890123456789
// xxxx: xx xx xx    LDA  $1234

#define TB_ADDRESS 0
#define TB_OP1 6
#define TB_OP2 9
#define TB_OP3 12
#define TB_OP4 15

#define TB_OPCODE 18
#define TB_OPERAND 23
#define TB_EOL 40

Disassembler::Disassembler(MMU *mmu, processor_type cputype) : mmu(mmu), cpu_type(cputype) {
    address = 0;
    length = 0;
    line_prepend = 0;
}

Disassembler::~Disassembler() {
}

void Disassembler::setAddress(uint32_t address) {
    this->address = address;
}

// AN OBSERVE VERB MUST NOT ACTUATE THE MACHINE.
//
// This guard compared the FULL 24-bit address, so it only ever fired in bank
// zero. `dis E1C055 1 <file>` therefore fell through to mmu->read() -- the
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
// all -- which is also why the old $C400 mockingboard hack is gone rather than
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

// The 65816's immediate operand width. REP/SEP/BRK/COP take one operand byte
// whatever M and X are; the A-group follows M and the index group follows X.
// PEA ($F4) is IMM_S, already sized 3, and is not routed through here.
static int imm_size(uint8_t op, bool m8, bool x8) {
    switch (op) {
    case 0xC2: case 0xE2:                                            // REP / SEP
        return 2;
    case 0x09: case 0x29: case 0x49: case 0x69:                      // ORA AND EOR ADC
    case 0x89: case 0xA9: case 0xC9: case 0xE9:                      // BIT LDA CMP SBC
        return m8 ? 2 : 3;
    case 0xA0: case 0xA2: case 0xC0: case 0xE0:                      // LDY LDX CPY CPX
        return x8 ? 2 : 3;
    default:
        return 2;
    }
}

void Disassembler::disassemble_one() {
    line_buffer buffer;
    char snpbuf[100];
    size_t buffer_size = sizeof(buffer);
    size_t snpbuf_size = sizeof(snpbuf);

    //memset(buffer, ' ', buffer_size);

    //char *bufptr = buffer + line_prepend;

    last_read_ok = true;
    uint8_t opcode = read_mem(address);
    if (!last_read_ok) {
        // NAME THE HOLE RATHER THAN DECODE THE PLACEHOLDER. The placeholder is
        // $EE, so the listing used to read "EE EE EE  INC $EEEE" over an I/O
        // page -- an instruction that is not there, presented as though it were
        // read. Advance one byte: over a $C0xx page that is one honest line per
        // byte, which is what the reader needs to see.
        buffer.pos(line_prepend);
        if (cpu_type == PROCESSOR_65816) buffer.put(address);
        else buffer.put((uint16_t)address);
        buffer.put(": ");
        buffer.pos(line_prepend + TB_OPCODE);
        buffer.put("<unreadable>");
        address += 1;
        output_buffer.push_back(buffer.get());
        return;
    }

    const disasm_entry *da = &disasm_table[opcode];
    const char *opcode_name = da->opcode;
    const address_mode_entry *am = &address_mode_formats[da->mode];
    // The 65816 immediate width is NOT in the address-mode table; it depends on
    // the M/X the caller seeded and on the REP/SEP/XCE this walk has passed.
    const int isz = (cpu_type == PROCESSOR_65816 && da->mode == IMM)
                        ? imm_size(opcode, m8, x8) : am->size;
    uint32_t operand;

    buffer.pos(line_prepend);
    if (cpu_type == PROCESSOR_65816) {        
        buffer.put(address);
/*         decode_hex_byte(bufptr + TB_ADDRESS, address >> 16);
        bufptr[TB_ADDRESS + 2] = '/';
        bufptr+=3; */
    } else {
        buffer.put((uint16_t)address );
        //decode_hex_word(bufptr + TB_ADDRESS, address);
    }

    buffer.put(": ");

    buffer.put((uint8_t)opcode);
    buffer.put(' ');

    operand = 0; uint8_t op2, op3, op4;
    if (isz == 2) {
        op2 = read_mem(address + 1);
        operand = op2;
        buffer.put((uint8_t)op2);
        buffer.put(' ');
        buffer.pos(buffer.pos() + 6);
    } else if (isz == 3) {
        op2 = read_mem(address + 1);
        op3 = read_mem(address + 2);
        operand = op2 | (op3 << 8);
        buffer.put((uint8_t)op2);
        buffer.put(' ');
        buffer.put((uint8_t)op3);
        buffer.put(' ');
        buffer.pos(buffer.pos() + 3);
    } else if (isz == 4) {
        op2 = read_mem(address + 1);
        op3 = read_mem(address + 2);
        op4 = read_mem(address + 3);
        operand = op2 | (op3 << 8) | (op4 << 16);
        buffer.put((uint8_t)op2);
        buffer.put(' ');
        buffer.put((uint8_t)op3);
        buffer.put(' ');
        buffer.put((uint8_t)op4);
        buffer.put(' ');
    } else {
        buffer.pos(buffer.pos() + 9);
    }

    if (opcode_name) buffer.put((char *)opcode_name);
    //memcpy(bufptr + TB_OPCODE, opcode_name, strlen(opcode_name));
    else buffer.put("???");
    //memcpy(bufptr + TB_OPCODE, "???", 3);
    buffer.put("  ");

    switch (da->mode) {
        case NONE:
            //memcpy(bufptr + TB_OPERAND, "???", 3);
            buffer.put("???");
            break;
        case ACC:
        case IMP:
            //memcpy(bufptr + TB_OPERAND, am->format, strlen(am->format));
            buffer.put((char *)am->format);
            break;

        case ABS:
        case ABS_X:
        case ABS_Y:
        case IMM:
        case INDIR:
        case ABS_IND_X: // 65c02
        case INDEX_INDIR:
        case INDIR_INDEX:
        case ZP:
        case ZP_X:
        case ZP_Y:
        case ZP_IND: // 65c02
        case IND_LONG:
        case IND_Y_LONG:
        case REL_S:
        case REL_S_Y:
        case ABSL:
        case ABSL_X:
        case ABS_IND_LONG:
        case IMM_S:
            // A 16-bit immediate prints four digits. address_mode_formats has
            // one IMM row and it says "#$%02X", so the wide form has to be
            // chosen here, beside the width that produced it.
            snprintf(snpbuf, snpbuf_size,
                     (da->mode == IMM && isz == 3) ? "#$%04X" : am->format, operand);
            //memcpy(bufptr + TB_OPERAND, snpbuf, strlen(snpbuf));
            buffer.put(snpbuf);
            break;
        case REL: {
                uint16_t btarget = (address+2) + (int8_t)operand;
                snprintf(snpbuf, snpbuf_size, "$%04X", btarget);
                //memcpy(bufptr + TB_OPERAND, snpbuf, strlen(snpbuf));
                buffer.put(snpbuf);
            }
            break;
        case REL_L: {
                uint16_t btargetl = (address+3) + (int16_t)operand;
                snprintf(snpbuf, snpbuf_size, "$%04X", btargetl);
                //memcpy(bufptr + TB_OPERAND, snpbuf, strlen(snpbuf));
                buffer.put(snpbuf);
            }
            break;
        case MOVE:  
            {
                printf("move operand %04X\n", operand);
                snprintf(snpbuf, snpbuf_size, am->format,
                    operand & 0x00FF, operand >> 8);
                //memcpy(bufptr + TB_OPERAND, snpbuf, strlen(snpbuf));
                buffer.put(snpbuf);
            }
            break;

    }
    // TRACK THE MODE AS THE LISTING WALKS IT. A listing that seeds M/X from the
    // CPU and then ignores the REP/SEP in front of it is right for one
    // instruction and wrong after. XCE is the one this cannot follow -- the new
    // E is whatever carry held, which a static walk does not know -- so it
    // takes the conservative reading (8-bit) and the caller's ack says the
    // widths are an assumption, not a measurement.
    if (cpu_type == PROCESSOR_65816) {
        if (opcode == 0xC2) {                       // REP #imm: clears the named bits
            if (operand & 0x20) m8 = false;
            if (operand & 0x10) x8 = false;
        } else if (opcode == 0xE2) {                // SEP #imm: sets them
            if (operand & 0x20) m8 = true;
            if (operand & 0x10) x8 = true;
        } else if (opcode == 0xFB) {                // XCE: direction unknowable here
            m8 = true; x8 = true;
        }
    }

    address += isz; // get ready for next instruction
    //bufptr[TB_EOL] = '\0';
    output_buffer.push_back(buffer.get());
}

// returns vector of disassembled instructions
std::vector<std::string> Disassembler::disassemble(int count) {
    //printf("disassembling %d instructions\n", count);
    for (int i = 0; i < count; i++) {
        disassemble_one();
    }
    std::vector<std::string> ret(std::move(output_buffer));
    output_buffer.clear();
    return ret;
}