#pragma once
#include "a2record.hpp"
#include "a2gspu_ctrl_json.hpp"
#include <filesystem>
#include <vector>
#include <sstream>
#include <functional>

namespace a2events {
inline FILE *sink=nullptr;
inline std::string path,error;
inline std::vector<std::string> kinds;
inline uint64_t retained=0,dropped=0,limit=100000,sequence=0;
inline bool active=false,publishing=false;
inline std::function<void(const char*,const std::string&)> secondary;
inline std::function<bool(const char*)> secondary_selects;
// Independent bounded context consumer. It coexists with file/socket sinks.
inline std::function<uint64_t(const char*,const std::string&)> context_listener;
inline uint64_t cause_id=0;
struct CauseScope {
    uint64_t previous;
    explicit CauseScope(uint64_t id):previous(cause_id){if(id)cause_id=id;}
    ~CauseScope(){cause_id=previous;}
};
inline bool enabled(){return active || (bool)secondary || (bool)context_listener;}
inline const std::vector<std::string> &available_kinds() {
    static const std::vector<std::string> names={"all","trap","watch","liveness","callstream","tap","loadmap","snap","card","u2","artifact","console","command","stop","fault","audio","irq","device","scheduler","interrupt","context"};
    return names;
}
inline bool selected(const std::string &kind) {
    for(const auto &k:kinds)if(k=="all" || k==kind || kind.compare(0,k.size()+1,k+".")==0)return true;
    return false;
}
inline bool needs(const char *kind){return (bool)context_listener || (active && selected(kind)) || (secondary && secondary_selects && secondary_selects(kind));}
inline void control(const char *kind,const std::string &payload) {
    if(!sink)return;
    const bool previous=publishing;publishing=true;
    a2record::Record r("subscription",kind,a2record::frame_now(),a2record::cycles_now());
    r.str("schema","a2subscription-v1").u("sequence",sequence++).raw("payload",payload.c_str());
    auto line=r.done()+"\n";
    if(std::fwrite(line.data(),1,line.size(),sink)!=line.size() || std::fflush(sink)) {
        error="subscription-output-failed";
        std::fprintf(stderr,"A2GSPU SUBSCRIPTION: FAIL output=%s retained=%llu dropped=%llu\n",path.c_str(),(unsigned long long)retained,(unsigned long long)dropped);
    }
    publishing=previous;
}
inline uint64_t publish(const char *kind,const std::string &payload) {
    if(publishing)return 0;
    uint64_t identity=context_listener?context_listener(kind,payload):0;
    if(secondary){publishing=true;secondary(kind,payload);publishing=false;}
    if(!active || publishing || !selected(kind))return identity;
    if(!error.empty() || retained>=limit) {
        ++dropped;
        if(dropped==1 && error.empty())control("overflow","{\"reason\":\"record-limit\",\"limit\":"+std::to_string(limit)+"}");
        return identity;
    }
    control("event","{\"kind\":"+a2ctrljson::quote(kind)+",\"clock_available\":"+
        std::string(a2record::g_cycles_fn?"true":"false")+",\"data\":"+payload+"}");
    if(error.empty())++retained;else ++dropped;
    return identity;
}
inline void observe(const char *rail,const std::string &record) {publish(rail,record);}
inline void set_secondary(std::function<void(const char*,const std::string&)> listener,std::function<bool(const char*)> selects={}) {
    secondary=std::move(listener);secondary_selects=std::move(selects);a2record::g_observer=enabled()?observe:nullptr;
}
inline void set_context(std::function<uint64_t(const char*,const std::string&)> listener) {
    context_listener=std::move(listener);a2record::g_observer=enabled()?observe:nullptr;
}
inline bool context_command(const char *command) {return !std::strncmp(command,"context",7) && (!command[7] || command[7]==' ');}
inline void begin_command(const char *command) {
    cause_id=0;
    if(context_listener && !context_command(command))cause_id=context_listener("command.begin","{\"command\":"+a2ctrljson::quote(command)+"}");
}
inline void close_with_ops(a2artifact::WriteOps ops={}) {
    if(sink) {
        control("stopped","{\"retained\":"+std::to_string(retained)+",\"dropped\":"+std::to_string(dropped)+"}");
        try {a2artifact::finish(sink,path.c_str(),ops);}
        catch(const a2artifact::Error &failure){error=std::string("subscription-finalization-failed: ")+failure.what();std::fprintf(stderr,"A2GSPU SUBSCRIPTION: FAIL %s\n",error.c_str());}
    }
    active=false;a2record::g_observer=enabled()?observe:nullptr;
}
inline void close(){close_with_ops();}
inline bool open(const std::vector<std::string> &filters,const std::string &file,uint64_t cap,std::string &why) {
    if(active){why="subscription-already-active; turn it off first";return false;}
    if(file.rfind("\\\\.\\",0)==0 || file.rfind("\\\\?\\",0)==0){why="subscription requires an ordinary file";return false;}
    std::error_code ec;
    if(std::filesystem::exists(file,ec) && !std::filesystem::is_regular_file(file,ec)){why="subscription requires an ordinary file";return false;}
    FILE *f=std::fopen(file.c_str(),"wb");if(!f){why="cannot open subscription file";return false;}
    sink=f;path=file;kinds=filters;limit=cap;retained=dropped=sequence=0;error.clear();active=true;
    a2record::g_observer=observe;
    control("started","{\"limit\":"+std::to_string(limit)+",\"coverage\":\"a2record rails, commands/stops, IRQ edges, audio captures, SCC/WDM console and command/frame-sampled device fields; transient device changes may be missed\"}");
    if(!error.empty()){why=error;close();return false;}
    static bool registered=false;if(!registered){std::atexit(close);registered=true;}
    return true;
}
inline std::string status() {
    return "{\"schema\":\"a2subscription-status-v1\",\"active\":"+std::string(active?"true":"false")+
        ",\"complete\":"+std::string(!dropped && error.empty()?"true":"false")+
        ",\"retained\":"+a2ctrljson::quote(std::to_string(retained))+",\"dropped\":"+a2ctrljson::quote(std::to_string(dropped))+
        ",\"path\":"+a2ctrljson::quote(path)+",\"error\":"+(error.empty()?"null":a2ctrljson::quote(error))+"}";
}
inline void completed(const char *command,const char *result) {
    if(context_command(command)){cause_id=0;return;}
    if(!enabled())return;
    char json[16384];a2ctrljson::format(json,sizeof json,command,result);
    publish("command",json);
    char status[24];a2ctrljson::status_word(result,status,sizeof status);
    if(!std::strcmp(status,"HALT") || !std::strcmp(status,"HANG"))publish("stop",json);
    cause_id=0;
}
inline void byte(const char *source,uint8_t value) {
    if(enabled())publish("console","{\"source\":"+a2ctrljson::quote(source)+",\"bytes\":["+std::to_string(value)+"]}");
}
}
