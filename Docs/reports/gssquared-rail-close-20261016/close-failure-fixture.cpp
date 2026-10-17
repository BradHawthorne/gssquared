#include "a2gspu_liveness.hpp"
#include "a2gspu_trap.hpp"
#include "devices/adb/ADB_Micro.hpp"
#include "wiz5_config.hpp"
#include <iostream>
uint64_t debug_level=0;
FILE *bad(const char *path){FILE *f=fopen(path,"rb");if(!f)throw std::runtime_error("fixture open");fputc('x',f);return f;}
int main(int argc,char **argv) {
    if(argc!=2)return 3;int failures=0;
    auto check=[&](auto close,const char *name){bool threw=false;try {close();}catch(const a2artifact::Error &){threw=true;}std::cout<<(threw?"PASS ":"FAIL ")<<name<<" latched stream error\n";if(!threw)++failures;};
    g_liveness_armed=true;g_liveness_out=bad(argv[1]);check(a2gspu_liveness_close,"liveness");
    a2trap::Trap trap{};trap.name="fixture";trap.own_out=true;trap.out=bad(argv[1]);trap.out_path=argv[1];a2trap::g_traps.push_back(std::move(trap));check(a2trap::close_all,"trap");a2trap::g_traps.clear();
    g_wiz5_ec19_out=bad(argv[1]);check(wiz5_config_close,"legacy title sink");
    return failures?1:0;
}
