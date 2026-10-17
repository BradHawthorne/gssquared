#include "a2gspu_outputs.hpp"
#include "a2gspu_output.hpp"
#include <string>

int main(int argc,char **argv) {
    if(argc!=3)return 64;
    A2ByteSink sink;
    sink.open_checked("fixture",("file:"+std::string(argv[1])).c_str());
    std::fclose(sink.file);sink.file=std::fopen(argv[1],"rb");
    if(!sink.file || std::fputc('x',sink.file)!=EOF || !std::ferror(sink.file))return 65;
    const std::string mode=argv[2];
    if(mode=="late")std::exit(0);
    if(mode=="destructor")return 0;
    bool refused=false;
    try {if(mode=="shutdown")a2outputs::finish_all();else sink.close();}
    catch(const a2artifact::Error &){refused=true;}
    if(!refused){std::fprintf(stderr,"FAIL: capture finalization returned success after latched I/O error\n");return 1;}
    std::fprintf(stderr,"PASS: capture finalization refused latched I/O error\n");
    return 0;
}
