#pragma once

#include "gs2.hpp"
#include "computer.hpp"

#include "serial_devices/SerialDevice.hpp"
#include "util/ResourceFile.hpp"

#include <deque>

#define PARALLEL_DEV 0x00

struct parallel_data: public SlotData {
    ResourceFile *rom = nullptr;
    SerialDevice *serial_device = nullptr;
    char port_id[16] = {};
    // FORK: what the host device has not taken yet, in order (parallel.cpp).
    std::deque<SerialMessage> spill;
    size_t spill_high = 0;
    size_t spill_reported = 0;
    size_t frame_handler_id = static_cast<size_t>(-1);
};

void init_slot_parallel(computer_t *computer, SlotType_t slot);
void parallel_reset(parallel_data *parallel_d);
