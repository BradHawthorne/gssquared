#pragma once

#include "computer.hpp"
#include "Z85C30.hpp"
#include "serial_devices/SerialDevice.hpp"
#include "util/InterruptController.hpp"

struct scc8530_state_t {
    Z85C30 *scc;
    /* FILE *data_file_a = NULL;
    FILE *data_file_b = NULL; */
    SerialDevice *channel_a_device = nullptr;
    SerialDevice *channel_b_device = nullptr;

    //char *data_filename = NULL;
    InterruptController *irq_control = nullptr;
};

void init_scc8530_slot(computer_t *computer, SlotType_t slot);

// A2GSPU snapshot (M5): the IIgs SCC, block "scc".
bool scc_A2GSPU_present();
void scc_A2GSPU_snapshot(FILE *f);
bool scc_A2GSPU_restore(FILE *f);
