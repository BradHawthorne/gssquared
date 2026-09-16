#pragma once

#include <cstdint>

#include "computer.hpp"
#include "MouseController.hpp"
#include "NClock.hpp"
#include "util/EventTimer.hpp"
#include "util/InterruptController.hpp"
#include "util/ResourceFile.hpp"

struct applemouseiii_state_t : public SlotData {
    computer_t *computer = nullptr;
    NClock *clock = nullptr;
    EventTimer *event_timer = nullptr;
    InterruptController *irq_control = nullptr;
    ResourceFile *rom_file = nullptr;
    uint8_t *rom = nullptr;
    MouseController controller;
    uint64_t vbl_cycle = 0;
    bool vbl_timer_armed = false;
};

void init_applemouseiii(computer_t *computer, SlotType_t slot);

// FORK: headless injection (A2GSPU file rail), the card-level twin of the ADB
// mouse injection. dx/dy = relative motion in the card's own units (no host
// scaling), split into int8 steps; btn: 1 = left down, 0 = left up,
// -1 = unchanged. It enters the controller exactly where the SDL paths do.
// The device registers itself as MODULE_MOUSECARD so the rail can find it.
void applemouseiii_inject(applemouseiii_state_t *ds, int dx, int dy, int btn);
