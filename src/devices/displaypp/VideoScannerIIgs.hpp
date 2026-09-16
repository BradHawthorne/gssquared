#pragma once

#include "VideoScannerII.hpp"

class VideoScannerIIgs : public VideoScannerII
{
protected:
    inline virtual bool supports_dblres() const override { return true; }
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

