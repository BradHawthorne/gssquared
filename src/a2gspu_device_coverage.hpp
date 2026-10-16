#pragma once
#include "Device_ID.hpp"
// Coverage declarations reference existing interfaces. The actual saved-state
// block inventory is authoritative; empty strings are explicit missing coverage.
struct A2DeviceCoverage {const char *observers,*snapshot,*controls;};
inline A2DeviceCoverage a2_device_coverage(device_id id) {
    switch(id) {
        case DEVICE_ID_KEYBOARD_IIPLUS:case DEVICE_ID_KEYBOARD_IIE:return {"keyboard","","input,press,key,keys"};
        case DEVICE_ID_SPEAKER:return {"speaker","","guest-mmio"};
        case DEVICE_ID_DISPLAY:return {"display","vgcint,scanner","guest-mmio,png,pngc,shr"};
        case DEVICE_ID_GAMECONTROLLER:return {"game","","paddle,pbutton,joymode"};
        case DEVICE_ID_LANGUAGE_CARD:return {"","mmu","guest-mmio"};
        case DEVICE_ID_PRODOS_BLOCK:case DEVICE_ID_PD_BLOCK2:return {"","","mount,guest-mmio"};
        case DEVICE_ID_PD_BLOCK3:return {"","pdblock","mount,guest-mmio"};
        case DEVICE_ID_PRODOS_CLOCK:return {"","","launch-pins,guest-mmio"};
        case DEVICE_ID_DISK_II:return {"diskii","diskii,media,motor","mount,guest-mmio"};
        case DEVICE_ID_MEM_EXPANSION:return {"","","guest-mmio"};
        case DEVICE_ID_THUNDER_CLOCK:return {"thunderclock","tclock","guest-mmio"};
        case DEVICE_ID_PARALLEL:return {"","","guest-mmio,launch-output-binding"};
        case DEVICE_ID_VIDEX:return {"","","guest-mmio"};
        case DEVICE_ID_MOCKINGBOARD:return {"mockingboard","mb","guest-mmio"};
        case DEVICE_ID_IIE_MEMORY:return {"iiememory","mmu","guest-mmio"};
        case DEVICE_ID_MOUSE:return {"mouse","mousecd","input mouse card,STATE_SET"};
        case DEVICE_ID_CASSETTE:return {"","","guest-mmio"};
        case DEVICE_ID_VIDHD:return {"","","guest-mmio"};
        case DEVICE_ID_RTC_PRAM:return {"rtc","rtc","guest-mmio,launch-pins"};
        case DEVICE_ID_KEYGLOO:return {"adb","adb","input,press,key,holdkey"};
        case DEVICE_ID_ENSONIQ:return {"es5503","ensoniq","guest-mmio,STATE_SET,audio capture"};
        case DEVICE_ID_SCC8530:return {"scc8530","scc","guest-mmio,capture,rail set SCC faults"};
        case DEVICE_ID_IWM:return {"iwm","iwm,media,motor","guest-mmio,mount"};
        case DEVICE_ID_SECOND_SIGHT:return {"ss","ssight","guest-mmio"};
        case DEVICE_ID_HOST_FST:return {"","","guest-WDM-FF,launch-root-binding"};
        case DEVICE_ID_UTHERNET2:return {"uthernet2","u2","guest-mmio,rail set U2 faults"};
        case DEVICE_ID_SUPER_SERIAL:return {"ssc","ssc","guest-mmio,rail set SSC faults"};
        case DEVICE_ID_VOC:return {"voc","voc","guest-mmio"};
        case DEVICE_ID_A2GSPU:return {"A2GSPU","a2gspu","guest-mmio,launch-mode-binding"};
        default:return {"","","undeclared"};
    }
}
