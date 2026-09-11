#pragma once

#include <string>
#include <cstdint>
#include <vector>

#include "mmus/mmu.hpp"
#include "cpus/processor_type.hpp"


class Disassembler {
    public:
        Disassembler(MMU *mmu, processor_type cputype);
        ~Disassembler();

        void setAddress(uint32_t address);
        std::vector<std::string> disassemble(int count);
        void disassemble_one();
        void setLinePrepend(int spaces) { line_prepend = spaces; }

        // 65816 IMMEDIATE WIDTH — the state this class did not have.
        //
        // On a 65816 `LDA #` is TWO operand bytes when M is clear and one when
        // it is set, and address_mode_formats fixes IMM at one. So a listing of
        // `A9 34 12 8F 00 10 7E` in native 16-bit mode read `LDA #$34` / `ORA
        // ($8F)` / `BRK` — desynchronised from the second instruction onward and
        // inventing every instruction after it, while the CPU in the same
        // session decoded it correctly as `LDA #$1234` / `STA $7E1000`.
        //
        // Defaults are 8-bit, which is the conservative reading of unknown code
        // and keeps the three existing call sites (the debugger overlay's two
        // Disassemblers and the CTRL `dis` verb) byte-identical until they opt
        // in by calling this.
        void setMX(bool m, bool x) { m8 = m; x8 = x; }
        bool mIs8() const { return m8; }
        bool xIs8() const { return x8; }

        // Bytes the MMU could not show (I/O and other handler-backed pages).
        // A listing built on the placeholder byte is fiction, so the caller
        // reports the count rather than letting it pass as a clean listing —
        // the three-outcome contract the `read` verb already keeps.
        int  unreadable() const { return n_unreadable; }
        void resetUnreadable() { n_unreadable = 0; }

    private:
        uint32_t address;
        uint16_t length;
        MMU *mmu;
        int line_prepend;
        std::vector<std::string> output_buffer;
        processor_type cpu_type;
        bool m8 = true, x8 = true;   // 65816 M/X widths assumed for the walk
        int  n_unreadable = 0;       // bytes the MMU refused to show
        bool last_read_ok = true;    // did the most recent read_mem see memory?

        uint8_t read_mem(uint32_t address);
};