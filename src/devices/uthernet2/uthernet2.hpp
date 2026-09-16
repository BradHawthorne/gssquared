#pragma once

#include "computer.hpp"
#include "gs2.hpp"

void init_slot_uthernet2(computer_t *computer, SlotType_t slot);

// FORK: snapshot block "u2" (M5). A host connection cannot be saved: restore
// brings a connected socket back CLOSED with DISCON raised and counts it in *lost;
// a saved card whose slot has no card here is stepped over and counted in *absent.
bool uthernet2_A2GSPU_present();
void uthernet2_A2GSPU_snapshot(FILE *f);
bool uthernet2_A2GSPU_restore(FILE *f, int *lost, int *absent);
