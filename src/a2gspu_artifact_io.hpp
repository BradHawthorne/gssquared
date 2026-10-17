#pragma once
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace a2artifact {
class Error : public std::runtime_error {
public:
    Error(const char *operation,const char *path,int code=errno)
        :std::runtime_error(std::string(operation)+" '"+(path?path:"")+"'"+(code?std::string(": ")+std::strerror(code):"")){}
};

// Explicit injection for native contract tests; production always uses stdio.
// No environment variable or runtime fault knob changes these operations.
struct WriteOps {
    size_t (*write)(const void*,size_t,size_t,FILE*)=std::fwrite;
    int (*flush)(FILE*)=std::fflush;
    int (*close)(FILE*)=std::fclose;
};

// Consume an existing owned stream even when flushing or closing fails.
inline void finish(FILE *&file,const char *path,WriteOps ops={}) {
    if(!file)return;
    FILE *owned=file;file=nullptr;
    errno=0;const bool flush_failed=ops.flush(owned)!=0 || std::ferror(owned)!=0;
    const int flush_error=errno;
    errno=0;const bool close_failed=ops.close(owned)!=0;
    const int close_error=errno;
    if(flush_failed || close_failed)throw Error(flush_failed?"flush":"close",path,flush_failed?flush_error:close_error);
}

class Output {
    std::string path_;
    WriteOps ops_;
    FILE *file_=nullptr;
public:
    explicit Output(const char *path,WriteOps ops={}):path_(path?path:""),ops_(ops) {
        errno=0;file_=path && *path?std::fopen(path,"wb"):nullptr;
        if(!file_)throw Error("open",path);
    }
    Output(const Output&)=delete;
    Output& operator=(const Output&)=delete;
    ~Output(){if(file_)ops_.close(file_);}
    FILE *get() const {return file_;}
    void write(const void *data,size_t bytes) {
        if(!file_ || (bytes && !data))throw Error("invalid-write",path_.c_str(),0);
        errno=0;
        if(bytes && ops_.write(data,1,bytes,file_)!=bytes)throw Error("write",path_.c_str());
    }
    void finish() {
        a2artifact::finish(file_,path_.c_str(),ops_);
    }
};

inline void write_file(const char *path,const void *data,size_t bytes) {
    Output out(path);out.write(data,bytes);out.finish();
}

// Preserve an existing fopen/refusal branch while owning its successful stream.
// The local FILE view is nulled on finish, including failure; early returns close.
class ExistingOutput {
    FILE *&file_;
    std::string path_;
public:
    ExistingOutput(FILE *&file,const char *path):file_(file),path_(path?path:""){}
    ExistingOutput(const ExistingOutput&)=delete;
    ExistingOutput& operator=(const ExistingOutput&)=delete;
    ~ExistingOutput(){FILE *owned=file_;file_=nullptr;if(owned)std::fclose(owned);}
    void finish(){if(!file_)throw Error("no-open-output",path_.c_str(),0);a2artifact::finish(file_,path_.c_str());}
};

[[noreturn]] inline void fatal(const Error &error) {
    std::fprintf(stderr,"A2GSPU ARTIFACT: status=FAIL artifact-io %s\n",error.what());
    std::exit(2);
}

// atexit cannot throw or recursively call exit. Normal drivers close explicitly;
// this fallback still refuses a zero process exit if late finalization fails.
[[noreturn]] inline void fatal_on_exit(const Error &error) {
    std::fprintf(stderr,"A2GSPU ARTIFACT: status=FAIL late-output-io %s\n",error.what());
    std::fflush(nullptr);
    std::_Exit(2);
}
}
