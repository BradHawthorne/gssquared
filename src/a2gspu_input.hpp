#pragma once
// Immediate device inputs, with no hidden run loop. Host-owned edge bookkeeping
// is separate from saved guest state; reset/shutdown releases owned keys.
#include "devices/keyboard/keyboard.hpp"
#include "devices/adb/keygloo.hpp"
#include "devices/adb/ADB_Micro.hpp"
#include "devices/applemouseiii/applemouseiii.hpp"
#include "a2gspu_ctrl_json.hpp"
namespace a2input {
inline bool installed=false,key_down=false;
inline uint8_t key=0,mods=0,old_apple=0,new_mods=0;
inline void release(computer_t *computer) {
    if(!key_down)return;
    auto *kb=(keyboard_state_t*)computer->get_module_state(MODULE_KEYBOARD);
    auto *kg=(keygloo_state_t*)computer->get_module_state(MODULE_KEYGLOO);
    if(kb){if(kb->key_down_count>0)--kb->key_down_count;kb->apple_keys&=~new_mods;}
    else if(kg && kg->kg){kg->kg->hold_key(0);kg->kg->key_up();kg->kg->release_mods(new_mods);keygloo_update_interrupt_status(kg,kg->kg);}
    key_down=false;key=mods=new_mods=0;
}
inline void down(computer_t *computer,uint8_t ascii,uint8_t flags) {
    if(key_down)throw std::runtime_error("an injected key is already held; release it first");
    if(!ascii || ascii>127 || (flags&~0xc3))throw std::runtime_error("key must be 1..7F, modifiers mask C3");
    auto *kb=(keyboard_state_t*)computer->get_module_state(MODULE_KEYBOARD);
    auto *kg=(keygloo_state_t*)computer->get_module_state(MODULE_KEYGLOO);
    if(kb) {
        if(flags&3)throw std::runtime_error("IIe modifier edge supports Apple keys; supply ASCII for shift/control characters");
        if(kb->key_down_count || (kb->kb_key_strobe&0x80))throw std::runtime_error("keyboard already down or strobe unread");
        old_apple=kb->apple_keys;uint8_t requested=((flags&0x80)?1:0)|((flags&0x40)?2:0);new_mods=requested&~old_apple;
        kb->apple_keys|=requested;kb->kb_key_strobe=ascii|0x80;++kb->key_down_count;
        if(kb->mk)kb->mk->last_key_val=ascii;
    } else if(kg && kg->kg) {
        if(!kg->kg->can_inject_agent_key())throw std::runtime_error("ADB key unavailable: held key, full buffer/latch or another interceptor owns it");
        new_mods=flags&~kg->kg->live_modifiers();
        kg->kg->force_key_mods(ascii,flags);kg->kg->hold_key(ascii);keygloo_update_interrupt_status(kg,kg->kg);
    } else throw std::runtime_error("machine has no supported keyboard");
    key_down=true;key=ascii;mods=flags;
}
inline int signed_value(const std::string &text,int low,int high) {
    if(text.empty())throw std::runtime_error("missing signed decimal value");
    char *end=nullptr;errno=0;long v=std::strtol(text.c_str(),&end,10);
    if(errno || end!=text.c_str()+text.size() || v<low || v>high)throw std::runtime_error("signed decimal value out of range");return (int)v;
}
}
