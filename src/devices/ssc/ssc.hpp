#pragma once

#include "computer.hpp"
#include "gs2.hpp"

void init_slot_ssc(computer_t *computer, SlotType_t slot);

// FORK: snapshot block "ssc" (M5), every Super Serial Card by slot.
bool ssc_A2GSPU_present();
void ssc_A2GSPU_snapshot(FILE *f);
bool ssc_A2GSPU_restore(FILE *f, int *absent);
