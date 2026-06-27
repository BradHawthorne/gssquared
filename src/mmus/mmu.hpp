#pragma once

#include <cstdint>
#include <cstdio>
#include <assert.h>

#include "util/DebugFormatter.hpp"
#include "memoryspecs.hpp"      // not used here but used by lots of stuff that includes this.

#define C0X0_BASE 0xC000
#define C0X0_SIZE 0x100

enum memory_type_t {
    M_NON,
    M_RAM,
    M_ROM,
    M_IO,
};

typedef uint8_t *page_ref;
typedef uint32_t page_t;

// Function pointer type for memory bus handlers
typedef uint8_t (*memory_read_func)(void *context, uint32_t address);
typedef void (*memory_write_func)(void *context, uint32_t address, uint8_t value);

struct read_handler_t {
    memory_read_func read;
    void *context;
};

struct read_handler_pair_t {
    read_handler_t hs[2];
};

struct write_handler_t {
    memory_write_func write;
    void *context;
};

struct write_handler_pair_t {
    write_handler_t hs[2];
};

struct page_table_entry_t {
    page_ref read_p; // pointer to uint8_t pointers
    page_ref write_p;
    read_handler_t read_h;
    write_handler_t write_h;
    write_handler_t shadow_h;
    const char *read_d;
    const char *write_d;
};

class MMU {
    protected:
        //cpu_state *cpu;
        int num_pages = 0;
        // this is an array of info about each page.
        page_table_entry_t *page_table;
        uint8_t floating_bus_val = 0xEE;
        uint32_t page_size = 0;
        uint32_t page_size_bits = 0;
        uint32_t page_size_mask = 0;

        /* static constexpr uint32_t PAGE_SIZE_BITS = __builtin_ctz(PAGE_SIZE);
        static constexpr uint32_t PAGE_MASK = PAGE_SIZE - 1; */
            
/**
 * MMU provides the memory management interface for the CPU.
 * 
 * Any memory space access here is "raw". It does not trigger cycles in the CPU.
 * 
 * The base implementation in MMU supports pages of type RAM, ROM, and IO. IO calls the memory_bus_read and memory_bus_write methods.
 * This is relatively generic. 
 */
    public:

        MMU(page_t num_pages, uint32_t page_size) {
            this->num_pages = num_pages;
            this->page_size = page_size;
            this->page_size_bits = __builtin_ctz(page_size);
            this->page_size_mask = page_size - 1;
            
            page_table = new page_table_entry_t[num_pages];
            for (int i = 0 ; i < num_pages ; i++) {
                //page_table[i].readable = 0;
                //page_table[i].writeable = 0;
                //page_table[i].type_r = M_NON;
                //page_table[i].type_w = M_NON;
                page_table[i].read_p = nullptr;
                page_table[i].write_p = nullptr;
                page_table[i].read_h = {nullptr, nullptr};
                page_table[i].write_h = {nullptr, nullptr};
                page_table[i].shadow_h = {nullptr, nullptr};
            }
        }

        virtual ~MMU() {
            delete[] page_table;
        }

        // Raw. Do not trigger cycles or do the IO bus stuff
        uint8_t read_raw(uint32_t address) {
            uint16_t page = address >> page_size_bits; // / GS2_PAGE_SIZE;
            uint16_t offset = address & page_size_mask; // % GS2_PAGE_SIZE;
            if (page > num_pages) return floating_bus_read();
            page_table_entry_t *pte = &page_table[page];
            if (pte->read_p == nullptr) return floating_bus_read();
            return pte->read_p[offset];
        }

        // Observation-free peek (default = read_raw; MMU_IIgs overrides to route
        // $E0/$E1 through the Mega II image). Never triggers IO/cycles/slot bus.
        virtual uint8_t probe_peek(uint32_t address) { return read_raw(address); }

        // no writable check here, do it higher up - this needs to be able to write to
        // memory block no matter what.
        void write_raw(uint32_t address, uint8_t value) {
            uint16_t page = address >> page_size_bits; // / GS2_PAGE_SIZE;
            uint16_t offset = address & page_size_mask; // % GS2_PAGE_SIZE;
            if (page > num_pages) return;
            page_table_entry_t *pte = &page_table[page];
            if (pte->read_p == nullptr) return;
            pte->write_p[offset] = value;
        }

        /* void write_raw_word(uint32_t address, uint16_t value) {
            write_raw(address, value & 0xFF);
            write_raw(address + 1, value >> 8);
        } */


