#pragma once
#include "../../src/debugger/trace.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace trace_io_checks {
inline unsigned closes=0;
inline int counted_close(FILE *file){++closes;return std::fclose(file);}
inline int failed_close(FILE *file){++closes;std::fclose(file);errno=EIO;return EOF;}
inline int failed_flush(FILE *){errno=ENOSPC;return EOF;}
inline size_t short_write(const void *data,size_t size,size_t count,FILE *file){
    auto n=std::fwrite(data,size,count?count-1:0,file);errno=ENOSPC;return n;
}
inline int run() {
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("a2trace-io-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(root))throw std::runtime_error("trace fixture directory exists");
    struct Cleanup {
        fs::path root,parent;
        ~Cleanup(){if(root.parent_path()==parent && root.filename().string().rfind("a2trace-io-",0)==0){std::error_code error;fs::remove_all(root,error);}}
    } cleanup{root,parent};
    int failures=0;
    auto check=[&](bool pass,const char *label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<'\n';if(!pass)++failures;};
    auto bytes=[](const fs::path &path){std::ifstream input(path,std::ios::binary);return std::vector<char>((std::istreambuf_iterator<char>(input)),{});};
    const auto target=(root/"trace.bin").string();
    for(auto cpu:{PROCESSOR_6502,PROCESSOR_65C02,PROCESSOR_65816}) {
        system_trace_buffer trace(4,cpu);
        trace.save_to_file(target);
        check(fs::file_size(target)==0,"legacy trace: empty ring produces a valid empty binary");
        std::vector<char> expected;
        for(unsigned i=0;i<6;++i) {
            system_trace_entry_t entry;std::memset(&entry,0,sizeof entry);
            entry.cycle=0x1a0d0a+i;entry.pc=0x0a1a;entry.opcode=0x0a;entry.db=0x1a;
            const auto *raw=reinterpret_cast<const char*>(&entry);
            expected.insert(expected.end(),raw,raw+sizeof entry);
            trace.add_entry(entry);
            if(i==1) {
                trace.save_to_file(target);
                check(bytes(target)==expected,"legacy trace: non-wrapped records preserve all binary bytes and order");
            }
        }
        // The existing ring deliberately reserves one slot: capacity four keeps
        // the newest three records. Pin that behavior rather than redefining it.
        expected.erase(expected.begin(),expected.begin()+3*sizeof(system_trace_entry_t));
        const auto head=trace.head,tail=trace.tail,count=trace.count;
        trace.save_to_file(target);
        check(bytes(target)==expected,"legacy trace: wrapped records export oldest first without stale slots");
        check(trace.head==head && trace.tail==tail && trace.count==count,"legacy trace: export leaves ring state unchanged");
    }
    system_trace_buffer trace(4,PROCESSOR_65816);
    system_trace_entry_t entry{};entry.opcode=0xea;trace.add_entry(entry);
    for(int failure=0;failure<3;++failure) {
        a2artifact::WriteOps ops;ops.close=failure==2?failed_close:counted_close;
        if(failure==0)ops.write=short_write;
        if(failure==1)ops.flush=failed_flush;
        closes=0;std::string error;
        try{trace.save_to_file(target,ops);}catch(const a2artifact::Error &e){error=e.what();}
        const char *operation=failure==0?"write":failure==1?"flush":"close";
        check(error.rfind(operation,0)==0 && error.find(target)!=std::string::npos && closes==1,
              failure==0?"legacy trace: short write refuses and closes once":failure==1?"legacy trace: flush failure refuses and closes once":"legacy trace: close failure refuses without double close");
    }
    system_trace_buffer empty(4,PROCESSOR_6502);
    for(int failure=0;failure<2;++failure) {
        a2artifact::WriteOps ops;ops.close=failure?failed_close:counted_close;
        if(!failure)ops.flush=failed_flush;
        closes=0;bool refused=false;
        try{empty.save_to_file(target,ops);}catch(const a2artifact::Error &){refused=true;}
        check(refused && closes==1,"legacy trace: empty export still checks finalization");
    }
    for(const auto &blocked:{root.string(),(root/"missing"/"trace.bin").string()}) {
        bool refused=false;
        try{trace.save_to_file(blocked);}catch(const a2artifact::Error &e){refused=std::string(e.what()).find(blocked)!=std::string::npos;}
        check(refused,"legacy trace: failed open refuses with destination context");
    }
    trace.save_to_file(target);
    const auto actual=bytes(target);
    check(actual.size()==sizeof entry && std::memcmp(actual.data(),&entry,sizeof entry)==0,"legacy trace: retry after failure exports retained records");
    return failures;
}
}
