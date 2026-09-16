#include "gs2.hpp"
#include "cpu.hpp"
#include "debug.hpp"
#include "parallel.hpp"

#include "Device_ID.hpp"
#include "util/Connections.hpp"
#include "util/DeviceFrameDispatcher.hpp"

/*
 * FORK: THE EMULATOR NEVER WAITS FOR THE HOST, AND NOTHING IS DROPPED.
 * The card has no busy/ACK model to hold a guest off, and a guest print loop outruns any
 * host drain: the device ring once dropped 8583 bytes of a 10240-byte burst silently, and
 * the interim fix (e483676c) blocked the emulator thread on host time, up to a second a
 * byte. Now a byte goes straight into the device's ring when there is room; otherwise it
 * waits here, in order, and moves into the ring as the ring empties -- on the next write
 * and once per video frame. When the device is let go (a port change, or shutdown) what is
 * still waiting is handed to it first, with a bounded wait: that is host teardown, where
 * waiting on the host is the right thing.
 */
static void parallel_drain(parallel_data *p) {
    if (!p->serial_device) return;
    while (!p->spill.empty() && p->serial_device->q_host.try_send(p->spill.front())) p->spill.pop_front();
}

static void parallel_emit(parallel_data *p, SerialMessage msg) {
    if (!p->serial_device) return;
    parallel_drain(p);
    if (p->spill.empty() && p->serial_device->q_host.try_send(msg)) return;
    p->spill.push_back(msg);
    if (p->spill.size() > p->spill_high) p->spill_high = p->spill.size();
    if (p->spill_high >= 4096 && p->spill_high >= 2 * p->spill_reported) {
        fprintf(stderr, "Parallel %s: %zu bytes waiting for the host device (none dropped)\n", p->port_id, p->spill_high);
        p->spill_reported = p->spill_high;
    }
}

static void parallel_flush(parallel_data *p, Uint64 budget_ms) {
    if (!p->serial_device) return;
    const Uint64 deadline = SDL_GetTicks() + budget_ms;
    while (!p->spill.empty()) {
        if (p->serial_device->q_host.try_send(p->spill.front())) { p->spill.pop_front(); continue; }
        if (SDL_GetTicks() >= deadline) break;
        SDL_Delay(1);
    }
    if (!p->spill.empty()) {
        fprintf(stderr, "Parallel %s: the host device did not take the last %zu bytes within %llu ms; they are lost\n",
                p->port_id, p->spill.size(), (unsigned long long)budget_ms);
        p->spill.clear();
    }
}

void parallel_write_C0x0(void *context, uint32_t addr, uint8_t data) {
    parallel_data * parallel_d = (parallel_data *)context;
    if (DEBUG(DEBUG_PARALLEL)) {
        printf("parallel_write_C0x0 %x\n", data);
    }
    parallel_emit(parallel_d, SerialMessage{MESSAGE_DATA, data});
}

void parallel_reset(parallel_data *parallel_d) {
    if ((parallel_d != nullptr) && (parallel_d->id == DEVICE_ID_PARALLEL)) {
        parallel_emit(parallel_d, SerialMessage{MESSAGE_CLOSE, 0});
    }
}

void init_slot_parallel(computer_t *computer, SlotType_t slot) {
    parallel_data * parallel_d = new parallel_data;
    parallel_d->id = DEVICE_ID_PARALLEL;
    parallel_d->_slot = slot;

    ResourceFile *rom = new ResourceFile("roms/cards/parallel/parallel.rom", READ_ONLY);
    if (rom == nullptr) {
        fprintf(stderr, "Failed to load parallel.rom\n");
        return;
    }
    rom->load();
    parallel_d->rom = rom;

    fprintf(stdout, "init_slot_parallel %d\n", slot);

    uint16_t slot_base = 0xC080 + (slot * 0x10);

    computer->mmu->set_C0XX_write_handler(slot_base + PARALLEL_DEV, { parallel_write_C0x0, parallel_d });

    uint8_t *rom_data = parallel_d->rom->get_data();
    computer->mmu->set_slot_rom(slot, rom_data, "PARL_ROM");

    snprintf(parallel_d->port_id, sizeof(parallel_d->port_id), "PARL%d", static_cast<int>(slot));

    char display_name[32];
    snprintf(display_name, sizeof(display_name), "Parallel Slot %d", static_cast<int>(slot));
    computer->connections->register_port(
        connection_key_t{static_cast<int>(slot), "a"},
        display_name,
        connection_port_kind_t::PARALLEL,
        connection_device_type_t::FILE,
        [parallel_d](SerialDevice *dev) {
            // FORK: bytes still waiting go to the device being let go, before it goes
            // (Connections destroys it right after this call).
            if (parallel_d->serial_device && parallel_d->serial_device != dev) parallel_flush(parallel_d, 2000);
            parallel_d->serial_device = dev;
        },
        parallel_d->port_id);

    computer->register_reset_handler(
        [parallel_d](bool cold_start) {
            (void)cold_start;
            parallel_reset(parallel_d);
            return true;
        });

    if (computer->device_frame_dispatcher) {
        parallel_d->frame_handler_id = computer->device_frame_dispatcher->registerHandler([parallel_d]() {
            parallel_drain(parallel_d);
            return true;
        });
    }

    computer->register_shutdown_handler([parallel_d, computer]() {
        if (computer->device_frame_dispatcher && parallel_d->frame_handler_id != static_cast<size_t>(-1))
            computer->device_frame_dispatcher->unregisterHandler(parallel_d->frame_handler_id);
        // SerialDevice owned by Connections.
        parallel_d->serial_device = nullptr;
        delete parallel_d->rom;
        parallel_d->rom = nullptr;
        delete parallel_d;
        return true;
    });
}
