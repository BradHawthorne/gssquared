#pragma once
#include "artifact_io_checks.hpp"
#include "../../src/a2gspu_coverage.hpp"
#include "../../src/a2gspu_png.hpp"
#include "../../src/a2gspu_events.hpp"

namespace sink_io_checks {
inline int run() {
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("a2sink-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(root))throw std::runtime_error("sink fixture exists");
    struct Cleanup {fs::path root,parent;~Cleanup(){if(root.parent_path()==parent && root.filename().string().rfind("a2sink-",0)==0){std::error_code error;fs::remove_all(root,error);}}} cleanup{root,parent};
    int failures=0;
    auto check=[&](bool value,const char *label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';if(!value)++failures;};
    auto path=[&](const char *name){return (root/name).string();};
    const auto empty=path("empty");a2artifact::write_file(empty.c_str(),nullptr,0);
    FILE *f=std::fopen(empty.c_str(),"rb");if(!f)throw std::runtime_error("read-only fixture");
    bool threw=false;try {a2record::Record("fixture","failed",0,0).write(f);}catch(const a2artifact::Error &){threw=true;}
    std::fclose(f);check(threw,"void record writer refuses failed write");
    a2record::g_global=std::fopen(empty.c_str(),"rb");a2record::g_global_path=empty;
    if(!a2record::g_global)throw std::runtime_error("global sink fixture");
    std::fputc('x',a2record::g_global);char status[512];a2record::format_status(status,sizeof status);
    check(std::string(status).find("status=FAIL records-incomplete")==0,"global readback refuses a latched stream error");
    threw=false;try {a2record::close();}catch(const a2artifact::Error &){threw=true;}
    check(threw && !a2record::g_global,"global close refuses a latched error and clears ownership");a2record::g_global_path.clear();
    for(int mode=0;mode<2;++mode) {
        FILE *owned=std::fopen(empty.c_str(),"wb");if(!owned)throw std::runtime_error("close fixture");
        a2artifact::WriteOps ops;ops.close=mode?artifact_io_checks::close_failed:artifact_io_checks::close_counted;
        if(!mode)ops.flush=artifact_io_checks::flush_failed;
        artifact_io_checks::closed_files=0;threw=false;
        try {a2artifact::finish(owned,"fixture",ops);}catch(const a2artifact::Error &){threw=true;}
        check(threw && !owned && artifact_io_checks::closed_files==1,"owned FILE close consumes pointer on finalization failure");
    }
    check(a2gspu_cov_init("000000-0000FF"),"coverage fixture arms");a2gspu_cov_counts_on();
    const uint8_t pixels[3]={12,34,56};
    for(int mode=0;mode<3;++mode) {
        a2artifact::WriteOps ops;
        if(mode==0)ops.write=artifact_io_checks::write_short;
        if(mode==1)ops.flush=artifact_io_checks::flush_failed;
        if(mode==2)ops.close=artifact_io_checks::close_failed;
        check(!a2gspu_cov_write(path("cov").c_str(),ops),"coverage refuses injected write/flush/close failure");
        // Profiles use fprintf; finalization faults exercise their buffered writes.
        if(mode)check(!a2gspu_cov_counts_write(path("profile").c_str(),ops),"profile refuses finalization failure");
        check(!a2png::write_gray(path("gray").c_str(),pixels,1,1,ops),"gray PNG refuses injected write/flush/close failure");
        check(!a2png::write_rgb(path("rgb").c_str(),pixels,1,1,ops),"RGB PNG refuses injected write/flush/close failure");
    }
    check(a2gspu_cov_write(path("valid-cov").c_str()),"coverage succeeds after refused output");
    check(a2gspu_cov_counts_write(path("valid-profile").c_str()),"profile succeeds after refused output");
    check(a2png::write_gray(path("valid-gray").c_str(),pixels,1,1) && a2png::write_rgb(path("valid-rgb").c_str(),pixels,1,1),"PNG writers succeed after refused output");
    for(int mode=0;mode<2;++mode) {
        std::string why;check(a2events::open({"all"},path("events"),10,why),"subscription opens before injected close fault");
        a2artifact::WriteOps ops;if(mode)ops.close=artifact_io_checks::close_failed;else ops.flush=artifact_io_checks::flush_failed;
        a2events::close_with_ops(ops);
        check(!a2events::active && !a2events::sink && !a2events::error.empty() && a2events::status().find("\"complete\":false")!=std::string::npos,"subscription refuses complete close receipt after finalization fault");
    }
    a2events::error.clear();
    a2gspu_cov_off();
    return failures;
}
}
