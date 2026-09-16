/*
 *   Copyright (c) 2025-2026 Jawaid Bazyar
 *
 *   Uthernet II slot card (W5100). Register model adapted from AppleWin
 *   Uthernet2 (Andrea Odetti / AppleWin, GPL).
 */

#include "uthernet2.hpp"

#include "SlotData.hpp"
#include "Device_ID.hpp"
#include "mmus/mmu.hpp"
#include "util/DebugFormatter.hpp"
#include "util/DebugHandlerIDs.hpp"
#include "util/DeviceFrameDispatcher.hpp"

#include <cstdio>
#include <memory>

#if defined(GS2_UTHERNET2_STUB) || defined(__EMSCRIPTEN__)

void init_slot_uthernet2(computer_t * /*computer*/, SlotType_t /*slot*/) {
    fprintf(stderr, "Uthernet II: not available on this platform\n");
}
bool uthernet2_A2GSPU_present() { return false; }
void uthernet2_A2GSPU_snapshot(FILE * /*f*/) {}
bool uthernet2_A2GSPU_restore(FILE * /*f*/, int *lost, int *absent) { *lost = 0; *absent = 0; return true; }

#else

#include "w5100_chip.hpp"
#include "w5100.hpp"

namespace {

struct uthernet2_data : public SlotData {
    std::unique_ptr<u2::W5100Chip> chip;
};

// FORK: every Uthernet II in the machine, by slot -- for the frame tick, the debug
// view (devstate uthernet2) and the snapshot block. Cleared at shutdown.
uthernet2_data *g_u2_cards[16] = {};
computer_t *g_u2_debug_registered_for = nullptr;

uint8_t u2_read_c0xx(void *context, uint32_t addr) {
    auto *d = static_cast<uthernet2_data *>(context);
    if (!d || !d->chip) {
        return 0xFF;
    }
    return d->chip->io_read(static_cast<uint8_t>(addr & U2_C0X_MASK));
}

void u2_write_c0xx(void *context, uint32_t addr, uint8_t data) {
    auto *d = static_cast<uthernet2_data *>(context);
    if (!d || !d->chip) {
        return;
    }
    d->chip->io_write(static_cast<uint8_t>(addr & U2_C0X_MASK), data);
}

DebugFormatter *debug_uthernet2() {
    auto *df = new DebugFormatter();
    std::vector<std::string> lines;
    for (auto *d : g_u2_cards) {
        if (d && d->chip) {
            d->chip->describe(lines);
        }
    }
    if (lines.empty()) {
        lines.push_back("Uthernet II: no card");
    }
    for (auto &l : lines) {
        df->addLine(l);
    }
    return df;
}

}  // namespace

void init_slot_uthernet2(computer_t *computer, SlotType_t slot) {
    auto *d = new uthernet2_data();
    d->id = DEVICE_ID_UTHERNET2;
    d->_slot = slot;
    d->chip = std::make_unique<u2::W5100Chip>(static_cast<int>(slot));
    if (!d->chip->start()) {
        fprintf(stderr, "Uthernet II: failed to start network worker\n");
        delete d;
        return;
    }
    g_u2_cards[slot & 0x0F] = d;

    const uint16_t slot_base = static_cast<uint16_t>(0xC080 + (slot * 0x10));
    // A0/A1 only — alias all 16 bytes onto the four registers.
    for (uint16_t off = 0; off < 0x10; ++off) {
        computer->mmu->set_C0XX_read_handler(slot_base + off, {u2_read_c0xx, d});
        computer->mmu->set_C0XX_write_handler(slot_base + off, {u2_write_c0xx, d});
    }

    // FORK: host network events enter the machine here, once per video frame, and
    // nowhere else (upstream drained them inside every register access).
    const int key = slot & 0x0F;
    computer->device_frame_dispatcher->registerHandler([key]() {
        uthernet2_data *card = g_u2_cards[key];
        if (card && card->chip) {
            card->chip->frame_tick();
        }
        return true;
    });

    if (g_u2_debug_registered_for != computer) {
        g_u2_debug_registered_for = computer;
        computer->register_debug_display_handler("uthernet2", DH_UTHERNET2, []() -> DebugFormatter * {
            return debug_uthernet2();
        });
    }

    computer->register_reset_handler([d](bool cold) {
        if (d->chip) {
            d->chip->reset(cold);
        }
        return true;
    });

    computer->register_shutdown_handler([d, key]() {
        if (g_u2_cards[key] == d) {
            g_u2_cards[key] = nullptr;
        }
        if (d->chip) {
            d->chip->stop();
            d->chip.reset();
        }
        delete d;
        return true;
    });
}

bool uthernet2_A2GSPU_present() {
    for (auto *d : g_u2_cards) {
        if (d && d->chip) return true;
    }
    return false;
}

// Block payload: a count, then per card its slot and a length-prefixed chip record,
// so a machine without a card in that slot steps over it.
void uthernet2_A2GSPU_snapshot(FILE *f) {
    uint8_t cnt = 0;
    for (auto *d : g_u2_cards) if (d && d->chip) cnt++;
    fwrite(&cnt, 1, 1, f);
    for (int s = 0; s < 16; s++) {
        uthernet2_data *d = g_u2_cards[s];
        if (!d || !d->chip) continue;
        uint8_t sl = static_cast<uint8_t>(s);
        fwrite(&sl, 1, 1, f);
        long at = ftell(f);
        uint32_t len = 0;
        fwrite(&len, 4, 1, f);
        d->chip->A2GSPU_snapshot(f);
        long end = ftell(f);
        len = static_cast<uint32_t>(end - at - 4);
        fseek(f, at, SEEK_SET);
        fwrite(&len, 4, 1, f);
        fseek(f, end, SEEK_SET);
    }
}

bool uthernet2_A2GSPU_restore(FILE *f, int *lost, int *absent) {
    *lost = 0;
    *absent = 0;
    uint8_t cnt = 0;
    if (fread(&cnt, 1, 1, f) != 1) return false;
    for (int i = 0; i < cnt; i++) {
        uint8_t sl = 0;
        uint32_t len = 0;
        if (fread(&sl, 1, 1, f) != 1 || fread(&len, 4, 1, f) != 1) return false;
        const long start = ftell(f);
        uthernet2_data *d = sl < 16 ? g_u2_cards[sl] : nullptr;
        if (d && d->chip) {
            int l = 0;
            if (!d->chip->A2GSPU_restore(f, &l) || ftell(f) != start + static_cast<long>(len)) return false;
            *lost += l;
        } else {
            (*absent)++;
            if (fseek(f, start + static_cast<long>(len), SEEK_SET) != 0) return false;
        }
    }
    return true;
}

#endif
