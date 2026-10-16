#pragma once
// Generated samples before host resampling. Bounded, explicit timing/loss.
#include "../a2gspu_ctrl_json.hpp"
#include "../a2record.hpp"
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <cstdint>
#include <algorithm>
namespace audio_capture {
inline std::ofstream stream;
inline int source=-1;
inline uint64_t frames=0,dropped=0,limit=0,batches=0;
inline std::string error,path;
inline bool active=false;
inline std::string status() {
    return "{\"schema\":\"a2audio-capture-status-v1\",\"active\":"+std::string(active?"true":"false")+
        ",\"source\":"+a2ctrljson::quote(source==0?"doc":source==1?"sfx":"none")+",\"frames\":"+a2ctrljson::quote(std::to_string(frames))+
        ",\"dropped\":"+a2ctrljson::quote(std::to_string(dropped))+",\"complete\":"+(!error.empty()||dropped?"false":"true")+
        ",\"error\":"+a2ctrljson::quote(error)+",\"path\":"+a2ctrljson::quote(path)+"}";
}
inline void write(const std::string &text) {
    stream<<text<<'\n';stream.flush();if(!stream){error="capture-write-failed";active=false;}
}
inline void start(int which,const std::string &file,uint64_t max_frames) {
    if(active || stream.is_open())throw std::runtime_error("audio capture already open; stop it first");
    if(which<0 || which>1 || !max_frames || max_frames>1000000)throw std::runtime_error("invalid audio source/frame cap");
    std::error_code ec;
    if(std::filesystem::exists(file,ec) && !std::filesystem::is_regular_file(file,ec))throw std::runtime_error("audio capture requires an ordinary file");
    if(ec)throw std::runtime_error("cannot inspect audio capture path");
    stream.clear();stream.open(file,std::ios::binary|std::ios::trunc);
    if(!stream)throw std::runtime_error("cannot open audio capture");
    path=file;source=which;limit=max_frames;frames=dropped=batches=0;error.clear();active=true;
    a2record::Record header("audio","started",a2record::frame_now(),a2record::cycles_now());
    header.str("schema","a2audio-v1").str("source",which==0?"doc":"sfx").str("model","generated-source-before-host-resampling").str("format","signed-int16-interleaved");
    write(header.done());
    if(!active){stream.close();throw std::runtime_error(error);}
}
inline void stop() {
    if(!stream.is_open())throw std::runtime_error("no audio capture open");
    active=false;
    if(stream){a2record::Record end("audio","stopped",a2record::frame_now(),a2record::cycles_now());end.str("schema","a2audio-v1").raw("status",status().c_str());write(end.done());}
    stream.close();if(stream.fail() && error.empty())error="capture-close-failed";
}
inline void unread(int src) {if(active && source==src){error="unsupported-audio-batch";active=false;}}
inline void note(int src,const int16_t *data,uint32_t count,int channels,uint64_t end_tick=0,uint32_t ticks_per_sample=0,uint32_t rate=0) {
    if(!active || src!=source)return;
    if(!data || channels<1 || channels>2){unread(src);return;}
    uint64_t take=std::min<uint64_t>(count,limit-frames);dropped+=count-take;
    if(take) {
        a2record::Record row("audio","samples",a2record::frame_now(),a2record::cycles_now());
        row.str("schema","a2audio-v1").u("sequence",batches++).str("frame_offset",std::to_string(frames).c_str()).u("channels",channels).u("frames",take);
        if(ticks_per_sample)row.str("end_tick",std::to_string(end_tick-(count-take)*ticks_per_sample).c_str()).u("ticks_per_sample",ticks_per_sample).str("tick_domain","c14m");
        else row.null("end_tick").null("ticks_per_sample").null("tick_domain");
        if(rate)row.u("sample_rate_hz",rate);else row.null("sample_rate_hz");
        std::string values="[";for(size_t i=0;i<take*(size_t)channels;i++){if(i)values+=',';values+=std::to_string(data[i]);}values+=']';
        row.raw("samples",values.c_str());write(row.done());frames+=take;
    }
    if(dropped){error="frame-cap-exceeded";active=false;}
}
}
