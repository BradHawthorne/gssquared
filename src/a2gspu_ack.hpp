#pragma once
#include "a2gspu_artifact_io.hpp"
#include <filesystem>

namespace a2ack {
struct PublishOps {int (*rename)(const char*,const char*)=std::rename;};
inline void publish(const char *dir,int sequence,const char *result,a2artifact::WriteOps writes={},PublishOps publication={}) {
    const std::string base=std::string(dir)+"/ack."+std::to_string(sequence);
    const std::string temporary=base+".tmp";
    std::error_code error;
    if(std::filesystem::exists(base,error) || error)throw a2artifact::Error("ack-destination-exists-or-unreadable",base.c_str(),error.value());
    a2artifact::Output output(temporary.c_str(),writes);
    output.write(result,std::strlen(result));output.write("\n",1);output.finish();
    errno=0;
    if(publication.rename(temporary.c_str(),base.c_str())!=0)throw a2artifact::Error("ack-rename",base.c_str());
}
}
