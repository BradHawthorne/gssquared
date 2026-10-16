#include "../../src/a2gspu_ctrl_json.hpp"
#include "../../src/obs_signal.hpp"
#include "../../src/util/DebugFormatter.hpp"
#include "../../src/util/audio_capture.hpp"
#include "../../src/a2gspu_events.hpp"
#include "../../src/a2gspu_agent.hpp"
#include "../../src/a2gspu_writeback.hpp"
#include "../../src/util/EventTimer.hpp"
#include "../../src/util/InterruptController.hpp"
#include "artifact_io_checks.hpp"
#include <chrono>
#include <string>
#include <iostream>
#include <stdexcept>
uint64_t debug_level=0;
int main() {
    int checks=0;
    auto check=[&](bool pass){if(!pass)throw std::runtime_error("interface contract check "+std::to_string(checks));++checks;};
    check(artifact_io_checks::run()==0);
    // Independent truth table: only request-with-no-guard combinations flush.
    const bool flush_expected[16]={false,true,true,true,false,false,false,false,false,false,false,false,false,false,false,false};
    for(unsigned bits=0;bits<16;++bits) {
        a2writeback::Policy policy{bool(bits&1),bool(bits&2),bool(bits&4),bool(bits&8)};
        check(policy.flush()==flush_expected[bits]);
    }
    check(a2agent::valid_command("cpu") && a2agent::valid_command("load 0800 source with spaces.bin"));
    check(a2agent::valid_command(std::string("text ")+"\xc3\xa9"));
    for(auto text:{std::string(),std::string(2048,'x'),std::string("cpu\nreset"),std::string("cpu\0reset",9),std::string("\xc0\x80"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("\xe2\x82")})check(!a2agent::valid_command(text));
    check(a2ctrljson::quote("a\tb\n\"\\")=="\"a\\u0009b\\u000a\\\"\\\\\"");
    char buffer[256];
    a2ctrljson::format(buffer,sizeof buffer,"cpu","status=OK\tvalue=1");
    check(std::string(buffer).find("\\u0009")!=std::string::npos);
    a2ctrljson::format(buffer,sizeof buffer,"cpu",std::string(1000,'a').c_str());
    check(std::string(buffer).find("reply-too-large")!=std::string::npos);
    check(std::string(buffer).find("\"ok\":false")!=std::string::npos);
    a2ctrljson::format(buffer,sizeof buffer,"unknown","payload status=OK");
    check(std::string(buffer).find("\"ok\":false")!=std::string::npos);
    a2ctrljson::native_status="OK";
    a2ctrljson::format(buffer,sizeof buffer,"oracle","ORACLE contract");
    check(std::string(buffer).find("\"ok\":true")!=std::string::npos);
    a2ctrljson::native_status.clear();
    uint16_t value=0x1234;uint64_t result=0;
    obs_add_scalar(0,1,0,"test.value",OBS_T_U16,&value,0,2);
    check(obs_read(obs_sigid(0,1),&result) && result==0x1234 && value==0x1234);
    obs_add_scalar(0,2,0,"test.edge",OBS_T_U16,&value,0,2,OBS_F_EDGE_ONLY);
    check(!obs_read(obs_sigid(0,2),&result));
    obs_add_scalar_h(0,3,0,"test.transient",OBS_T_U16,OBS_OWNER_VGC,0,2);
    check(!obs_read(obs_sigid(0,3),&result));
    obs_owner_register(OBS_OWNER_VGC,&value);
    check(obs_read(obs_sigid(0,3),&result) && result==value);
    obs_owner_release(OBS_OWNER_VGC);
    check(!obs_read(obs_sigid(0,3),&result));
    DebugFormatter fields;
    fields.unsignedField("register",42,8);
    fields.boolField("ready",true);
    check(fields.fields[0].value_json=="42" && fields.fields[1].value_json=="true");
    bool refused=false;try{fields.boolField("ready",false);}catch(const std::runtime_error&){refused=true;}
    check(refused);
    auto audio_path=std::filesystem::temp_directory_path()/("a2audio-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".jsonl");
    int16_t samples[]={-30000,123,0,-456,30000,789};
    audio_capture::start(0,audio_path.string(),3);
    audio_capture::note(1,samples,3,2,96,32);check(audio_capture::frames==0);
    audio_capture::note(0,samples,3,2,96,32);check(audio_capture::frames==3 && audio_capture::dropped==0);
    audio_capture::note(0,samples,1,2,128,32);check(audio_capture::dropped==1 && !audio_capture::active);
    audio_capture::stop();check(audio_capture::status().find("\"complete\":false")!=std::string::npos);
    std::ifstream recording(audio_path);std::string contents((std::istreambuf_iterator<char>(recording)),{});recording.close();
    check(contents.find("[-30000,123,0,-456,30000,789]")!=std::string::npos);
    check(contents.find("frame-cap-exceeded")!=std::string::npos);std::filesystem::remove(audio_path);
    auto event_path=audio_path;event_path+=".events";
    unsigned socket_records=0;std::string why;
    a2events::set_secondary([&](const char*,const std::string&){++socket_records;},[](const char*){return true;});
    check(a2events::open({"stop"},event_path.string(),10,why));
    a2events::publish("stop","{}");check(socket_records==1 && a2events::retained==1);
    a2events::close();a2events::publish("stop","{}");check(socket_records==2 && a2events::enabled());
    check(a2events::open({"stop"},event_path.string(),10,why));
    a2events::set_secondary({});a2events::publish("stop","{}");
    check(socket_records==2 && a2events::retained==1 && a2events::enabled());
    a2events::close();check(!a2events::enabled());std::filesystem::remove(event_path);
    check(a2agent::live_context("context overview") && a2agent::live_context("context address C030"));
    check(!a2agent::live_context("context on") && !a2agent::live_context("context mark") && !a2agent::live_context("contextual overview"));
    uint64_t identity=0,fire_id=0,irq_parent=0;unsigned scheduler_events=0;
    a2events::set_context([&](const char *kind,const std::string &payload){
        const auto id=++identity;
        if(std::string(kind)=="scheduler"){++scheduler_events;if(payload.find("\"fired\"")!=std::string::npos)fire_id=id;}
        if(std::string(kind)=="irq")irq_parent=a2events::cause_id;
        return id;
    });
    InterruptController controller;EventTimer timer;timer.observation_domain="cpu";
    timer.scheduleEvent(5,[](uint64_t,void *context){static_cast<InterruptController*>(context)->assert_irq(IRQ_ID_AGENT);},7,&controller,"test.irq","apps/interfacecontracttest/main.cpp");
    timer.scheduleEvent(9,nullptr,8);
    auto pending=timer.observation_events();
    check(pending.size()==2 && pending[0].instanceID==7 && pending[1].triggerCycles==9);
    check(std::string(pending[0].observation_owner)=="test.irq" && std::string(pending[0].observation_source)=="apps/interfacecontracttest/main.cpp");
    timer.processEvents(5);
    check(controller.get_irq(IRQ_ID_AGENT) && fire_id && irq_parent==fire_id);
    check(timer.observation_events().size()==1 && timer.observation_events()[0].instanceID==8);
    timer.cancelEvents(8);check(!timer.hasPendingEvents() && scheduler_events==4);
    a2events::set_context({});check(!a2events::enabled());
    std::cout<<"interface contract: "<<checks<<" checks passed\n";
}
