#pragma once

#include "computer.hpp"
#include "SlotData.hpp"
#include "RTC.hpp"


struct rtc_pram_state_t {
    RTC *rtc = nullptr;

    /* write_handler_t display_wr_handler = { nullptr, nullptr };
    read_handler_t display_rd_handler = { nullptr, nullptr }; */

};

void init_slot_rtc_pram(computer_t *computer, SlotType_t slot);

// ---- A2GSPU snapshot: the IIgs clock chip (block "rtc").
bool rtc_A2GSPU_present();
void rtc_A2GSPU_snapshot(FILE *f);
bool rtc_A2GSPU_restore(FILE *f);
