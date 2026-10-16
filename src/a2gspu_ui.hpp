#pragma once
// Native counterparts of useful menu settings; never synthesize host input.
#include "computer.hpp"
#include "videosystem.hpp"
#include "NClock.hpp"
#include "a2gspu_ctrl_json.hpp"
#include <sstream>
#include <stdexcept>
#include <cstring>
namespace a2ui {
inline const char *clock_name(clock_mode_t mode) {
    switch(mode){case CLOCK_FREE_RUN:return "free";case CLOCK_1_024MHZ:return "1.024";case CLOCK_2_8MHZ:return "2.8";case CLOCK_7_159MHZ:return "7.159";case CLOCK_14_3MHZ:return "14.3";default:return "unknown";}
}
inline std::string state(computer_t *computer) {
    auto *video=computer?computer->video_system:nullptr;
    if(!video || !computer->clock)throw std::runtime_error("presentation state unavailable");
    const char *monitor=video->display_color_engine==DM_ENGINE_NTSC?"composite":video->display_color_engine==DM_ENGINE_RGB?"rgb":video->display_color_engine==DM_ENGINE_MONO?(video->display_mono_color==DM_MONO_GREEN?"green":video->display_mono_color==DM_MONO_AMBER?"amber":"white"):"unknown";
    const auto flags=video->window?SDL_GetWindowFlags(video->window):0;
    std::ostringstream out;out<<std::boolalpha;
    out<<"{\"schema\":\"a2ui-v1\",\"model\":\"native-presentation-and-clock-settings\",\"complete_gui_automation\":false,\"monitor\":"<<a2ctrljson::quote(monitor)
       <<",\"window\":{\"available\":"<<(video->window!=nullptr)<<",\"background\":"<<video->background_window
       <<",\"hidden\":"<<bool(flags&SDL_WINDOW_HIDDEN)<<",\"focused\":"<<bool(flags&SDL_WINDOW_INPUT_FOCUS)<<",\"fullscreen\":"<<bool(flags&SDL_WINDOW_FULLSCREEN)<<"}"
       <<",\"crt\":{\"available\":"<<video->crt_shader_available()<<",\"enabled\":"<<video->get_crt_shader_enabled()<<"}"
       <<",\"clock\":{\"requested\":"<<a2ctrljson::quote(clock_name(computer->speed_new))<<",\"effective\":"<<a2ctrljson::quote(clock_name(computer->clock->get_clock_mode()))
       <<",\"pending\":"<<computer->speed_shift<<",\"apply_model\":\"normal-frame-entry; no time advance in setter\"}"
       <<",\"controls\":[{\"name\":\"monitor\",\"available\":true,\"values\":[\"composite\",\"rgb\",\"green\",\"amber\",\"white\"]},"
       <<"{\"name\":\"speed\",\"available\":true,\"values\":[\"free\",\"1.024\",\"2.8\",\"7.159\",\"14.3\"]},"
       <<"{\"name\":\"crt\",\"available\":"<<video->crt_shader_available()<<",\"reason\":"<<(video->crt_shader_available()?"null":"\"GPU shader unavailable\"")<<"}]"
       <<",\"unavailable\":[{\"scope\":\"native-file-dialogs,clipboard,fullscreen,host-pointer-capture\",\"reason\":\"host interaction is not exposed by this tool; use explicit artifact paths and existing input/mount controls\"}]"
       <<",\"proof_scope\":\"native settings/readback; not visual proof of physical display, dialogs or host audio\"}";
    return out.str();
}
inline bool command(const char *line,char *result,size_t size,computer_t *computer) {
    if(std::strncmp(line,"ui",2) || (line[2] && line[2]!=' '))return false;
    try {
        std::istringstream input(line);std::string verb,operation,value,extra;input>>verb>>operation>>value>>extra;
        if(operation.empty() || operation=="status") {
            if(!value.empty() || !extra.empty())throw std::runtime_error("ui status takes no arguments");
            a2ctrljson::native_payload=state(computer);snprintf(result,size,"status=OK ui model=native-presentation-and-clock-settings");return true;
        }
        if(value.empty() || !extra.empty())throw std::runtime_error("ui requires exactly one setting value");
        const std::string before=state(computer);auto *video=computer->video_system;bool changed=false;const char *effect="host-presentation";
        if(operation=="monitor") {
            display_color_engine_t engine;display_mono_color_t mono=video->display_mono_color;
            if(value=="composite")engine=DM_ENGINE_NTSC;
            else if(value=="rgb")engine=DM_ENGINE_RGB;
            else if(value=="green" || value=="amber" || value=="white"){engine=DM_ENGINE_MONO;mono=value=="green"?DM_MONO_GREEN:value=="amber"?DM_MONO_AMBER:DM_MONO_WHITE;}
            else throw std::runtime_error("unsupported monitor; use composite|rgb|green|amber|white");
            changed=video->display_color_engine!=engine || (engine==DM_ENGINE_MONO && video->display_mono_color!=mono);
            if(changed){video->set_display_mono_color(mono);video->set_display_engine(engine);}
        } else if(operation=="crt") {
            if(value!="on" && value!="off")throw std::runtime_error("crt requires on|off");
            if(!video->crt_shader_available())throw std::runtime_error("GPU shader unavailable; setting unchanged");
            changed=video->get_crt_shader_enabled()!=(value=="on");if(changed)video->set_crt_shader_enabled(value=="on",false);
            if(video->get_crt_shader_enabled()!=(value=="on"))throw std::runtime_error("shader apply failed; inspect ui status");
        } else if(operation=="speed") {
            clock_mode_t mode;
            if(value=="free")mode=CLOCK_FREE_RUN;else if(value=="1.024")mode=CLOCK_1_024MHZ;else if(value=="2.8")mode=CLOCK_2_8MHZ;else if(value=="7.159")mode=CLOCK_7_159MHZ;else if(value=="14.3")mode=CLOCK_14_3MHZ;else throw std::runtime_error("unsupported speed; use free|1.024|2.8|7.159|14.3");
            changed=computer->speed_new!=mode || computer->clock->get_clock_mode()!=mode;
            if(changed){computer->speed_new=mode;computer->speed_shift=true;}
            effect="requested-clock-mode; applies-at-next-normal-frame";
        } else throw std::runtime_error("unsupported ui control; discover ui status; host-only controls are not callable");
        a2ctrljson::native_payload="{\"schema\":\"a2ui-action-v1\",\"action\":"+a2ctrljson::quote(operation)+",\"value\":"+a2ctrljson::quote(value)+",\"effect\":"+a2ctrljson::quote(effect)+",\"changed\":"+(changed?"true":"false")+",\"before\":"+before+",\"state\":"+state(computer)+"}";
        snprintf(result,size,"status=OK ui action=%s changed=%d effect=%s",operation.c_str(),changed?1:0,effect);
    } catch(const std::exception &error){a2ctrljson::native_payload.clear();snprintf(result,size,"status=FAIL ui %s",error.what());}
    return true;
}
}
