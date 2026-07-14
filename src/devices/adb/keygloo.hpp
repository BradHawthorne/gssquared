#pragma once

#include "computer.hpp"

#include "mmus/mmu_ii.hpp"
#include "devices/adb/ADB_Micro.hpp"
#include "util/InterruptController.hpp"
#include "util/ResetController.hpp"

struct keygloo_state_t {
    KeyGloo *kg = nullptr;
    MMU_II *mmu = nullptr;
    computer_t *computer = nullptr;
    InterruptController *irq_control = nullptr;
    ResetController *reset_control = nullptr;
};

void init_slot_keygloo(computer_t *computer, SlotType_t slot);

// A2GSPU: keyboard soft-switch read counts [C000,C010,C024,C025,C026].
void a2gspu_keygloo_read_counts(uint64_t out[5]);

// Asserts/clears the CPU keyboard+data IRQ from the KeyGloo's internal status —
// normally only called from the C0xx read handlers, so an INJECTED key never
// raises the IRQ until the guest reads a register. Call it on inject so an
// interrupt-driven menu (waits on the IRQ, not a $C000 poll) wakes.
void keygloo_update_interrupt_status(keygloo_state_t *kb_state, KeyGloo *kg);