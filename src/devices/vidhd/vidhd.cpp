
#include "computer.hpp"

#include "vidhd.hpp"


/* THIS IS A DETECTION STUB, NOT A VIDHD.
 *
 * The three ROM bytes below ($24 $EA $4C at $Cn00-02) are the signature
 * software reads to decide whether a VidHD is installed. That is all this
 * device does: no registers are decoded, no video is produced, and a program
 * that detects the card and then tries to use it -- SHR on a IIe, the extra
 * text modes, anything -- gets floating bus and no picture.
 *
 * Worth stating plainly because the failure is silent and one-directional: the
 * card ADVERTISES itself successfully, which is exactly the shape of the
 * problems found across the rest of this codebase (a positive acknowledgement
 * for capability that is not there). Verified present at $C700 in config 6, so
 * detection genuinely works -- and detection is the whole of it.
 *
 * If VidHD functionality is ever implemented, this comment is the thing to
 * delete. Until then, treat "the guest found a VidHD" as meaning only that.
 */
void init_slot_vidhd(computer_t *computer, SlotType_t slot) {
    cpu_state *cpu = computer->cpu;

    vidhd_data *vidhd_d = new vidhd_data();
    vidhd_d->computer = computer;
    vidhd_d->cpu = cpu;

    vidhd_d->rom[0] = 0x24;
    vidhd_d->rom[1] = 0xEA;
    vidhd_d->rom[2] = 0x4C; // how did the AI know this? wild.
   
    computer->mmu->set_slot_rom(slot, vidhd_d->rom, "VIDHD_ROM");
}