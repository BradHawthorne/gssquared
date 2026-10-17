#pragma once
#include "artifact_io_checks.hpp"
#include "../../src/a2gspu_ack.hpp"
#include "../../src/a2gspu_liveness.hpp"
#include "../../src/a2gspu_trap.hpp"
#include "../../src/devices/adb/ADB_Micro.hpp"
#include "../../src/wiz5_config.hpp"
#include <fstream>

namespace railclose_checks {
inline int callback_count=0;
inline void bad_callback(){++callback_count;throw a2artifact::Error("fixture-callback","failure",0);}
inline void good_callback(){++callback_count;}
inline int rename_failed(const char*,const char*){errno=EACCES;return -1;}
inline int run() {
    namespace fs=std::filesystem;
    const auto parent=fs::canonical(fs::temp_directory_path());
    const auto root=parent/("a2railclose-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if(!fs::create_directory(root))throw std::runtime_error("rail close fixture exists");
    struct Cleanup {fs::path root,parent;~Cleanup(){if(root.parent_path()==parent && root.filename().string().rfind("a2railclose-",0)==0){std::error_code error;fs::remove_all(root,error);}}} cleanup{root,parent};
    int failures=0;auto check=[&](bool value,const char *label){std::cout<<(value?"PASS ":"FAIL ")<<label<<'\n';if(!value)++failures;};
    auto path=[&](const char *name){return (root/name).string();};
    const auto empty=path("empty");a2artifact::write_file(empty.c_str(),nullptr,0);
    auto bad=[&](){FILE *f=fopen(empty.c_str(),"rb");if(!f)throw std::runtime_error("fixture open");fputc('x',f);return f;};
    auto refuses=[&](auto fn,const char *label){bool threw=false;try {fn();}catch(const a2artifact::Error &){threw=true;}check(threw,label);};
    g_liveness_armed=true;g_liveness_out=bad();refuses(a2gspu_liveness_close,"liveness close refuses latched I/O error");
    a2trap::Trap trap{};trap.name="fixture";trap.own_out=true;trap.out=bad();trap.out_path=empty;a2trap::g_traps.push_back(std::move(trap));
    refuses(a2trap::close_all,"trap close refuses latched I/O error");a2trap::g_traps.clear();
    g_wiz5_ec19_out=bad();refuses(wiz5_config_close,"legacy title close refuses latched I/O error");
    FILE *failed=bad(),*healthy=fopen(path("healthy").c_str(),"wb");if(!healthy)throw std::runtime_error("healthy fixture");
    a2outputs::track(healthy,"healthy fixture");a2outputs::track(failed,"failed fixture");
    a2outputs::add("good callback",good_callback);a2outputs::add("bad callback",bad_callback);callback_count=0;
    refuses(a2outputs::finish_all,"managed shutdown reports failures after attempting all outputs");
    check(!failed && !healthy && a2outputs::slots.empty() && a2outputs::finalizers.empty() && callback_count==2,"failed finalizer does not skip other callbacks or owned streams");
    failed=bad();a2outputs::track(failed,"first path");a2outputs::track(failed,"replacement path");check(a2outputs::slots.size()==1,"tracking replacement updates one owner entry");
    refuses([&]{a2outputs::finish(failed);},"live sink close propagates error");check(!failed && a2outputs::slots.empty(),"failed live close clears registry and pointer");
    failed=bad();{a2artifact::ExistingOutput owned(failed,empty.c_str());refuses([&]{owned.finish();},"scoped export refuses existing stream error");}check(!failed,"scoped export does not double close after refusal");
    healthy=fopen(path("scope").c_str(),"wb");{a2artifact::ExistingOutput owned(healthy,"scope");}check(!healthy,"early return ownership clears scoped FILE view");
    const auto directory=root.string();a2ack::publish(directory.c_str(),1,"status=OK");
    auto read=[&](const char *name){std::ifstream stream(root/name,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});};
    check(read("ack.1")=="status=OK\n" && !fs::exists(root/"ack.1.tmp"),"ACK publishes one complete newline-terminated receipt");
    refuses([&]{a2ack::publish(directory.c_str(),1,"replacement");},"ACK collision refuses");check(read("ack.1")=="status=OK\n","ACK collision preserves prior receipt");
    fs::create_directory(root/"ack.2.tmp");refuses([&]{a2ack::publish(directory.c_str(),2,"status=OK");},"ACK blocked temporary output refuses");check(!fs::exists(root/"ack.2"),"ACK failed write never publishes final file");
    for(int mode=0;mode<3;++mode){a2artifact::WriteOps ops;if(mode==0)ops.write=artifact_io_checks::write_short;if(mode==1)ops.flush=artifact_io_checks::flush_failed;if(mode==2)ops.close=artifact_io_checks::close_failed;
        refuses([&]{a2ack::publish(directory.c_str(),3+mode,"status=OK",ops);},"ACK short-write/flush/close fault refuses");check(!fs::exists(root/("ack."+std::to_string(3+mode))),"ACK finalization fault leaves no final receipt");}
    a2ack::PublishOps publication;publication.rename=rename_failed;
    refuses([&]{a2ack::publish(directory.c_str(),6,"status=OK",{},publication);},"ACK rename failure refuses");
    check(!fs::exists(root/"ack.6") && read("ack.6.tmp")=="status=OK\n","ACK rename failure retains complete temporary evidence");
    return failures;
}
}