        inline virtual uint8_t read(uint32_t address) {
            uint16_t page = address >> page_size_bits; // / GS2_PAGE_SIZE;
            uint16_t offset = address & page_size_mask; // % GS2_PAGE_SIZE;
            assert(page < num_pages);

            page_table_entry_t *pte = &page_table[page];

            if (pte->read_p != nullptr) return pte->read_p[offset];
            else if (pte->read_h.read != nullptr) return pte->read_h.read(pte->read_h.context, address);
            else return floating_bus_read();
        }

        /**
         * write 
         * Perform bus write which includes potential I/O and slot-card handlers etc.
         * This is the only interface to the CPU.
         * 
         * If a page has a write_h, it's "IO" and we call that handler.
         * If a page has a write_p, it is "RAM" and can be written to.
         * If a page has no write_p, it is "ROM" and cannot be written to.
         * If a page has a shadow_h, it is "shadowed memory" and we further call the shadow handler.
         *  */
        inline virtual void write(uint32_t address, uint8_t value) {
            uint16_t page = address >> page_size_bits; // / GS2_PAGE_SIZE;
            uint16_t offset = address & page_size_mask; // % GS2_PAGE_SIZE;

            assert(page < num_pages);
            page_table_entry_t *pte = &page_table[page];
            
            // if there is a write handler, call it instead of writing directly.
            if (pte->write_h.write != nullptr) pte->write_h.write(pte->write_h.context, address, value);
            else if (pte->write_p) pte->write_p[offset] = value;

            if (pte->shadow_h.write != nullptr) pte->shadow_h.write(pte->shadow_h.context, address, value);

            // if none of those things were set, silently do nothing.
        }

        // By default, this is the same as read.
        inline virtual uint8_t vp_read(uint32_t address) {
            return read(address);
        }

        void set_floating_bus(uint8_t val) { floating_bus_val = val; }
    
        uint8_t floating_bus_read() { return floating_bus_val; }
    

        uint8_t *get_page_base_address(page_t page) {
            return page_table[page].read_p;
        }

        void map_page_both(page_t page, uint8_t *data, const char *read_d) {
            if (page > num_pages) {
                return;
            }
            page_table_entry_t *pte = &page_table[page];

            pte->read_p = data;
            pte->write_p = data;
            pte->read_h = {nullptr, nullptr};
            pte->write_h = {nullptr, nullptr};
            pte->read_d = read_d;
            pte->write_d = read_d;
        }

        // map page to read only
        void map_page_read_only(page_t page, uint8_t *data, const char *read_d) {
            if (page > num_pages) {
                return;
            }
            page_table_entry_t *pte = &page_table[page];

            pte->read_p = data;
            pte->write_p = nullptr;
            pte->read_d = read_d;
            pte->write_d = nullptr;
        }

        void map_page_read(page_t page, uint8_t *data, const char *read_d) {
            if (page > num_pages) {
                return;
            }
            page_table_entry_t *pte = &page_table[page];
            pte->read_p = data;
            pte->read_d = read_d;
        }

        void map_page_write(page_t page, uint8_t *data, const char *write_d) {
            if (page > num_pages) {
                return;
            }
            page_table_entry_t *pte = &page_table[page];
            
            pte->write_p = data;
            pte->write_d = write_d;
        }

        void set_page_shadow(page_t page, write_handler_t handler) {
            page_table[page].shadow_h = handler;
        }

        void set_page_read_h(page_t page, read_handler_t handler, const char *read_d) {
            page_table[page].read_h = handler;
            page_table[page].read_d = read_d;
        }

        void set_page_write_h(page_t page, write_handler_t handler, const char *write_d) {
            page_table[page].write_h = handler;
            page_table[page].write_d = write_d;
        }

