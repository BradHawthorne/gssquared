#pragma once
#include "../../src/debugger/trace.hpp"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace trace_input_checks {
inline void make_fixtures(const std::filesystem::path &root) {
    if(!std::filesystem::create_directory(root))throw std::runtime_error("trace input fixture directory exists");
    system_trace_entry_t records[2];std::memset(records,0,sizeof records);
    records[0].cycle=0x0a0d1a;records[0].pc=0x1234;records[0].opcode=0xea;
    records[1].cycle=0x0d0a;records[1].pc=0x5678;records[1].opcode=0xea;
    auto write=[&](const char *name,const void *data,size_t bytes){a2artifact::write_file((root/name).string().c_str(),data,bytes);};
    write("valid.bin",records,sizeof records);write("empty.bin",nullptr,0);
    write("partial.bin",records,sizeof records-1);
    const char labels[]="al 001234 .start\n";write("labels.lbl",labels,sizeof labels-1);
    a2artifact::Output maximum((root/"maximum.bin").string().c_str());
    for(unsigned i=0;i<99999;++i)maximum.write(records,sizeof records[0]);
    maximum.finish();
    a2artifact::Output large((root/"large.bin").string().c_str());
    for(unsigned i=0;i<100000;++i)large.write(records,sizeof records[0]);
    large.finish();
}
inline unsigned closes=0,reads=0;
inline int counted_close(FILE *file){++closes;return std::fclose(file);}
inline int failed_close(FILE *file){++closes;std::fclose(file);errno=EIO;return EOF;}
inline size_t short_read(void*,size_t,size_t,FILE*){errno=EIO;return 0;}
inline size_t latched_read(void*,size_t,size_t,FILE *file){std::fwrite("x",1,1,file);return 0;}
inline size_t failed_probe(void *data,size_t size,size_t count,FILE *file){
    if(++reads==1)return std::fread(data,size,count,file);
    errno=EIO;return 0;
}
inline int run() {
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("a2trace-input-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(root))throw std::runtime_error("trace input fixture directory exists");
    struct Cleanup {
        fs::path root,parent;
        ~Cleanup(){if(root.parent_path()==parent && root.filename().string().rfind("a2trace-input-",0)==0){std::error_code error;fs::remove_all(root,error);}}
    } cleanup{root,parent};
    int failures=0;
    auto check=[&](bool pass,const char *label){std::cout<<(pass?"PASS ":"FAIL ")<<label<<'\n';if(!pass)++failures;};
    const auto path=(root/"trace.bin").string(),exported=(root/"export.bin").string();
    system_trace_entry_t records[4];std::memset(records,0,sizeof records);
    for(unsigned i=0;i<4;++i){records[i].cycle=0x0a0d1a+i;records[i].pc=0x1234+i;records[i].opcode=0xea;}
    auto write=[&](size_t bytes){a2artifact::write_file(path.c_str(),records,bytes);};
    auto bytes=[](const std::string &name){std::ifstream file(name,std::ios::binary);return std::vector<char>((std::istreambuf_iterator<char>(file)),{});};
    for(auto cpu:{PROCESSOR_6502,PROCESSOR_65C02,PROCESSOR_65816}) {
        system_trace_buffer trace(4,cpu);trace.add_entry(records[3]);
        write(2*sizeof records[0]);closes=0;
        trace.read_from_file(path,{std::fread,counted_close});
        check(trace.count==2 && trace.head==2 && trace.tail==0 && closes==1 && std::memcmp(trace.entries,records,2*sizeof records[0])==0,
              "trace input: exact binary bytes replace ring and publish loaded count");
        trace.add_entry(records[2]);trace.save_to_file(exported);
        auto actual=bytes(exported);
        check(actual.size()==3*sizeof records[0] && std::memcmp(actual.data(),records,actual.size())==0,
              "trace input: append and re-export preserve order at usable capacity");
        trace.add_entry(records[3]);trace.save_to_file(path);
        system_trace_buffer wrapped(4,cpu);wrapped.read_from_file(path);
        check(wrapped.count==3 && std::memcmp(wrapped.entries,records+1,3*sizeof records[0])==0,
              "trace input: append after capacity and wrapped reload preserve newest records");
        write(0);trace.read_from_file(path);
        check(trace.count==0 && trace.head==0 && trace.tail==0,"trace input: empty file replaces old trace without phantom records");
    }
    system_trace_buffer trace(4,PROCESSOR_65816);
    std::memset(trace.entries,0xa5,4*sizeof records[0]);
    trace.add_entry(records[3]);
    const auto original=std::vector<char>(reinterpret_cast<char*>(trace.entries),reinterpret_cast<char*>(trace.entries)+4*sizeof records[0]);
    auto unchanged=[&](){return trace.count==1 && trace.head==1 && trace.tail==0 && std::memcmp(trace.entries,original.data(),original.size())==0;};
    auto refused=[&](const std::string &name,trace_read_ops ops,const char *operation,const char *label){
        std::string error;closes=0;
        try{trace.read_from_file(name,ops);}catch(const a2artifact::Error &e){error=e.what();}
        check(!error.empty() && (!operation || error.rfind(operation,0)==0) && error.find(name)!=std::string::npos && unchanged(),label);
    };
    refused((root/"missing.bin").string(),{},"open","trace input: missing file refuses without mutation");
    refused(root.string(),{},nullptr,"trace input: directory refuses without mutation");
    write(2*sizeof records[0]-1);
    refused(path,{},"partial-record","trace input: partial record refuses without mutation");
    write(sizeof records);
    refused(path,{},"capacity-exceeded","trace input: oversized input refuses instead of truncating");
    write(3*sizeof records[0]+1);
    refused(path,{},"capacity-exceeded","trace input: one byte beyond capacity refuses");
    write(2*sizeof records[0]);
    refused(path,{short_read,counted_close},"read","trace input: short read without EOF refuses without mutation");
    check(closes==1,"trace input: short read releases input exactly once");
    refused(path,{latched_read,counted_close},"read","trace input: latched real stdio error refuses without mutation");
    check(closes==1,"trace input: latched read error releases input exactly once");
    refused(path,{std::fread,failed_close},"close","trace input: failed close refuses before publishing loaded state");
    check(closes==1,"trace input: failed close consumes handle without double close");
    write(0);
    refused(path,{std::fread,failed_close},"close","trace input: empty file still checks close before replacing state");
    write(3*sizeof records[0]);reads=0;
    refused(path,{failed_probe,counted_close},"read","trace input: failed EOF probe refuses a plausible complete prefix");
    check(reads==2 && closes==1,"trace input: EOF probe failure releases input exactly once");
    trace.read_from_file(path);
    check(trace.count==3 && std::memcmp(trace.entries,records,3*sizeof records[0])==0,"trace input: retry after refusal can load retained source");
    system_trace_buffer tiny(1,PROCESSOR_6502);write(0);tiny.read_from_file(path);
    check(tiny.count==0 && tiny.head==0 && tiny.tail==0,"trace input: zero usable capacity accepts an empty input");
    write(sizeof records[0]);bool failed=false;
    try{tiny.read_from_file(path);}catch(const a2artifact::Error &){failed=true;}
    check(failed && tiny.count==0,"trace input: zero usable capacity refuses a record");
    return failures;
}
}
