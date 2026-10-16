#include "a2record.hpp"
#include "a2gspu_artifact_io.hpp"
#include <cstdio>
int main(int argc,char **argv) {
    if(argc==3) {
        a2record::arm();
        if(!a2record::g_global || !std::freopen(argv[1],"rb",a2record::g_global))return 3;
        std::fputc('x',a2record::g_global); // A real latched stdio error at exit.
        return 0;
    }
    if(argc!=2)return 3;
    FILE *f=std::fopen(argv[1],"rb");if(!f)return 3;
    bool refused=false;
    try {a2record::Record("fixture","failed-write",0,0).write(f);}
    catch(const a2artifact::Error &){refused=true;}
    std::fclose(f);
    std::puts(refused?"PASS void record writer refuses failed write":"FAIL void record writer silently discards failed write");
    return refused?0:1;
}
