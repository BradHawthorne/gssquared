#pragma once
#include "../../src/a2gspu_artifact_meta.hpp"
#include "../../src/bus_trace.hpp"
#include "../../src/devices/slot_bus/slot_bus.hpp"
#include "../../src/mmu_state_trace.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace artifact_io_checks {
inline int closed_files=0;
inline unsigned failed_record_events=0;
inline int close_counted(FILE *file){++closed_files;return std::fclose(file);}
inline int close_failed(FILE *file){++closed_files;std::fclose(file);errno=EIO;return EOF;}
inline int flush_failed(FILE *){errno=ENOSPC;return EOF;}
inline size_t write_short(const void *p,size_t size,size_t count,FILE *file){const auto n=std::fwrite(p,size,count?count-1:0,file);errno=ENOSPC;return n;}
inline void record_observed(const char *,const std::string &){++failed_record_events;}
inline int run() {
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("a2artifact-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(root))throw std::runtime_error("artifact fixture directory exists");
    struct Cleanup { fs::path root,parent; ~Cleanup(){if(root.parent_path()==parent && root.filename().string().rfind("a2artifact-",0)==0){std::error_code error;fs::remove_all(root,error);}} } cleanup{root,parent};
    int failures=0;
    auto check=[&](bool value,const char *label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';if(!value)++failures;};
    auto path=[&](const char *name){return (root/name).string();};
    g_bus_trace.clear();g_slot_bus.clear();g_mmu_state_trace.clear();
    uint64_t count=99;
    const auto empty=path("empty.bin");
    const auto hash=bus_trace_dump(empty.c_str(),&count);
    check(count==0 && hash==HOUSE_FNV_BASIS && fs::is_regular_file(empty) && fs::file_size(empty)==0,"real zero-record trace is valid");
    check(a2gspu_artifact_meta(empty.c_str(),"bus-trace-v1",sizeof(BusTraceRecord),0,"fixture","fixture",hash),"empty payload can receive a sidecar");
    const auto missing=path("missing.bin");
    check(!a2gspu_artifact_meta(missing.c_str(),"bus-trace-v1",sizeof(BusTraceRecord),0,"fixture","fixture",hash),"missing payload is not a zero-record capture");
    check(!fs::exists(missing+".meta.json"),"missing payload does not publish metadata");
    const auto mismatch=path("mismatch.bin");
    a2artifact::write_file(mismatch.c_str(),"abc",3);
    const auto sentinel=mismatch+".meta.json";
    a2artifact::write_file(sentinel.c_str(),"preserved",9);
    check(!a2gspu_artifact_meta(mismatch.c_str(),"bus-trace-v1",sizeof(BusTraceRecord),0,"fixture","fixture",hash),"wrong payload length refuses before sidecar publication");
    check(fs::file_size(sentinel)==9,"invalid payload does not truncate an existing sidecar");
    const auto blocked=path("blocked.bin");fs::create_directory(blocked);
    auto refused=[&](auto dump,const char *label){bool threw=false;count=99;try{dump(blocked.c_str(),&count);}catch(const std::exception&){threw=true;}check(threw && count==99,label);};
    refused(bus_trace_dump,"SHR dump refuses failed open without a valid count");
    refused(slot_bus_dump,"slot dump refuses failed open without a valid count");
    refused(mmu_state_trace_dump,"MMU dump refuses failed open without a valid count");
    const uint8_t bytes[]={0x12,0x34,0x56};
    for(int failure=0;failure<3;++failure) {
        a2artifact::WriteOps ops;ops.close=failure==2?close_failed:close_counted;
        if(failure==0)ops.write=write_short;
        if(failure==1)ops.flush=flush_failed;
        bool threw=false;closed_files=0;
        try {a2artifact::Output output(path("injected.bin").c_str(),ops);output.write(bytes,sizeof bytes);output.finish();}
        catch(const a2artifact::Error &){threw=true;}
        check(threw && closed_files==1,failure==0?"short write refuses and closes":failure==1?"flush failure refuses and closes":"close failure refuses without a double close");
    }
    for(int failure=0;failure<2;++failure) {
        a2artifact::WriteOps ops;
        if(failure==0)ops.flush=flush_failed;else ops.close=close_failed;
        check(!a2gspu_artifact_meta(empty.c_str(),"bus-trace-v1",sizeof(BusTraceRecord),0,"fixture","fixture",hash,-1,ops),"failed sidecar finalization refuses");
        check(!fs::exists(empty+".meta.json"),"failed sidecar is removed");
    }
    check(!a2gspu_artifact_meta(empty.c_str(),"bus-trace-v1",0,0,"fixture","fixture",hash),"zero record size refuses");
    check(!a2gspu_artifact_meta(empty.c_str(),"bus-trace-v1",2,UINT64_MAX,"fixture","fixture",hash),"record length overflow refuses");
    FILE *read_only=std::fopen(empty.c_str(),"rb");
    if(!read_only)throw std::runtime_error("cannot open read-only fixture");
    auto observer=a2record::g_observer;a2record::g_observer=record_observed;failed_record_events=0;
    a2record::Record record("artifact","failed-write",-1,0);
    const bool recorded=record.write_checked(read_only,true);
    a2record::g_observer=observer;std::fclose(read_only);
    check(!recorded && failed_record_events==0,"failed record write does not publish a successful observer event");
    return failures;
}
}
