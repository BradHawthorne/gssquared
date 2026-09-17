#pragma once

#include "mmu_ii.hpp"

#include "mbus/MessageBus.hpp"

class MMU_IIe : public MMU_II {
    private:
        MessageBus *mbus;

        virtual void power_on_randomize(uint8_t *ram, int ram_size) override;
    protected:
        uint8_t reg_slot = 0b11110110; // slots 7-4,2-1 set to 1 here.
        page_table_entry_t int_rom_ptable[15];

    public:
        bool f_intcxrom = false;
        bool f_slotc3rom = false;
        // FORK: set by MMU_IIgs. The IIgs slot register $C02D gates slots 1, 2, 5, 6 and 7:
        // bit n selects the card's $Cn00 ROM AND its $C0nX I/O, clear selects the built-in
        // port [HW Ref Table 8-2]. Slots 3 and 4 always give their I/O to the card. Apple's
        // own ROM relies on it: before calling the built-in slot 5 firmware it saves $C02D,
        // clears bits 5 and 6 so the IWM at $C0E0-$C0EF answers, and restores it after
        // (ROM 01 $FE7026 and $FE6F63).
        bool gs_slot_register = false;

        MMU_IIe(int page_table_size, int ram_amount, uint8_t *rom_pointer);
        virtual ~MMU_IIe();
        void set_default_C8xx_map() override;
        void compose_c1cf() override;
        void set_slot_register(uint8_t value) { reg_slot = value; compose_c1cf(); }
        uint8_t get_slot_register() { return reg_slot; }

        int c0xx_gate(uint8_t sub) override {
            if (!gs_slot_register || sub < 0x90) return -1;
            const uint8_t slot = (uint8_t)((sub >> 4) - 8);
            if (slot == 3 || slot == 4) return -1;          // always the card's
            return (reg_slot >> slot) & 1;
        }
        // The same rule compose_c1cf uses to map the page: INTCXROM off, and SLOTC3ROM for
        // slot 3, the slot register bit for the others on a IIgs (a IIe has no register).
        bool slot_card_selected(uint8_t slot) override {
            if (f_intcxrom) return false;
            if (slot == 3) return f_slotc3rom;
            return gs_slot_register ? ((reg_slot >> slot) & 1) != 0 : true;
        }
        // "The C800-CFFF range is assigned to motherboard ROM any time SlotC3 ROM is reset
        // and an access is made to $C3XX" [Sather, UtA2E 5-28], whatever INTCXROM says. The
        // 80-column firmware (the IIgs Text Tool Set's Pascal device in slot 3 included)
        // runs on into $C800. 575e1703 tied this to slot_card_selected(3), which is false
        // exactly then, and the IIgs Finder boot fell into an unmapped $C800 in InitTextDev.
        bool motherboard_c8_access(uint8_t slot) override { return slot == 3 && !f_slotc3rom; }
        void map_motherboard_c8() override;
        void map_c1cf_internal_rom(page_t page, uint8_t *data, const char *read_d);

        void init_map() override;
        void reset(bool cold_start = false) override;

        // ===== A2GSPU warm-boot snapshot (the embedded Mega-II) =====
        void A2GSPU_snapshot(FILE *f) {
            fwrite(&reg_slot, 1, 1, f);
            uint8_t b_int = f_intcxrom ? 1 : 0; fwrite(&b_int, 1, 1, f);   // MMU_IIe::f_intcxrom (the live one)
            uint8_t b_s3  = f_slotc3rom ? 1 : 0; fwrite(&b_s3, 1, 1, f);
            int8_t  c8s = C8xx_slot;             fwrite(&c8s, 1, 1, f);
            // full 128KB main_ram (//e main 0..FFFF + aux 10000..1FFFF; IIgs $E0/$E1 live above 48KB)
            fwrite(get_memory_base(), 1, (size_t)0x20000, f);
            // bases: [RAM 128KB, ROM 16KB window = main_rom_D0..+0x4000). Tight: the hottest
            // megaii ROM pointers are bsr_map_memory's E0-FF case rom+0x2000+31*0x100 = rom+0x3F00
            // (top byte rom+0x3FFF) < 0x4000; D0-DF rom+0x1000; int_rom_ptable +i*0x100; slot3 +0x800.
            uint8_t *bases[2] = { get_memory_base(), get_rom_base() };
            size_t   sizes[2] = { (size_t)0x20000, (size_t)0x4000 };
            A2GSPU_save_pages(f, bases, sizes, 2);                 // main page_table[256]
            // aux ptables so a post-restore INTCXROM/SLOTC3ROM toggle recomposes correctly
            A2GSPU_save_pages_arr(f, int_rom_ptable, 15, bases, sizes, 2);
            A2GSPU_save_pages_arr(f, slot_rom_ptable, 15, bases, sizes, 2);
        }

        bool A2GSPU_restore(FILE *f) {
            uint8_t b_int = 0, b_s3 = 0; int8_t c8s = 0;
            if (fread(&reg_slot, 1, 1, f) != 1) return false;
            if (fread(&b_int, 1, 1, f) != 1) return false; f_intcxrom = (b_int != 0);
            if (fread(&b_s3, 1, 1, f) != 1) return false;  f_slotc3rom = (b_s3 != 0);
            if (fread(&c8s, 1, 1, f) != 1) return false;   C8xx_slot = c8s;
            c8_flipflops = (c8s >= 1 && c8s <= 7) ? (uint8_t)(1u << c8s) : 0;   // FORK: the owner's flip-flop
            if (fread(get_memory_base(), 1, (size_t)0x20000, f) != (size_t)0x20000) return false;
            uint8_t *bases[2] = { get_memory_base(), get_rom_base() };  // FRESH ROM ptr this run
            bool ok = A2GSPU_load_pages(f, bases, 2);                   // main page_table
            if (!ok) return false;               // geometry mismatch / truncation -> stop (FILE* unusable)
            if (!A2GSPU_load_pages_arr(f, int_rom_ptable, 15, bases, 2)) return false;
            if (!A2GSPU_load_pages_arr(f, slot_rom_ptable, 15, bases, 2)) return false;
            return true;
        }
};

void iie_mmu_handle_C00X_write(void *context, uint16_t address, uint8_t value);
