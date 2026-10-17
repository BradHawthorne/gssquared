#pragma once
#include "artifact_io_checks.hpp"
#include "../../src/a2gspu_output.hpp"
#include <fstream>

namespace byte_close_checks {
inline int pipe_closes=0;
inline bool pipe_close_failed(A2PipeHandle pipe) {
    ++pipe_closes;a2_close_pipe(pipe);
#ifdef _WIN32
    SetLastError(ERROR_WRITE_FAULT);
#else
    errno=EIO;
#endif
    return false;
}
inline int run() {
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("a2byteclose-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(root))throw std::runtime_error("byte close fixture exists");
    struct Cleanup {fs::path root,parent;~Cleanup(){if(root.parent_path()==parent && root.filename().string().rfind("a2byteclose-",0)==0){std::error_code error;fs::remove_all(root,error);}}} cleanup{root,parent};
    int failures=0;auto check=[&](bool value,const char *label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';if(!value)++failures;};
    auto path=[&](const char *name){return (root/name).string();};
    auto read=[&](const char *name){std::ifstream stream(root/name,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});};
    auto refuses=[&](auto fn){try {fn();}catch(const a2artifact::Error &){return true;}return false;};
    auto arm=[&](A2ByteSink &sink,const char *name){sink.open_checked(name,("file:"+path(name)).c_str());};
    auto bad=[&](A2ByteSink &sink,const char *name){arm(sink,name);std::fclose(sink.file);sink.file=std::fopen(path(name).c_str(),"rb");if(!sink.file || std::fputc('x',sink.file)!=EOF || !std::ferror(sink.file))throw std::runtime_error("latched capture error fixture");};
    A2ByteSink unarmed;unarmed.close();check(a2_byte_sinks.empty(),"unarmed byte capture owns no output");
    {
        A2ByteSink a,b,wdm;arm(a,"SCC_A");arm(b,"SCC_B");arm(wdm,"WDM");
        a.byte('a');b.byte('b');wdm.byte('w');
        check(a2_byte_sinks.size()==3,"all byte captures register with managed shutdown");
        a2outputs::finish_all();
        check(!a.enabled && !b.enabled && !wdm.enabled && a2_byte_sinks.empty(),"managed close consumes all byte captures");
        check(read("SCC_A")=="a" && read("SCC_B")=="b" && read("WDM")=="w","managed close preserves independent exact capture bytes");
    }
    {
        A2ByteSink empty;arm(empty,"empty");empty.close();
        check(fs::file_size(root/"empty")==0,"zero-byte capture closes successfully");
    }
    for(int mode=0;mode<2;++mode) {
        A2ByteSink sink;arm(sink,"injected");sink.byte('x');
        A2ByteCloseOps ops;artifact_io_checks::closed_files=0;
        ops.stdio.close=mode?artifact_io_checks::close_failed:artifact_io_checks::close_counted;
        if(!mode)ops.stdio.flush=artifact_io_checks::flush_failed;
        check(refuses([&]{sink.close(ops);}),mode?"byte capture refuses injected fclose failure":"byte capture refuses injected flush failure");
        check(!sink.enabled && !sink.file && !sink.reserved && sink.binding.empty() && a2_byte_sinks.empty() && a2_sink_paths.empty(),"failed byte close releases state and endpoint reservation");
        sink.close(ops);check(artifact_io_checks::closed_files==1,"failed byte close is consumed exactly once");
        arm(sink,"injected");check(sink.bytes==0,"failed binding can be armed again with a fresh counter");sink.close();
    }
    {
        A2ByteSink failed;bad(failed,"latched");
        check(refuses([&]{failed.close();}),"byte close refuses a real latched stdio error");
        check(!failed.enabled && a2_byte_sinks.empty(),"latched-error close leaves capture disabled");
    }
    {
        A2ByteSink first,second,healthy;bad(first,"bad-first");bad(second,"bad-second");arm(healthy,"healthy");healthy.byte('h');
        check(refuses(a2outputs::finish_all),"managed close reports aggregate byte-capture failures");
        check(!first.file && !second.file && !healthy.file && a2_byte_sinks.empty() && a2_sink_paths.empty(),"failed captures do not skip other captures during shutdown");
        check(read("healthy")=="h","healthy capture completes beside failed captures");
        check(!refuses(a2outputs::finish_all),"repeated managed cleanup does not retry consumed failures");
    }
    {
        A2ByteSink sink;arm(sink,"preserved");sink.byte('p');bool refused=false;
        try {sink.open_checked("replacement",("file:"+path("replacement")).c_str());}catch(const std::runtime_error &){refused=true;}
        check(refused && sink.enabled && a2_byte_sinks.size()==1,"rebind refusal preserves the already armed capture");
        sink.byte('q');sink.close();check(read("preserved")=="pq" && !fs::exists(root/"replacement"),"rebind refusal preserves bytes and creates no replacement");
    }
    for(int failure=0;failure<2;++failure) {
        A2ByteSink sink;
#ifdef _WIN32
        HANDLE reader=INVALID_HANDLE_VALUE,writer=INVALID_HANDLE_VALUE;
        if(!CreatePipe(&reader,&writer,nullptr,0))throw std::runtime_error("pipe fixture creation");
#else
        int pair[2];if(::pipe(pair))throw std::runtime_error("pipe fixture creation");
        const int reader=pair[0],writer=pair[1];
#endif
        sink.pipe=writer;sink.enabled=true;sink.label="pipe-fixture";sink.binding="pipe:fixture";
        A2ByteCloseOps ops;pipe_closes=0;if(failure)ops.pipe_close=pipe_close_failed;
        const bool refused=refuses([&]{sink.close(ops);});a2_close_pipe(reader);
        check(refused==bool(failure),failure?"pipe close failure refuses":"valid pipe handle closes successfully");
#ifdef _WIN32
        const bool consumed=sink.pipe==INVALID_HANDLE_VALUE;
#else
        const bool consumed=sink.pipe<0;
#endif
        sink.close(ops);check(consumed && !sink.enabled && (!failure || pipe_closes==1),"pipe close consumes handle even when finalization fails");
    }
    return failures;
}
}
