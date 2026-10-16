#pragma once
// Connected machine observations. All entry points run on the machine thread.
// A context stamp denotes one observation boundary, not a saved replay state.
#include "a2gspu_inspect.hpp"
#include "util/EventTimer.hpp"
#include <deque>
#include <iomanip>
#include <chrono>

namespace a2context {
using a2ctrljson::quote;
inline computer_t *owner=nullptr;
inline uint64_t observation=0,event_sequence=0,discarded=0,oversized=0,mark_sequence=0;
inline size_t capacity=256;
inline bool active=false;
struct Event {uint64_t id,parent;std::string kind,payload,stamp;};
inline std::deque<Event> history;
using Fields=std::map<std::string,std::string>;
struct Mark {uint64_t id;uint32_t address,length;Fields fields;std::string stamp;};
inline std::deque<Mark> marks;
inline Fields sampled;
inline std::string session_token;
inline uint64_t clock_epoch=0,last_clock=0;

inline std::string stamp(computer_t *c) {
    auto *clock=c->clock;
    if(clock->get_cycles()<last_clock)++clock_epoch;last_clock=clock->get_cycles();
    return "{\"context_session\":"+quote(session_token)+",\"clock_epoch\":"+quote(std::to_string(clock_epoch))+",\"observation_id\":"+quote(std::to_string(++observation))+
        ",\"cpu_cycles\":"+quote(std::to_string(clock->get_cycles()))+
        ",\"master_14m_ticks\":"+quote(std::to_string(clock->get_c14m()))+
        ",\"video_cycles\":"+quote(std::to_string(clock->get_vid_cycles()))+
        ",\"video_frames\":"+quote(std::to_string(clock->get_frame_count()))+
        ",\"clock_model\":\"native NClock domains; compare cycles only within a continuous timeline\","
        "\"execution_mode\":"+std::to_string(c->execution_mode)+",\"boundary\":\"machine-thread observation; no guest execution during capture\"}";
}
inline void attach(computer_t *c) {
    if(owner==c)return;
    if(owner)throw std::runtime_error("context belongs to another machine");
    owner=c;observation=event_sequence=discarded=oversized=mark_sequence=0;history.clear();marks.clear();sampled.clear();active=false;
    clock_epoch=0;last_clock=c->clock->get_cycles();
    session_token=std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    c->register_shutdown_handler([](){a2events::set_context({});owner=nullptr;active=false;history.clear();marks.clear();sampled.clear();return true;});
}
inline uint64_t retain(const char *kind,const std::string &payload) {
    if(!active || !owner)return 0;
    auto value=payload;
    // An oversized payload becomes an explicit hole, never a cut JSON value.
    if(value.size()>4096){++oversized;value="{\"omitted\":true,\"reason\":\"context-payload-cap\",\"original_bytes\":"+std::to_string(value.size())+"}";}
    if(history.size()==capacity){history.pop_front();++discarded;}
    const auto id=++event_sequence;
    history.push_back({id,a2events::cause_id,kind,value,stamp(owner)});
    return id;
}
inline std::string history_status() {
    return "{\"active\":"+std::string(active?"true":"false")+",\"capacity\":"+std::to_string(capacity)+
        ",\"retained\":"+std::to_string(history.size())+",\"last_event_id\":"+quote(std::to_string(event_sequence))+
        ",\"discarded\":"+quote(std::to_string(discarded))+",\"oversized\":"+quote(std::to_string(oversized))+
        ",\"coverage\":\"armed interval only; command boundaries, central records, IRQ edges, interrupt entry, EventTimer transitions and sampled native device fields\","
        "\"complete_machine_history\":false}";
}
inline std::string stop(computer_t *c) {
    std::string last="null";for(auto i=history.rbegin();i!=history.rend();++i) {
        if(i->kind=="command" && i->payload.find("\"schema\":\"a2execution-v1\"")!=std::string::npos) {
            last="{\"event_id\":"+quote(std::to_string(i->id))+",\"receipt\":"+i->payload+"}";break;
        }
    }
    return "{\"cpu\":"+a2inspect::cpu(c)+",\"execution_mode\":"+std::to_string(c->execution_mode)+",\"last_retained_execution\":"+last+
        ",\"explanation\":\"Current halt/STP/WAI flags and the latest retained execution receipt describe different boundaries. Inspect completed/stop_reason/target_reached in that receipt. Missing history cannot establish why an earlier stop happened.\","
        "\"source\":[\"src/a2gspu_ctrl_cmds.cpp\",\"src/cpus/base_6502.cpp\"],\"evidence_class\":\"current-state-plus-recorded-execution-receipt\"}";
}
inline std::string pixel(computer_t *c,unsigned x,unsigned y) {
    auto *m=rail_mmu(c);std::ostringstream out;
    if(auto *gs=dynamic_cast<MMU_IIgs*>(m)) {
        if(y>=200 || x>=640)throw std::runtime_error("SHR reference coordinate out of range");
        auto *raw=gs->get_megaii_memory_base();if(!raw)throw std::runtime_error("video RAM unavailable");
        const auto physical=[](unsigned linear){return 0x10000u|iigs_aux_linear_to_phys((uint16_t)linear);};
        const unsigned scb_address=physical(0x9d00+y);
        const uint8_t scb=raw[scb_address];bool mode640=scb&0x80;
        if(!mode640 && x>=320)throw std::runtime_error("320-mode source line requires x < 320");
        const unsigned address=physical(0x2000+y*160+(mode640?x/4:x/2));const uint8_t packed=raw[address];
        static const unsigned offsets[]={8,12,0,4};
        unsigned index=mode640?offsets[x%4]+((packed>>(6-2*(x%4)))&3):((x&1)?packed&15:packed>>4);
        const bool supported=mode640 || !(scb&0x20);unsigned palette=0x9e00+(scb&15)*32+index*2;
        const uint16_t color=raw[physical(palette)]|(raw[physical(palette+1)]<<8);
        out<<"{\"model\":\"SHR source-pixel decode from physical Mega-II RAM; not actual GPU output\",\"x\":"<<x<<",\"y\":"<<y
           <<",\"shr_enabled\":"<<((gs->new_video_register()&128)?"true":"false")<<",\"line_width\":"<<(mode640?640:320)<<",\"scb_offset\":"<<scb_address<<",\"scb\":"<<(unsigned)scb
           <<",\"memory_domain\":\"megaii_raw\",\"pixel_byte_offset\":"<<address<<",\"pixel_byte\":"<<(unsigned)packed<<",\"palette_index\":"<<(supported?std::to_string(index):"null")
           <<",\"palette_byte_offsets\":"<<(supported?"["+std::to_string(physical(palette))+","+std::to_string(physical(palette+1))+"]":"null")<<",\"rgb\":";
        if(supported)out<<'['<<((color>>8)&15)*17<<','<<((color>>4)&15)*17<<','<<(color&15)*17<<']';else out<<"null";
        out<<",\"gap\":"<<(supported?"null":quote("320 fill-mode generator history is not reconstructed by this query"))
           <<",\"source\":[\"src/devices/displaypp/VideoScanGenerator.cpp\",\"src/mmus/mmu_iigs.hpp\"],\"evidence_class\":\"physical-bytes-and-reference-decode\"}";
    } else {
        if(y>=192 || x>=280)throw std::runtime_error("HGR reference coordinates require x < 280, y < 192");
        auto *scanner=c->clock->get_video_scanner();if(!scanner)throw std::runtime_error("current-machine video scanner unavailable");
        const int flags=(scanner->is_text()?1:0)|(scanner->is_mixed()?2:0)|(scanner->is_page_2()?4:0)|(scanner->is_hires()?8:0);
        unsigned page=(flags&4)?2:1;
        const unsigned a=page*0x2000+(y&7)*0x400+((y>>3)&7)*0x80+(y>>6)*40+x/7;
        auto *memory=rail_video_base(c);if(!memory)throw std::runtime_error("video RAM unavailable");const unsigned byte=memory[a];
        out<<"{\"model\":\"HGR main-memory monochrome source dot; not DHGR/composite/CRT output\",\"x\":"<<x<<",\"y\":"<<y<<",\"page\":"<<page<<",\"video_flags\":"<<flags
           <<",\"memory_domain\":\"megaii_raw\",\"pixel_byte_offset\":"<<a<<",\"pixel_byte\":"<<byte<<",\"bit\":"<<x%7<<",\"dot\":"<<((byte>>(x%7))&1)
           <<",\"source\":[\"src/devices/displaypp/VideoScanGenerator.cpp\"],\"evidence_class\":\"physical-byte-and-monochrome-reference-decode\",\"gap\":\"text/lores/DHGR/composite/GPU paths require their named observers\"}";
    }
    return out.str();
}
inline std::string irq(computer_t *c) {
    static const char *names[]={"slot0","slot1","slot2","slot3","slot4","slot5","slot6","slot7","soundglu","keygloo","adb-data","vgc","scc","megaii","agent"};
    const uint64_t mask=c->irq_control?*c->irq_control->obs_pending_ptr():0;
    std::ostringstream out;out<<"{\"available\":"<<(c->irq_control?"true":"false")<<",\"pending_mask\":"<<(c->irq_control?quote(std::to_string(mask)):"null")
        <<",\"cpu_line\":"<<(c->cpu->irq_asserted?"true":"false")<<",\"cpu_i_flag\":"<<(c->cpu->I?"true":"false")
        <<",\"sample_pipeline\":"<<(unsigned)c->cpu->irq_pipe<<",\"nmi_pending\":"<<(c->cpu->nmi_pending?"true":"false")
        <<",\"boundary_abort_pending\":"<<(c->cpu->boundary_abort_pending?"true":"false")<<",\"sources\":[";
    for(size_t i=0;i<sizeof names/sizeof *names;i++){if(i)out<<',';out<<"{\"id\":"<<i<<",\"name\":"<<quote(names[i])<<",\"asserted\":"<<(mask&(1ULL<<i)?"true":"false")<<'}';}
    out<<"],\"explanation\":\"Controller combines source bits into the CPU IRQ line. CPU service tests the sampled IRQ/I pipeline; an asserted source alone does not prove handler entry. Multiple sources may be pending; no unique source is inferred.\","
        "\"source\":[\"src/util/InterruptController.hpp\",\"src/cpus/base_6502.cpp\",\"src/device_irq_id.hpp\"],\"evidence_class\":\"current-model-state-and-service-rule\"}";
    return out.str();
}
inline std::string clocks(computer_t *c) {
    auto *k=c->clock;std::ostringstream out;
    out<<"{\"mode\":"<<quote(k->get_clock_mode_name())<<",\"mode_id\":"<<(int)k->get_clock_mode()
       <<",\"nominal_hz\":"<<quote(std::to_string(k->get_hz_rate()))<<",\"cpu_per_14m\":"<<k->get_cpu_per_14m()
       <<",\"frame_start_14m\":"<<quote(std::to_string(k->get_frame_start_c14M()))<<",\"frame_end_14m\":"<<quote(std::to_string(k->get_frame_end_c14M()))
       <<",\"video_phase_14m\":"<<quote(std::to_string(k->get_video_cycle_14M_count()))<<",\"scanline_phase_14m\":"<<quote(std::to_string(k->get_scanline_14M_count()))
       <<",\"source\":\"src/NClock.hpp\",\"model\":\"native counters; no fixed CPU-speed conversion or host refresh inference\"}";
    return out.str();
}
inline std::string schedule(computer_t *c,size_t offset=0,bool full=false) {
    EventTimer *timers[]={c->event_timer,c->vid_event_timer,c->cpu_event_timer};
    const char *domains[]={"master-14m","video","cpu"};
    uint64_t now[]={c->clock->get_c14m(),c->clock->get_vid_cycles(),c->clock->get_cycles()};
    std::ostringstream out;out<<"{\"queues\":[";
    for(size_t i=0;i<3;i++) {
        if(i)out<<',';auto entries=timers[i]?timers[i]->observation_events():std::vector<EventTimer::Event>{};
        const auto end=std::min(entries.size(),offset+(full?entries.size():8));
        out<<"{\"domain\":"<<quote(domains[i])<<",\"available\":"<<(timers[i]?"true":"false")<<",\"now\":"<<quote(std::to_string(now[i]))
           <<",\"pending\":"<<entries.size()<<",\"next_due\":"<<(entries.empty()?"null":quote(std::to_string(entries[0].triggerCycles)))<<",\"events\":[";
        for(size_t j=offset;j<end;j++){if(j!=offset)out<<',';out<<"{\"instance_id\":"<<quote(std::to_string(entries[j].instanceID))<<",\"due\":"<<quote(std::to_string(entries[j].triggerCycles))
            <<",\"callback_available\":"<<(entries[j].triggerCallback?"true":"false")<<",\"owner\":"<<(entries[j].observation_owner?quote(entries[j].observation_owner):"null")
            <<",\"source\":"<<(entries[j].observation_source?quote(entries[j].observation_source):"null")<<'}';}
        out<<"],\"next_offset\":"<<(end<entries.size()?std::to_string(end):"null")<<'}';
    }
    out<<"],\"coverage\":\"three EventTimer queues; device-local timers, host queues and future software decisions are not all enumerated\","
       "\"source\":[\"src/util/EventTimer.cpp\",\"src/gs2.cpp\"],\"evidence_class\":\"scheduled-model-events\"}";
    return out.str();
}
inline const char *device_source(device_id id) {
    switch(id) {
        case DEVICE_ID_KEYBOARD_IIPLUS:case DEVICE_ID_KEYBOARD_IIE:return "src/devices/keyboard/keyboard.cpp";
        case DEVICE_ID_SPEAKER:return "src/devices/speaker/speaker.cpp";
        case DEVICE_ID_DISPLAY:return "src/display/display.cpp";
        case DEVICE_ID_GAMECONTROLLER:return "src/devices/game/gamecontroller.cpp";
        case DEVICE_ID_DISK_II:return "src/devices/diskii/diskii_controller.hpp";
        case DEVICE_ID_THUNDER_CLOCK:return "src/devices/thunderclock_plus/thunderclockplus.cpp";
        case DEVICE_ID_MOCKINGBOARD:return "src/devices/mockingboard/mb2.cpp";
        case DEVICE_ID_IIE_MEMORY:return "src/devices/iiememory/iiememory.cpp";
        case DEVICE_ID_MOUSE:return "src/devices/applemouseiii/applemouseiii.cpp";
        case DEVICE_ID_RTC_PRAM:return "src/devices/rtc/rtc_pram.cpp";
        case DEVICE_ID_KEYGLOO:return "src/devices/adb/keygloo.cpp";
        case DEVICE_ID_ENSONIQ:return "src/devices/es5503/soundglu.cpp";
        case DEVICE_ID_SCC8530:return "src/devices/scc8530/scc8530.cpp";
        case DEVICE_ID_IWM:return "src/devices/iwm/iwm_device.cpp";
        case DEVICE_ID_SECOND_SIGHT:return "src/devices/secondsight/secondsight.cpp";
        case DEVICE_ID_UTHERNET2:return "src/devices/uthernet2/uthernet2.cpp";
        case DEVICE_ID_SUPER_SERIAL:return "src/devices/ssc/ssc.cpp";
        case DEVICE_ID_VOC:return "src/devices/voc/voc.cpp";
        case DEVICE_ID_A2GSPU:return "src/devices/a2gspu/a2gspu.cpp";
        default:return "src/devices.cpp";
    }
}
inline std::vector<std::string> nodes(computer_t *c) {
    std::vector<std::string> out={
        "{\"id\":\"machine\",\"installed\":true,\"source\":\"src/computer.cpp\"}",
        "{\"id\":\"cpu\",\"installed\":true,\"parent\":\"machine\",\"source\":\"src/cpus/base_6502.cpp\",\"relations\":[\"executes-through:mmu\",\"advances:clock\",\"receives:irq\"]}",
        "{\"id\":\"mmu\",\"installed\":true,\"parent\":\"machine\",\"source\":"+quote(a2obs_is_iigs(rail_mmu(c))?"src/mmus/mmu_iigs.cpp":"src/mmus/mmu_iie.cpp")+",\"query\":\"context address <hex>\"}",
        "{\"id\":\"clock\",\"installed\":true,\"parent\":\"machine\",\"source\":\"src/NClock.hpp\",\"relations\":[\"times:cpu\",\"times:devices\"]}",
        "{\"id\":\"irq\",\"installed\":true,\"parent\":\"machine\",\"source\":\"src/util/InterruptController.hpp\",\"relations\":[\"signals:cpu\"]}",
        "{\"id\":\"scheduler\",\"installed\":true,\"parent\":\"machine\",\"source\":\"src/util/EventTimer.cpp\",\"query\":\"context schedule\"}"};
    for(int id=1;id<NUM_DEVICE_IDS;id++) {
        auto *device=get_device((device_id)id);const auto coverage=a2_device_coverage((device_id)id);
        std::vector<int> slots;for(auto installed:c->installed_devices)if(installed.first==id)slots.push_back(installed.second);
        std::ostringstream node;node<<"{\"id\":"<<quote("device:"+std::to_string(id))<<",\"device_id\":"<<id<<",\"name\":"<<quote(device?device->name:"unknown")
            <<",\"installed\":"<<(slots.empty()?"false":"true")<<",\"slots\":[";
        for(size_t i=0;i<slots.size();i++){if(i)node<<',';node<<slots[i];}
        node<<"],\"parent\":\"machine\",\"source\":"<<quote(device_source((device_id)id))<<",\"observers\":"<<quote(coverage.observers)
            <<",\"snapshot_blocks\":"<<quote(coverage.snapshot)<<",\"complete_state\":false,\"query\":"<<quote("context device "+std::to_string(id))<<'}';
        out.push_back(node.str());
    }
    return out;
}
inline std::string topology(computer_t *c,size_t offset=0,bool full=false) {
    auto all=nodes(c);if(offset>all.size())throw std::runtime_error("topology offset exceeds inventory");
    size_t end=std::min(all.size(),offset+(full?all.size():8));std::ostringstream out;
    out<<"{\"total\":"<<all.size()<<",\"offset\":"<<offset<<",\"nodes\":[";
    for(size_t i=offset;i<end;i++){if(i!=offset)out<<',';out<<all[i];}
    out<<"],\"next_offset\":"<<(end<all.size()?std::to_string(end):"null")<<",\"coverage\":\"core graph plus every compiled device ID; declared relations describe implementation structure, not a complete bus transaction or device dependency graph\"}";
    return out.str();
}
inline std::string device(computer_t *c,int id) {
    auto coverage=a2_device_coverage((device_id)id);auto *model=get_device((device_id)id);
    bool installed=false;for(auto row:c->installed_devices)if(row.first==id)installed=true;
    std::ostringstream out;out<<"{\"id\":"<<id<<",\"name\":"<<quote(model?model->name:"unknown")<<",\"installed\":"<<(installed?"true":"false")
        <<",\"source\":"<<quote(device_source((device_id)id))<<",\"observers\":[";
    // Names can list several observers; token equality prevents accidental aliases.
    std::string names=std::string(",")+coverage.observers+",";bool comma=false;
    for(auto &handler:c->debug_display_handlers) {
        if(!installed || names.find(","+handler.name+",")==std::string::npos)continue;
        if(comma)out<<',';comma=true;
        out<<"{\"name\":"<<quote(handler.name)<<",\"fields\":[";
        std::unique_ptr<DebugFormatter> state(handler.native_handler?handler.native_handler():nullptr);
        if(state)for(size_t j=0;j<state->fields.size();j++){auto &f=state->fields[j];if(j)out<<',';out<<"{\"name\":"<<quote(f.name)<<",\"type\":"<<quote(f.type)<<",\"unit\":"<<quote(f.unit)<<",\"value\":"<<f.value_json<<'}';}
        out<<"]}";
    }
    out<<"],\"complete_state\":false,\"gap\":"<<quote(!installed?"model-not-installed":!comma?"no-registered-native-observer":"native-fields-are-partial")
       <<",\"controls\":"<<quote(coverage.controls)<<",\"candidate_snapshot_blocks\":"<<quote(coverage.snapshot)<<",\"evidence_class\":\"native-owner-fields\"}";
    return out.str();
}
inline std::string address(computer_t *c,uint32_t a) {
    MMU *mmu=rail_mmu(c);if(!a2obs_is_iigs(mmu) && a>0xffff)throw std::runtime_error("CPU address exceeds this processor address space; use explicit raw memory domains for aux");
    bool readable=a2obs_readable(mmu,a);std::ostringstream out;
    out<<"{\"address\":"<<a<<",\"readable\":"<<(readable?"true":"false")<<",\"value\":"<<(readable?std::to_string(a2obs_peek(mmu,a)):"null")
        <<",\"observation_path\":\"a2obs_peek; no bus handlers, cycles or soft-switch effects\",\"page_table\":";
    auto *page=mmu->observation_page(a);
    if(page)out<<"{\"page_size\":"<<mmu->observation_page_size()<<",\"read_pointer\":"<<(page->read_p?"true":"false")<<",\"read_handler\":"<<(page->read_h.read?"true":"false")
        <<",\"write_pointer\":"<<(page->write_p?"true":"false")<<",\"write_handler\":"<<(page->write_h.write?"true":"false")<<",\"shadow_handler\":"<<(page->shadow_h.write?"true":"false")
        <<",\"read_description\":"<<(page->read_d?quote(page->read_d):"null")<<",\"write_description\":"<<(page->write_d?quote(page->write_d):"null")<<'}';
    else out<<"null";
    if(auto *gs=dynamic_cast<MMU_IIgs*>(mmu)) {
        const unsigned bank=a>>16;const unsigned lo=a&65535;
        out<<",\"mapping_state\":{\"state\":"<<(unsigned)gs->state_register()<<",\"shadow\":"<<(unsigned)gs->shadow_register()<<",\"new_video\":"<<(unsigned)gs->new_video_register()<<",\"speed\":"<<(unsigned)gs->speed_register()<<'}';
        if(bank<=1 && !(gs->is_iolc_shadowed() && lo>=0xc000 && lo<0xd000)) {
            uint32_t adjusted=a;if(gs->is_iolc_shadowed() && lo>=0xd000 && gs->is_lc_bank1() && lo<0xe000)adjusted-=0x1000;
            bool lc=gs->is_iolc_shadowed() && lo>=0xd000;
            const uint32_t write_index=adjusted+gs->calc_aux_write(adjusted);
            const bool shadow=(!lc || gs->is_lc_write_enable()) && gs->shadow_is_enabled(write_index);
            out<<",\"resolved_read\":{\"domain\":"<<quote(lc&&!gs->is_lc_read_enable()?"rom":"main_raw")<<",\"offset\":"<<(lc&&!gs->is_lc_read_enable()?gs->rom_bank_ff_offset()+lo:adjusted+gs->calc_aux_read(adjusted))<<'}'
               <<",\"resolved_write\":{\"domain\":\"main_raw\",\"offset\":"<<adjusted+gs->calc_aux_write(adjusted)<<",\"enabled\":"<<(!lc || gs->is_lc_write_enable()?"true":"false")<<'}'
               <<",\"shadow_enabled\":"<<(shadow?"true":"false")<<",\"shadow_target\":";
            if(shadow)out<<"{\"domain\":\"megaii_raw\",\"offset\":"<<gs->megaii_cpu_index(write_index&0x1ffff)<<'}';else out<<"null";
        } else if((bank<=1 && gs->is_iolc_shadowed()) || bank==0xe0 || bank==0xe1) {
            out<<",\"routing\":\"Mega-II compatibility mapping; bank latch, LC and slot selection apply; inspect native ii-gs/iiememory fields\"";
            if(gs->megaii)out<<",\"selected_c8_slot\":"<<gs->megaii->get_C8xx_slot();
        }
        out<<",\"source\":[\"src/mmus/mmu_iigs.hpp\",\"src/mmus/mmu_iigs.cpp\",\"src/a2gspu_observe.hpp\"]";
    } else out<<",\"source\":[\"src/mmus/mmu_ii.cpp\",\"src/mmus/mmu_iie.cpp\",\"src/a2gspu_observe.hpp\"]";
    out<<",\"evidence_class\":\"current-mapping-and-probe; not historical last-writer attribution\",\"unreadable_reason\":"<<(readable?"null":quote("no side-effect-free probe value; inspect owning device state"))<<'}';return out.str();
}
inline std::string value(const char *type,const std::string &v,const char *unit="") {return "{\"type\":"+quote(type)+",\"unit\":"+quote(unit)+",\"value\":"+v+"}";}
inline Fields fields(computer_t *c,uint32_t address=0,uint32_t length=0) {
    Fields out;auto *p=c->cpu;
    const bool native=p->cpu_type==PROCESSOR_65816 && !p->E;
    out["cpu.a"]=value(native&&!p->_M?"u16":"u8",std::to_string(native&&!p->_M?p->a:p->a_lo));
    out["cpu.x"]=value(native&&!p->_X?"u16":"u8",std::to_string(native&&!p->_X?p->x:p->x_lo));
    out["cpu.y"]=value(native&&!p->_X?"u16":"u8",std::to_string(native&&!p->_X?p->y:p->y_lo));
    if(p->cpu_type==PROCESSOR_65816) {
        out["cpu.a_storage"]=value("u16",std::to_string(p->a));out["cpu.d"]=value("u16",std::to_string(p->d));
        out["cpu.dbr"]=value("u8",std::to_string(p->db));out["cpu.pbr"]=value("u8",std::to_string(p->pb));out["cpu.e"]=value("bool",p->E?"true":"false");
    }
    out["cpu.pc"]=value("u32",std::to_string(p->cpu_type==PROCESSOR_65816?p->full_pc&0xffffff:p->pc));
    out["cpu.sp"]=value("u16",std::to_string(p->sp));out["cpu.p"]=value("u8",std::to_string(p->p));out["cpu.halt"]=value("enum",std::to_string(p->halt));
    out["clock.cpu_cycles"]=value("u64",quote(std::to_string(c->clock->get_cycles())),"CPU cycles");
    out["clock.master_14m_ticks"]=value("u64",quote(std::to_string(c->clock->get_c14m())),"14M ticks");
    out["clock.video_cycles"]=value("u64",quote(std::to_string(c->clock->get_vid_cycles())),"video cycles");
    if(c->irq_control)out["irq.pending_mask"]=value("u64",quote(std::to_string(*c->irq_control->obs_pending_ptr())));
    size_t instance=0;
    for(auto &handler:c->debug_display_handlers) {
        std::unique_ptr<DebugFormatter> state(handler.native_handler?handler.native_handler():nullptr);
        std::string prefix="device."+handler.name+"["+std::to_string(instance++)+"].";
        if(state)for(auto &f:state->fields)out[prefix+f.name]=value(f.type.c_str(),f.value_json,f.unit.c_str());
    }
    for(uint32_t i=0;i<length;i++) {
        auto *m=rail_mmu(c);auto a=address+i;char name[32];std::snprintf(name,sizeof name,"memory.%06X",a);
        out[name]=value("u8",a2obs_readable(m,a)?std::to_string(a2obs_peek(m,a)):"null");
    }
    return out;
}
inline void sample(computer_t *c) {
    if(!a2events::needs("context"))return;
    attach(c);auto now=fields(c);std::vector<std::string> changed;
    for(auto &f:now) {auto previous=sampled.find(f.first);if(previous==sampled.end() || previous->second!=f.second)
        changed.push_back("{\"field\":"+quote(f.first)+",\"before\":"+(previous==sampled.end()?"null":previous->second)+",\"after\":"+f.second+"}");}
    if(!changed.empty()) {
        std::string data="{\"schema\":\"a2context-delta-v1\",\"stamp\":"+stamp(c)+",\"baseline\":"+(sampled.empty()?"true":"false")+",\"changed_fields\":"+std::to_string(changed.size())+",\"changes\":[";
        for(size_t i=0;i<std::min(changed.size(),size_t(8));i++){if(i)data+=',';data+=changed[i];}
        data+="],\"truncated\":"+std::string(changed.size()>8?"true":"false")+",\"model\":\"command/frame-sampled fields; retain a mark or snapshot for expanded state; transient changes may be missed\"}";
        a2events::publish("context",data);
    }
    sampled=std::move(now);
}
inline std::string changes(computer_t *c,uint64_t mark,size_t offset) {
    const Mark *saved=nullptr;for(auto &m:marks)if(m.id==mark)saved=&m;
    if(!saved)throw std::runtime_error("unknown-or-evicted-mark; retain at most four marks");
    auto now=fields(c,saved->address,saved->length);std::set<std::string> keys;
    for(auto &f:saved->fields)keys.insert(f.first);for(auto &f:now)keys.insert(f.first);
    std::vector<std::string> changed;
    for(auto &key:keys) {
        auto a=saved->fields.find(key);auto b=now.find(key);if(a!=saved->fields.end() && b!=now.end() && a->second==b->second)continue;
        changed.push_back("{\"field\":"+quote(key)+",\"before\":"+(a==saved->fields.end()?"null":a->second)+",\"after\":"+(b==now.end()?"null":b->second)+"}");
    }
    if(offset>changed.size())throw std::runtime_error("changes offset exceeds result");
    auto end=std::min(changed.size(),offset+8);std::ostringstream out;
    out<<"{\"mark_id\":"<<quote(std::to_string(mark))<<",\"mark_stamp\":"<<saved->stamp<<",\"changed_fields\":"<<changed.size()<<",\"changes\":[";
    for(size_t i=offset;i<end;i++){if(i!=offset)out<<',';out<<changed[i];}
    out<<"],\"next_offset\":"<<(end<changed.size()?std::to_string(end):"null")<<",\"coverage\":\"native CPU/device fields plus explicitly marked memory window; intermediate transitions may be missed; each page is a new observation\"}";
    return out.str();
}
inline std::string events(uint64_t after,size_t count) {
    if(after>event_sequence)throw std::runtime_error("event cursor is ahead of this history");
    uint64_t lost=history.empty()?0:after+1<history.front().id?history.front().id-after-1:0;
    std::ostringstream out;out<<"{\"events\":[";size_t n=0,bytes=0;uint64_t next=after;bool more=false;
    for(auto &e:history)if(e.id>after) {
        std::string row="{\"id\":"+quote(std::to_string(e.id))+",\"parent_id\":"+(e.parent?quote(std::to_string(e.parent)):"null")+",\"kind\":"+quote(e.kind)+",\"stamp\":"+e.stamp+",\"payload\":"+e.payload+"}";
        if(n==count || (n && bytes+row.size()>6000)){more=true;break;}
        if(n++)out<<',';out<<row;bytes+=row.size();next=e.id;
    }
    out<<"],\"next_cursor\":"<<quote(std::to_string(next))<<",\"has_more\":"<<(more?"true":"false")<<",\"lost_before_cursor\":"<<quote(std::to_string(lost))
       <<",\"complete\":"<<(!lost && !oversized?"true":"false")<<",\"status\":"<<history_status()<<",\"causality\":\"parent links are recorded dynamic command/callback nesting; temporal adjacency alone never establishes causation; missing parents may have been evicted\"}";
    return out.str();
}
inline std::string overview(computer_t *c) {
    std::ostringstream out;out<<"{\"system_id\":"<<c->system_id<<",\"platform\":"<<quote(c->platform?c->platform->name:"unknown")<<",\"cpu\":"<<a2inspect::cpu(c)
       <<",\"clock\":"<<clocks(c)<<",\"history\":"<<history_status()<<",\"installed_device_instances\":"<<c->installed_devices.size()
       <<",\"native_observers\":"<<c->debug_display_handlers.size()<<",\"registered_signals\":"<<g_obs_registry.size()
       <<",\"queries\":[\"topology [offset]\",\"device <id>\",\"address <hex>\",\"irq\",\"stop\",\"pixel <x> <y>\",\"schedule [offset]\",\"history [after-id] [count]\",\"mark [hex-address decimal-length]\",\"changes <mark-id> [offset]\",\"snapshot <file>\",\"on [capacity]\",\"off\"]"
       <<",\"coverage\":{\"complete_machine_state\":false,\"complete_causal_history\":false,\"host_presentation\":\"reference raster and source audio differ from actual GPU and host mix\",\"source_semantics\":\"Rosetta bound maps via guestview; variables/unwind/DWARF incomplete\",\"replay\":\"snapshots exclude clock/host state; no full replay\"}}";return out.str();
}
inline void command(const char *args,char *result,size_t size,computer_t *c) {
    try {
        const auto began=std::chrono::steady_clock::now();
        auto p=a2inspect::arguments(args);if(p.empty())p.push_back("overview");
        attach(c);std::string data,kind=p[0];std::string identity=stamp(c);
        auto number=[&](size_t i,uint64_t max){return a2inspect::integer(p.at(i),10,max);};
        auto exact=[&](size_t count){if(p.size()!=count)throw std::runtime_error("wrong context argument count; context overview lists queries");};
        if(kind=="overview"){exact(1);data=overview(c);}
        else if(kind=="irq"){exact(1);data=irq(c);}
        else if(kind=="stop"){exact(1);data=stop(c);}
        else if(kind=="pixel"){exact(3);data=pixel(c,number(1,639),number(2,199));}
        else if(kind=="device"){exact(2);auto id=number(1,NUM_DEVICE_IDS-1);if(!id)throw std::runtime_error("device ID must be positive");data=device(c,id);}
        else if(kind=="address"){exact(2);data=address(c,a2inspect::integer(p[1],16,0xffffff));}
        else if(kind=="topology" || kind=="schedule") {if(p.size()>2)throw std::runtime_error("too many context arguments");size_t offset=p.size()==2?number(1,1000000):0;data=kind=="topology"?topology(c,offset):schedule(c,offset);}
        else if(kind=="on") {
            if(p.size()>2)throw std::runtime_error("too many context arguments");auto n=p.size()==2?number(1,4096):256;
            if(!n || active)throw std::runtime_error(active?"context-history-already-active":"capacity must be 1..4096");
            capacity=n;history.clear();sampled.clear();discarded=oversized=0;active=true;a2events::set_context(retain);data=history_status();
        } else if(kind=="off") {
            exact(1);if(!active)throw std::runtime_error("context-history-not-active");active=false;a2events::set_context({});data=history_status();
        } else if(kind=="history") {
            if(p.size()>3)throw std::runtime_error("too many history arguments");auto count=p.size()==3?number(2,8):4;if(!count)throw std::runtime_error("history count must be 1..8");data=events(p.size()>=2?number(1,UINT64_MAX):0,count);
        } else if(kind=="mark") {
            if(p.size()!=1 && p.size()!=3)throw std::runtime_error("mark [hex-address decimal-length]");
            auto a=p.size()==3?a2inspect::integer(p[1],16,a2obs_is_iigs(rail_mmu(c))?0xffffff:0xffff):0;
            auto n=p.size()==3?number(2,256):0;
            if(p.size()==3 && (!n || a+n>(a2obs_is_iigs(rail_mmu(c))?0x1000000:0x10000)))throw std::runtime_error("mark memory range invalid");
            auto captured=fields(c,a,n);if(marks.size()==4)marks.pop_front();marks.push_back({++mark_sequence,(uint32_t)a,(uint32_t)n,std::move(captured),identity});
            data="{\"mark_id\":"+quote(std::to_string(mark_sequence))+",\"fields\":"+std::to_string(marks.back().fields.size())+",\"memory_length\":"+std::to_string(n)+"}";
        } else if(kind=="changes") {
            if(p.size()<2 || p.size()>3)throw std::runtime_error("changes <mark-id> [offset]");data=changes(c,number(1,UINT64_MAX),p.size()==3?number(2,1000000):0);
        } else if(kind=="snapshot") {
            exact(2);data="{\"overview\":"+overview(c)+",\"topology\":"+topology(c,0,true)+",\"irq\":"+irq(c)+",\"schedule\":"+schedule(c,0,true)+",\"devices\":"+a2inspect::devices(c)+",\"signals\":"+a2inspect::signals("*")+"}";
        } else throw std::runtime_error("unknown context query; context overview lists queries");
        const auto cost=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-began).count();
        std::string wrapped="{\"schema\":\"a2context-v1\",\"kind\":"+quote(kind)+",\"stamp\":"+identity+",\"capture_host_ns\":"+quote(std::to_string(cost))+",\"capture_cost_model\":\"host wall duration through capture/render; excludes final envelope/file/transport\",\"data\":"+data+"}";
        if(kind=="snapshot") {
            std::ofstream file(p[1],std::ios::binary|std::ios::trunc);file<<wrapped<<'\n';file.close();if(!file)throw std::runtime_error("cannot write context snapshot");
            uint64_t hash=HOUSE_FNV_BASIS;for(unsigned char byte:wrapped){hash^=byte;hash*=HOUSE_FNV_PRIME;}hash^='\n';hash*=HOUSE_FNV_PRIME;
            wrapped="{\"schema\":\"a2context-ref-v1\",\"path\":"+quote(p[1])+",\"stamp\":"+identity+
                ",\"producer\":\"context.snapshot\",\"producer_finalized\":true,\"bytes\":"+quote(std::to_string(wrapped.size()+1))+
                ",\"hash_algorithm\":\"house-fnv1a64-noncanonical\",\"hash\":"+quote(std::to_string(hash))+"}";
        }
        if(wrapped.size()>12000)throw std::runtime_error("context reply too large; use context snapshot <file> for full evidence");
        a2ctrljson::native_payload=wrapped;
        std::snprintf(result,size,"status=OK context kind=%s schema=a2context-v1 observation=%llu",kind.c_str(),(unsigned long long)observation);
    } catch(const std::exception &e){std::snprintf(result,size,"status=FAIL context %s",e.what());}
}
}
