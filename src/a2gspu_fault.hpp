#pragma once
// Deliberate instruction-boundary faults; never a claim of mid-bus ABORT.
#include "cpu.hpp"
#include "util/InterruptController.hpp"
#include "a2gspu_ctrl_json.hpp"
#include "a2gspu_events.hpp"
#include <vector>
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace a2fault {
struct Scheduled {uint64_t id,due;std::string kind;};
inline std::vector<Scheduled> queue;
inline cpu_state *owner=nullptr;
inline InterruptController *controller=nullptr;
inline uint64_t next_id=1,delivered=0,last_due=0,last_actual=0;
inline bool irq_on=false;
inline bool installed=false;
inline bool busy(cpu_state *cpu) {return !queue.empty() || irq_on || cpu->nmi_pending || cpu->boundary_abort_pending;}
inline void clear(cpu_state *cpu) {
    queue.clear();if(controller && irq_on)controller->deassert_irq(IRQ_ID_AGENT);irq_on=false;
    if(cpu){cpu->nmi_pending=false;cpu->boundary_abort_pending=false;}
}
inline std::string status(cpu_state *cpu) {
    std::string out="{\"schema\":\"a2fault-v1\",\"model\":\"instruction-boundary-injection\",\"mid_bus_abort\":false,\"irq_held\":"+std::string(irq_on?"true":"false")+
        ",\"nmi_pending\":"+(cpu->nmi_pending?"true":"false")+",\"abort_pending\":"+(cpu->boundary_abort_pending?"true":"false")+
        ",\"delivered\":"+a2ctrljson::quote(std::to_string(delivered))+",\"last_due\":"+a2ctrljson::quote(std::to_string(last_due))+
        ",\"last_actual\":"+a2ctrljson::quote(std::to_string(last_actual))+",\"queue\":[";
    bool comma=false;for(const auto &item:queue){if(comma)out+=',';comma=true;out+="{\"id\":"+std::to_string(item.id)+",\"kind\":"+a2ctrljson::quote(item.kind)+",\"due_cycles\":"+a2ctrljson::quote(std::to_string(item.due))+"}";}
    return out+"]}";
}
inline void schedule(cpu_state *cpu,const std::string &kind,uint64_t delay,uint64_t now) {
    if(queue.size()>=128 || delay>std::numeric_limits<uint64_t>::max()-now)throw std::runtime_error("fault queue/deadline limit");
    if(kind!="nmi" && kind!="abort" && kind!="irq-on" && kind!="irq-off")throw std::runtime_error("unknown fault kind");
    if(kind=="abort" && cpu->cpu_type!=PROCESSOR_65816)throw std::runtime_error("ABORT requires a 65816");
    if((kind=="nmi" && cpu->nmi_pending) || (kind=="abort" && cpu->boundary_abort_pending))throw std::runtime_error("fault already latched");
    for(const auto &item:queue)if(item.kind==kind && (kind=="nmi" || kind=="abort"))throw std::runtime_error("same fault already queued; await delivery first");
    // A level transition must change the injector's own line. Do not queue
    // redundant edges or borrow a real device's IRQ bit.
    if(kind=="irq-on" || kind=="irq-off") {
        bool projected=irq_on;
        for(const auto &item:queue)if(item.due<=now+delay){if(item.kind=="irq-on")projected=true;if(item.kind=="irq-off")projected=false;}
        if(projected==(kind=="irq-on"))throw std::runtime_error("IRQ transition would be a no-op");
        if(!queue.empty() && queue.back().due>now+delay)throw std::runtime_error("IRQ edges must be scheduled in deadline order");
    }
    owner=cpu;queue.push_back({next_id++,now+delay,kind});
    std::stable_sort(queue.begin(),queue.end(),[](const auto&a,const auto&b){return a.due<b.due;});
}
inline void poll(cpu_state *cpu,uint64_t now) {
    if(cpu!=owner)return;
    while(!queue.empty() && queue.front().due<=now) {
        auto item=queue.front();queue.erase(queue.begin());last_due=item.due;last_actual=now;++delivered;
        if(item.kind=="nmi")cpu->nmi_pending=true;
        else if(item.kind=="abort")cpu->boundary_abort_pending=true;
        else if(item.kind=="irq-on"){controller->assert_irq(IRQ_ID_AGENT);irq_on=true;}
        else {controller->deassert_irq(IRQ_ID_AGENT);irq_on=false;}
        if(a2events::enabled())a2events::publish("fault","{\"id\":"+std::to_string(item.id)+",\"kind\":"+a2ctrljson::quote(item.kind)+
            ",\"due_cycles\":"+a2ctrljson::quote(std::to_string(item.due))+",\"actual_cycles\":"+a2ctrljson::quote(std::to_string(now))+
            ",\"lateness_cycles\":"+a2ctrljson::quote(std::to_string(now-item.due))+",\"pc\":"+std::to_string(cpu->full_pc&0xffffff)+"}");
    }
}
}
