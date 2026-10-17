#include "debugger/trace.hpp"
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc,char **argv) {
    if(argc!=2)return 3;
    const std::filesystem::path root=argv[1];
    std::filesystem::create_directories(root);
    system_trace_entry_t records[2];std::memset(records,0,sizeof records);
    records[0].cycle=0x0a0d1a;records[0].opcode=0xea;records[0].pc=0x1234;
    records[1].cycle=0x0d0a;records[1].opcode=0xea;records[1].pc=0x5678;
    a2artifact::write_file((root/"valid.bin").string().c_str(),records,sizeof records);
    a2artifact::write_file((root/"empty.bin").string().c_str(),nullptr,0);
    a2artifact::write_file((root/"partial.bin").string().c_str(),records,sizeof records-1);
    a2artifact::write_file((root/"labels.lbl").string().c_str(),"al 001234 .start\n",17);
    // 100000 physical slots means at most 99999 retained ring records.
    a2artifact::Output large((root/"large.bin").string().c_str());
    for(unsigned i=0;i<100000;++i)large.write(records,sizeof records[0]);
    large.finish();
    system_trace_buffer trace(4,PROCESSOR_65816);
    system_trace_entry_t sentinel{};sentinel.pc=0xbeef;trace.add_entry(sentinel);
    trace.read_from_file((root/"valid.bin").string());
    const bool exact=std::memcmp(trace.entries,records,sizeof records)==0;
    std::cout<<"loaded_count="<<trace.count<<" expected=2 exact_binary="<<exact<<'\n';
    return 0;
}
