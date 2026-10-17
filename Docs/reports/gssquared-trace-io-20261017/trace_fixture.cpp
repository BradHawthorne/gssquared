#include "debugger/trace.hpp"
#include "a2gspu_artifact_io.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

// Identical real-stdio fixture for the baseline and candidate implementations.
int main(int argc,char **argv) {
    if(argc!=3)return 3;
    const std::filesystem::path path=argv[1];
    const bool blocked=std::string(argv[2])=="blocked";
    if(blocked)std::filesystem::create_directory(path);
    system_trace_buffer trace(4,PROCESSOR_65816);
    system_trace_entry_t entry;
    std::memset(&entry,0,sizeof entry);
    entry.cycle=0x1a0d0a;entry.opcode=0x0a;entry.pc=0x0a1a;
    trace.add_entry(entry);
    try {trace.save_to_file(path.string());}
    catch(const a2artifact::Error &error) {
        std::cerr<<"refused: "<<error.what()<<'\n';return 2;
    }
    std::cout<<"save returned success\n";
    if(!blocked) {
        std::ifstream input(path,std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(input)),{});
        const bool exact=bytes.size()==sizeof entry && std::memcmp(bytes.data(),&entry,sizeof entry)==0;
        std::cout<<"exact_binary="<<exact<<" bytes="<<bytes.size()<<" expected="<<sizeof entry<<'\n';
    }
    return 0;
}
