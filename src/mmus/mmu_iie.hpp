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

        MMU_IIe(int page_table_size, int ram_amount, uint8_t *rom_pointer);
        virtual ~MMU_IIe();
        void set_default_C8xx_map() override;
        void compose_c1cf() override;
        void set_slot_register(uint8_t value) { reg_slot = value; compose_c1cf(); }
        uint8_t get_slot_register() { return reg_slot; }
        void map_c1cf_internal_rom(page_t page, uint8_t *data, const char *read_d);

        void init_map() override;
        void reset() override;

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
            fread(&reg_slot, 1, 1, f);
            uint8_t b_int = 0, b_s3 = 0; int8_t c8s = 0;
            fread(&b_int, 1, 1, f); f_intcxrom = (b_int != 0);
            fread(&b_s3, 1, 1, f);  f_slotc3rom = (b_s3 != 0);
            fread(&c8s, 1, 1, f);   C8xx_slot = c8s;
            fread(get_memory_base(), 1, (size_t)0x20000, f);
            uint8_t *bases[2] = { get_memory_base(), get_rom_base() };  // FRESH ROM ptr this run
            bool ok = A2GSPU_load_pages(f, bases, 2);                   // main page_table
            if (!ok) return false;               // geometry mismatch -> stop (FILE* unusable)
            A2GSPU_load_pages_arr(f, int_rom_ptable, 15, bases, 2);
            A2GSPU_load_pages_arr(f, slot_rom_ptable, 15, bases, 2);
            return true;
        }
};

void iie_mmu_handle_C00X_write(void *context, uint16_t address, uint8_t value);
