#pragma once
// Native observations come from model fields, never parsed debug-display prose.
#include "a2gspu_ctrl_json.hpp"
#include "obs_signal.hpp"
#include <sstream>
#include <fstream>
#include <set>
#include <algorithm>
#include <cerrno>
#include "a2gspu_observe.hpp"
#include "a2gspu_device_coverage.hpp"
#include "a2gspu_events.hpp"
#include <map>

namespace a2inspect {
using a2ctrljson::quote;
inline std::vector<std::string> arguments(const char *p) {
    std::vector<std::string> out;
    while(*p) {
        while(*p==' ' || *p=='\t')++p;if(!*p)break;
        bool quoted=*p=='"';if(quoted)++p;std::string s;
        while(*p && (quoted?*p!='"':(*p!=' ' && *p!='\t')))s+=*p++;
        if(quoted) {if(*p!='"')throw std::runtime_error("unclosed quote");++p;if(*p && *p!=' ' && *p!='\t')throw std::runtime_error("bad delimiter");}
        if(s.empty())throw std::runtime_error("empty argument");out.push_back(s);
    }
    return out;
}
inline const char *type_name(uint8_t type) {
    static const char *names[]={"u8","u16","u32","u64","i8","i16","i32","i64","bool","bitfield","enum","rgb12","bytes"};
    return type<sizeof names/sizeof *names?names[type]:"unknown";
}
inline std::string scalar(uint8_t type,uint64_t value) {
    if(type==OBS_T_BOOL)return value?"true":"false";
    if(type==OBS_T_I64 || type==OBS_T_U64)return quote(type==OBS_T_I64?std::to_string((int64_t)value):std::to_string(value));
    return type>=OBS_T_I8 && type<=OBS_T_I32?std::to_string((int64_t)value):std::to_string(value);
}
inline std::string signals(const char *glob) {
    std::ostringstream out;out<<"{\"scope\":\"registered Observatory signals; not a claim of complete device coverage\",\"signals\":[";
    bool comma=false;std::set<uint32_t> ids;std::set<std::string> names;
    for(const auto &d:g_obs_registry) {
        if(!ids.insert(d.sigid).second || !d.path || !names.insert(d.path).second)throw std::runtime_error("duplicate/invalid signal identity");
        if(!obs_glob_match(glob,d.path))continue;
        if(comma)out<<',';comma=true;
        out<<"{\"id\":"<<d.sigid<<",\"path\":"<<quote(d.path)<<",\"type\":"<<quote(type_name(d.type))
           <<",\"class\":"<<(int)d.cls<<",\"flags\":"<<d.flags<<",\"length\":"<<d.len<<",\"width_bytes\":"<<(int)d.width;
        const bool edge=(d.flags&OBS_F_EDGE_ONLY) || d.cls==OBS_C_EVENT;
        const bool clock_latched=obs_sigid_subsys(d.sigid)==OBS_SUB_CLOCK && !g_obs_clock_cost_enabled;
        const bool unbounded=d.cls==OBS_C_ARRAY && !d.len;
        if(edge || !obs_resolve_owner(&d) || clock_latched || unbounded) {
            out<<",\"available\":false,\"reason\":"<<quote(edge?"event-only":clock_latched?"clock-observer-not-armed":unbounded?"array-length-undeclared":"owner-unavailable");
        } else if(d.cls==OBS_C_MEMWINDOW) {
            const uint32_t n=std::min(d.len,256u);std::vector<uint8_t> bytes(n);
            if(obs_read_window(d.sigid,bytes.data(),n)!=n)throw std::runtime_error("window read failed");
            out<<",\"available\":true,\"encoding\":\"byte-array\",\"truncated\":"<<(n<d.len?"true":"false")<<",\"value\":[";
            for(uint32_t i=0;i<n;i++){if(i)out<<',';out<<(unsigned)bytes[i];}out<<']';
        } else {
            const uint32_t n=d.cls==OBS_C_ARRAY?std::min(d.len,256u):1;
            out<<",\"available\":true,\"encoding\":"<<quote(d.type==OBS_T_U64 || d.type==OBS_T_I64?"decimal-string":"native-json")
               <<",\"truncated\":"<<(d.cls==OBS_C_ARRAY && n<d.len?"true":"false")<<",\"value\":";
            if(d.cls==OBS_C_ARRAY)out<<'[';
            for(uint32_t i=0;i<n;i++) {uint64_t v=0;if(!obs_read(d.sigid,&v,i))throw std::runtime_error("signal read failed");if(i)out<<',';out<<scalar(d.type,v);}
            if(d.cls==OBS_C_ARRAY)out<<']';
        }
        out<<'}';
    }
    return out.str()+"]}";
}
inline std::string cpu(computer_t *computer) {
    auto *c=computer->cpu;if(!c)throw std::runtime_error("CPU unavailable");
    const bool is816=c->cpu_type==PROCESSOR_65816,native=is816 && !c->E;
    const int aw=native&&!c->_M?16:8,xw=native&&!c->_X?16:8;
    std::ostringstream out;
    out<<"{\"processor_type\":"<<(int)c->cpu_type<<",\"sp_model\":\"bank0-stack-address\",\"registers\":{\"pc\":"<<(is816?(c->full_pc&0xffffff):c->pc)
       <<",\"a\":"<<(aw==16?c->a:c->a_lo)<<",\"x\":"<<(xw==16?c->x:c->x_lo)<<",\"y\":"<<(xw==16?c->y:c->y_lo)
       <<",\"sp\":"<<(native?c->sp:(0x100u|(c->sp&255u)))<<",\"p\":"<<(unsigned)c->p;
    if(is816)out<<",\"a_storage\":"<<c->a<<",\"d\":"<<c->d<<",\"dbr\":"<<(unsigned)c->db<<",\"pbr\":"<<(unsigned)c->pb<<",\"e\":"<<(c->E?"true":"false");
    out<<"},\"register_widths\":{\"a\":"<<aw<<",\"x\":"<<xw<<",\"y\":"<<xw
       <<"},\"halt_reason\":"<<(int)c->halt<<",\"stopped\":"<<(c->clock_stopped?"true":"false")<<",\"waiting_for_interrupt\":"<<(c->rdy?"true":"false")
       <<",\"interrupts\":{\"irq_line\":"<<(c->irq_asserted?"true":"false")<<",\"irq_pipeline\":"<<(unsigned)c->irq_pipe
       <<",\"nmi_pending\":"<<(c->nmi_pending?"true":"false")<<",\"boundary_abort_pending\":"<<(c->boundary_abort_pending?"true":"false")
       <<",\"reset_sources\":"<<quote(std::to_string(c->reset_asserted))<<"}}";
    return out.str();
}
inline std::string devices(computer_t *computer) {
    std::ostringstream out;out<<"{\"coverage\":\"installed models plus independent debug-handler inventory; native fields are partial\",\"installed\":[";
    bool installed_comma=false;
    for(auto identity:computer->installed_devices) {
        auto *device=get_device((device_id)identity.first);
        auto coverage=a2_device_coverage((device_id)identity.first);
        if(installed_comma)out<<',';installed_comma=true;
        out<<"{\"device_id\":"<<identity.first<<",\"slot\":"<<identity.second<<",\"name\":"<<quote(device?device->name:"unknown")
           <<",\"socket_device_handler\":"<<(computer->device_debug_handlers[identity.first]?"true":"false")
           <<",\"state_ops\":\"device-specific; callback availability does not imply all opcodes\",\"native_observer_names\":"<<quote(coverage.observers)
           <<",\"candidate_snapshot_blocks\":"<<quote(coverage.snapshot)<<",\"controls\":"<<quote(coverage.controls)
           <<",\"snapshot_coverage\":\"per-snapshot-inventory is authoritative; no full-replay claim\",\"complete_typed_state\":false}";
    }
    out<<"],\"devices\":[";
    bool comma=false;size_t instance=0;
    for(auto &h:computer->debug_display_handlers) {
        if(comma)out<<',';comma=true;
        out<<"{\"name\":"<<quote(h.name)<<",\"instance\":"<<instance++<<",\"diagnostic_command\":"<<quote("devstate "+h.name)
           <<",\"diagnostic_format\":\"human-text\",\"complete_typed_state\":false,\"generic_state_mutation\":false,\"fields\":[";
        // Human diagnostics may sample host queues or keep UI history. Only an
        // explicitly registered native observer is allowed on this path.
        std::unique_ptr<DebugFormatter> state(h.native_handler?h.native_handler():nullptr);
        if(state)for(size_t i=0;i<state->fields.size();i++) {
            auto &f=state->fields[i];if(i)out<<',';
            out<<"{\"name\":"<<quote(f.name)<<",\"type\":"<<quote(f.type)<<",\"unit\":"<<quote(f.unit)
               <<",\"encoding\":"<<quote(f.type=="u64" || f.type=="i64"?"decimal-string":"native-json")<<",\"value\":"<<f.value_json<<'}';
        }
        out<<"],\"typed_available\":"<<(state && !state->fields.empty()?"true":"false")<<'}';
    }
    return out.str()+"]}";
}
inline std::string capabilities(computer_t *computer) {
    std::ostringstream out;out<<"{\"commands\":[";int count=0;auto *verbs=a2manifest::verbs(&count);
    for(int i=0;i<count;i++){if(i)out<<',';out<<"{\"name\":"<<quote(verbs[i].name)<<",\"kind\":"<<quote(verbs[i].kind)<<",\"model\":"<<quote(verbs[i].model)<<",\"synopsis\":"<<quote(verbs[i].synopsis)<<'}';}
    out<<"],\"rails\":[";int n=0;auto *rails=a2rails::rails(n);
    for(int i=0;i<n;i++) {
        if(i)out<<',';auto *r=a2rails::reach_find(rails[i].name);auto *s=a2rails::setter_find(rails[i].name);
        out<<"{\"name\":"<<quote(rails[i].name)<<",\"family\":"<<quote(rails[i].family)
           <<",\"live_command\":"<<(r && *r->verb?quote(r->verb):"null")<<",\"generic_setter\":"<<(s?"true":"false")
           <<",\"readback\":"<<(r && *r->read?quote(r->read):"null")
           <<",\"live_state\":"<<quote(s?"generic-setter":r && *r->verb?"dedicated-command":"launch-only")
           <<",\"live_gap_reason\":"<<(s || (r && *r->verb)?"null":quote("no live apply registered; launch configuration currently required"))<<'}';
    }
    out<<"],\"device_inventory\":"<<devices(computer)<<",\"limitations\":"<<a2manifest::limitations_json()<<",\"limitations_scope\":\"global known model gaps; not a complete fidelity proof\",\"limitations_command\":\"limitations <file>\",\"snapshot_contract\":\"per-file included/excluded lists; external host state is not complete replay\",\"full_replay\":false,\"reverse_step\":false,"
       <<"\"client_libraries\":[{\"language\":\"Python\",\"package\":\"gs2debug\",\"file_client\":\"CtrlClient\",\"socket_client\":\"Client\",\"source_path\":\"clients/python/src\"}],"
       <<"\"companion_tools\":[{\"name\":\"guestview\",\"entrypoint\":\"tools/a2rail/guestview.py\",\"availability\":\"requires-source-checkout-and-python\",\"commands\":[\"schema\",\"normalize-rosetta\",\"source\",\"dmesg\",\"tasks\",\"stack\",\"panic\"],\"layout_schema\":\"a2kernel-layout-v1\",\"limitations\":[\"explicit-layout-required\",\"Rosetta-line-sidecar-subset\",\"DWARF-not-yet-supported\"]},"
       <<"{\"name\":\"checkpoint\",\"entrypoint\":\"tools/a2rail/checkpoint.py\",\"commands\":[\"capture\",\"diff\"],\"availability\":\"requires-source-checkout-python-and-snapdiff\",\"complete_device_semantics\":false},"
       <<"{\"name\":\"outputassert\",\"entrypoint\":\"tools/a2rail/outputassert.py\",\"commands\":[\"image\",\"audio\",\"text\"],\"availability\":\"requires-source-checkout-and-python\"},"
       <<"{\"name\":\"inputmacro\",\"entrypoint\":\"tools/a2rail/inputmacro.py\",\"availability\":\"requires-source-checkout-and-python\",\"schema\":\"a2input-plan-v1\",\"complete_machine_replay\":false},"
       <<"{\"name\":\"experiment\",\"entrypoint\":\"tools/a2rail/experiment.py\",\"commands\":[\"run\",\"compare\"],\"plan_schema\":\"a2experiment-plan-v1\",\"availability\":\"requires-source-checkout-python-and-selected-emulator; optional build tools are explicit\",\"model\":\"digest-bound raw CPU fixture; correctness/negative controls and native context; observed efficiency pairs, not proven hardware bounds\"},"
       <<"{\"name\":\"mcp\",\"entrypoint\":\"tools/a2rail/mcpserver.py\",\"installed_entrypoint\":\"gs2-mcp\",\"availability\":\"requires-python-client-package-or-source-checkout; socket tool extensions required\",\"transport\":\"stdio-jsonrpc\",\"protocol_version\":\"2025-11-25\",\"tool_registry\":\"live AGENT_TOOLS plus structured observations, artifacts, machine context and isolated batches\",\"artifact_access\":\"managed output directory by default; explicit --artifact-root on attach\",\"artifact_identity\":\"SHA256-bound bytes; native PNG image content; create-once test input upload\",\"launch\":\"--exe <executable> --platform <id> --out-dir <new-dir>\",\"attach\":\"--socket <endpoint> [--artifact-root <existing-directory>]\"}]}";
    return out.str();
}
inline computer_t *device_event_owner=nullptr;
inline std::map<std::string,std::string> device_event_values;
inline void device_events(computer_t *computer) {
    if(!a2events::needs("device")){device_event_values.clear();return;}
    if(device_event_owner!=computer) {
        device_event_values.clear();device_event_owner=computer;
        computer->register_shutdown_handler([](){device_event_values.clear();device_event_owner=nullptr;return true;});
    }
    std::map<std::string,std::string> now;size_t instance=0;
    for(auto &handler:computer->debug_display_handlers) {
        const std::string prefix=handler.name+"["+std::to_string(instance++)+"]";
        std::unique_ptr<DebugFormatter> state(handler.native_handler?handler.native_handler():nullptr);
        if(state)for(const auto &field:state->fields)now[prefix+"."+field.name]="{\"type\":"+quote(field.type)+",\"unit\":"+quote(field.unit)+",\"encoding\":"+quote(field.type=="u64" || field.type=="i64"?"decimal-string":"native-json")+",\"value\":"+field.value_json+"}";
    }
    std::set<std::string> names;for(auto &v:device_event_values)names.insert(v.first);for(auto &v:now)names.insert(v.first);
    const bool baseline=device_event_values.empty();
    for(auto &name:names) {
        auto before=device_event_values.find(name),after=now.find(name);
        if(before!=device_event_values.end() && after!=now.end() && before->second==after->second)continue;
        a2record::Record record("device",baseline?"baseline":"change",a2record::frame_now(),computer->clock->get_cycles());
        record.str("schema","a2device-change-v1").str("model","command-and-frame-boundary native fields; transient changes between samples may be missed")
            .str("field",name.c_str()).raw("before",before==device_event_values.end()?"null":before->second.c_str())
            .raw("after",after==now.end()?"null":after->second.c_str());record.done();
    }
    device_event_values=std::move(now);
}
inline uint64_t integer(const std::string &text,int base,uint64_t limit) {
    if(text.empty() || text[0]=='-' || text[0]=='+')throw std::runtime_error("invalid numeric argument");
    char *end=nullptr;errno=0;auto n=std::strtoull(text.c_str(),&end,base);
    if(errno || end!=text.c_str()+text.size() || n>limit)throw std::runtime_error("numeric argument out of range");
    return n;
}
inline std::string memory(computer_t *computer,const std::string &address,const std::string &count) {
    auto *mmu=rail_mmu(computer);
    if(!mmu)throw std::runtime_error("MMU unavailable");
    auto start=integer(address,16,0xffffff),length=integer(count,10,65536);
    if(!length || start+length>0x1000000)throw std::runtime_error("invalid memory capture range");
    std::ostringstream out,valid;size_t unreadable=0;
    out<<"{\"regions\":[{\"start\":"<<start<<",\"bytes\":[";
    for(size_t i=0;i<length;i++) {
        bool readable=a2obs_readable(mmu,(uint32_t)(start+i));
        if(i){out<<',';valid<<',';}
        out<<(unsigned)(readable?a2obs_peek(mmu,(uint32_t)(start+i)):0);
        valid<<(readable?"true":"false");if(!readable)++unreadable;
    }
    out<<"],\"readable\":["<<valid.str()<<"]}],\"unreadable_count\":"<<unreadable
       <<",\"complete\":"<<(unreadable?"false":"true")<<",\"unreadable_value\":\"zero-placeholder; never guest truth\"}";
    return out.str();
}
inline void command(const char *args,char *result,size_t size,computer_t *computer) {
    try {
        auto p=arguments(args);
        if(p.size()<2 || p.size()>4)throw std::runtime_error("usage: inspect cpu|devices|signals|capabilities <file> [signal-glob] | memory <file> <hex-address> <decimal-length>");
        if(p.size()>2 && p[0]!="signals" && p[0]!="memory")throw std::runtime_error("extra inspection arguments");
        if(p[0]=="signals" && p.size()>3)throw std::runtime_error("too many signal arguments");
        std::string data;
        if(p[0]=="cpu")data=cpu(computer);
        else if(p[0]=="devices")data=devices(computer);
        else if(p[0]=="signals")data=signals(p.size()==3?p[2].c_str():"*");
        else if(p[0]=="capabilities")data=capabilities(computer);
        else if(p[0]=="memory" && p.size()==4)data=memory(computer,p[2],p[3]);
        else throw std::runtime_error("unknown inspection kind");
        std::ostringstream json;json<<"{\"schema\":\"a2inspect-v1\",\"kind\":"<<quote(p[0])
            <<",\"surface\":"<<quote(a2ctrljson::surface)
            <<",\"frames\":"<<quote(std::to_string(computer->clock?computer->clock->get_frame_count():0))<<",\"frame_model\":\"NClock-video-frame-counter; excludes host refresh\""
            <<",\"model\":"<<quote(p[0]=="memory"?"memory-peek":"internal-state-observation")<<",\"cycle_model\":\"session-absolute-NClock\",\"cycles\":"
            <<quote(std::to_string(computer->clock?computer->clock->get_cycles():0))<<",\"data\":"<<data<<"}\n";
        std::ofstream out(p[1],std::ios::binary|std::ios::trunc);out<<json.str();out.close();
        if(!out)throw std::runtime_error("cannot write inspection artifact");
        a2ctrljson::native_payload="{\"schema\":\"a2inspect-ref-v1\",\"kind\":"+quote(p[0])+",\"path\":"+quote(p[1])+"}";
        std::snprintf(result,size,"status=OK inspect kind=%s schema=a2inspect-v1 model=internal-state-observation -> %s",p[0].c_str(),p[1].c_str());
    } catch(const std::exception &e){std::snprintf(result,size,"status=FAIL inspect %s",e.what());}
}
}