        void dump_page_table(page_t start_page, page_t end_page) {

            printf("Page                        R-Ptr            W-Ptr              read_h   (    context     )        write_h  (     context    )        S-Handler(     context    )\n");
            printf("-------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
            for (int i = start_page ; i <= end_page ; i++) {
                printf("%02X (%8s %8s): %16p %16p %16p(%16p) %16p(%16p) %16p(%16p)\n", 
                    i, 
                    page_table[i].read_d, page_table[i].write_d, //page_table[i].readable, page_table[i].writeable,
                    page_table[i].read_p,
                    page_table[i].write_p, 
                    page_table[i].read_h.read, page_table[i].read_h.context,
                    page_table[i].write_h.write, page_table[i].write_h.context,
                    page_table[i].shadow_h.write, page_table[i].shadow_h.context
                );
            }
        }

        void debug_output_page(DebugFormatter *f, page_t page, bool header = false) {
            if (header) {
                f->addLine("Page                        R-Ptr            W-Ptr              read_h   (    context     )        write_h  (     context    )        S-Handler(     context    )\n");
                f->addLine("-------------------------------------------------------------------------------------------------------------------------------------------------------------\n");
            }
            f->addLine("%02X (%8s %8s): %16p %16p %16p(%16p) %16p(%16p) %16p(%16p)\n", 
                page, 
                page_table[page].read_d, page_table[page].write_d, //page_table[i].readable, page_table[i].writeable,
                page_table[page].read_p,
                page_table[page].write_p, 
                page_table[page].read_h.read, page_table[page].read_h.context,
                page_table[page].write_h.write, page_table[page].write_h.context,
                page_table[page].shadow_h.write, page_table[page].shadow_h.context
            );
        }

        void dump_page_table() {
            dump_page_table(0, num_pages - 1);
        }

        void dump_page(page_t page) {
            printf("Page %02X:\n", page);
            for (int i = 0; i < page_size; i++) {
                printf("%02X ", read((page << 8) | i) );
                if (i % 16 == 15) printf("\n"); // 16 bytes per line
            }
            printf("\n");
        }

        virtual void reset() {
            // do nothing.
        }

        const char *get_read_d(page_t page) {
            return page_table[page].read_d;
        }

        const char *get_write_d(page_t page) {
            return page_table[page].write_d;
        }

        void get_page_table_entry(page_t page, page_table_entry_t *pte) {
            *pte = page_table[page];
        }

        void set_page_table_entry(page_t page, page_table_entry_t *pte) {
            page_table[page] = *pte;
        }

        // ---- A2GSPU warm-boot snapshot: relocatable page-table (de)serialize ----
        // Serialize read_p/write_p of `n` entries as (region_id, offset) against the
        // caller's bases[]/sizes[]. rg=-1 nullptr, rg=-2 non-null/not-in-any-base (left
        // on load), rg>=0 -> bases[rg]+off. Handlers/descriptors are construction-fixed
        // and NOT saved (re-installed by this run's init_map ctor). These live in the base
        // class because page_table/num_pages are protected and reused by both MMUs.
        void A2GSPU_save_pages_arr(FILE *f, page_table_entry_t *pt, int n,
                                   uint8_t *const *bases, const size_t *sizes, int nb) {
            for (int i = 0; i < n; i++) {
                for (int w = 0; w < 2; w++) {
                    uint8_t *p = w ? pt[i].write_p : pt[i].read_p;
                    int8_t rg = -1; uint32_t off = 0;       // -1 = nullptr
                    if (p) {
                        rg = -2;                            // -2 = non-null/unknown
                        for (int b = 0; b < nb; b++) {
                            if (p >= bases[b] && p < bases[b] + sizes[b]) {
                                rg = (int8_t)b; off = (uint32_t)(p - bases[b]); break;
                            }
                        }
                    }
                    fwrite(&rg, 1, 1, f); fwrite(&off, 4, 1, f);
                }
            }
        }
        // Returns false on a truncated read (so a partial snapshot fails loud instead of
        // restoring half-initialized page pointers and reporting success).
        bool A2GSPU_load_pages_arr(FILE *f, page_table_entry_t *pt, int n,
                                   uint8_t *const *bases, int nb) {
            for (int i = 0; i < n; i++) {
                for (int w = 0; w < 2; w++) {
                    int8_t rg; uint32_t off;
                    if (fread(&rg, 1, 1, f) != 1 || fread(&off, 4, 1, f) != 1) return false;
                    uint8_t **d = w ? &pt[i].write_p : &pt[i].read_p;
                    if (rg == -1) *d = nullptr;
                    else if (rg >= 0 && rg < nb) *d = bases[rg] + off;
                    // rg == -2: leave the construction-time pointer in place
                }
            }
            return true;
        }
        // Main page_table convenience wrappers (prefix the entry count for validation).
        void A2GSPU_save_pages(FILE *f, uint8_t *const *bases, const size_t *sizes, int nb) {
            uint32_t np = (uint32_t)num_pages; fwrite(&np, sizeof(np), 1, f);
            A2GSPU_save_pages_arr(f, page_table, num_pages, bases, sizes, nb);
        }
        // Returns false on a page-geometry mismatch OR a truncated read. On a geometry
        // mismatch the per-entry bytes ARE consumed (fseek past them) so the FILE* stays
        // aligned for the caller's bail logic.
        bool A2GSPU_load_pages(FILE *f, uint8_t *const *bases, int nb) {
            uint32_t np = 0;
            if (fread(&np, sizeof(np), 1, f) != 1) return false;
            if (np != (uint32_t)num_pages) {           // page geometry mismatch -> skip the block, bail
                fseek(f, (long)np * 2 * 5, SEEK_CUR);  // each entry = 1-byte rg + 4-byte off, x2 (read/write)
                return false;
            }
            return A2GSPU_load_pages_arr(f, page_table, num_pages, bases, nb);
        }

};
