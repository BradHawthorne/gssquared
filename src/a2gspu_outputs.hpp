#pragma once
#include "a2gspu_artifact_io.hpp"
#include <algorithm>
#include <vector>

// Machine-thread owned outputs. Stable global FILE slots use track(); containers
// with movable elements register a finalizer instead. Nothing is armed by default.
namespace a2outputs {
struct Slot {FILE **file;std::string path;};
struct Finalizer {const char *name;void (*close)();};
inline std::vector<Slot> slots;
inline std::vector<Finalizer> finalizers;
inline void finish_all();
inline void arm_exit() {
    static bool armed=false;
    if(!armed){armed=true;std::atexit([]{try {finish_all();}catch(const a2artifact::Error &error){a2artifact::fatal_on_exit(error);}});}
}
inline void track(FILE *&file,const char *path) {
    if(!file)return;
    arm_exit();
    auto entry=std::find_if(slots.begin(),slots.end(),[&](const Slot &s){return s.file==&file;});
    if(entry==slots.end())slots.push_back({&file,path?path:"output"});else entry->path=path?path:"output";
}
inline void add(const char *name,void (*close)()) {
    arm_exit();
    if(std::none_of(finalizers.begin(),finalizers.end(),[&](const Finalizer &f){return f.close==close;}))finalizers.push_back({name,close});
}
inline void finish(FILE *&file,const char *fallback="output",a2artifact::WriteOps ops={}) {
    std::string path=fallback;
    auto entry=std::find_if(slots.begin(),slots.end(),[&](const Slot &s){return s.file==&file;});
    if(entry!=slots.end()){path=entry->path;slots.erase(entry);}
    a2artifact::finish(file,path.c_str(),ops);
}
inline void finish_all() {
    std::string failures;
    auto attempt=[&](auto close){try {close();}catch(const a2artifact::Error &error){if(!failures.empty())failures+="; ";failures+=error.what();std::fprintf(stderr,"A2GSPU OUTPUT: FAIL %s\n",error.what());}};
    while(!finalizers.empty()){auto entry=finalizers.back();finalizers.pop_back();attempt(entry.close);}
    while(!slots.empty()){auto entry=slots.back();slots.pop_back();attempt([&]{a2artifact::finish(*entry.file,entry.path.c_str());});}
    if(!failures.empty())throw a2artifact::Error("output-finalization",failures.c_str(),0);
}
}
