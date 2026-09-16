#pragma once

#include "VideoScannerII.hpp"

class VideoScannerIIgs : public VideoScannerII
{
protected:
    static constexpr uint8_t delay_lut_iigs[static_cast<uint8_t>(vs_mode_switch_t::COUNT)] = {
        // TEXT, MIXED, PAGE2, HIRES, STORE80, COL80, ALTCHAR, DBLRES, SHR
        1, 1, 0, 0, 0, 0, 0, 1, 1
    };

    inline virtual bool supports_dblres() const override { return true; }
    inline uint8_t mode_change_delay(vs_mode_switch_t sw) const override {
        return delay_lut_iigs[static_cast<uint8_t>(sw)];
    }
    uint8_t palette_index = 0;
public:
    VideoScannerIIgs(MMU_II *mmu);

    virtual void video_cycle() override;
    virtual void init_video_addresses() override;
    virtual void dump_cycles() ;
    // Class B snapshot (M5): the base state plus the SHR palette index being scanned.
    void A2GSPU_snapshot(FILE *f) override { VideoScannerII::A2GSPU_snapshot(f); fwrite(&palette_index, 1, 1, f); }
    bool A2GSPU_restore(FILE *f) override {
        return VideoScannerII::A2GSPU_restore(f) && fread(&palette_index, 1, 1, f) == 1;
    }
};

//void init_mb_video_scanner_iie(computer_t *computer, SlotType_t slot);

