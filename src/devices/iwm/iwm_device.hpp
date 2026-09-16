#pragma once

#include <cstdint>
#include <cstdio>
#include "SlotData.hpp"

class computer_t;
class IWM;

struct iwm_state_t {
    computer_t *computer;
    uint8_t status = 0;
    uint8_t control = 0;
    uint8_t data = 0;
    
    IWM *iwm;   
};

void init_iwm_slot(computer_t *computer, SlotType_t slot);

// ---- A2GSPU snapshot (M5): the controller and the drives' head state; not the
// media. `media_mismatch` counts drives whose mounted image differs from the
// snapshot's and were therefore left alone.
inline iwm_state_t *g_a2gspu_iwm = nullptr;   // the one instance, for the snapshot code
void iwm_A2GSPU_snapshot(iwm_state_t *st, FILE *f);
void iwm_A2GSPU_media_snapshot(iwm_state_t *st, FILE *f);
bool iwm_A2GSPU_media_restore(iwm_state_t *st, FILE *f, int *mismatch, int *applied);
bool iwm_A2GSPU_restore(iwm_state_t *st, FILE *f, int *media_mismatch);
