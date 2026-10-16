// One registry for socket type IDs and discovery. Protocol v1 stays additive.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include "a2gspu_events.hpp"
#define A2_DEBUG_TYPES(X) \
 X(Hello,0x0001,"HELLO","request") \
 X(Ping,0x0002,"PING","request") \
 X(Error,0x0003,"ERROR","error") \
 X(Event,0x0004,"EVENT","event") \
 X(Quit,0x0005,"QUIT","request") \
 X(Manifest,0x0006,"MANIFEST","request") \
 X(Subscribe,0x0007,"SUBSCRIBE","request") \
 X(AgentTools,0x0008,"AGENT_TOOLS","request") \
 X(AgentCommand,0x0009,"AGENT_COMMAND","request") \
 X(GetStatus,0x0101,"GET_STATUS","request") \
 X(Reset,0x0102,"RESET","request") \
 X(Pause,0x0103,"PAUSE","request") \
 X(Continue,0x0104,"CONTINUE","request") \
 X(StepInto,0x0105,"STEP_INTO","request") \
 X(GetTrace,0x0201,"GET_TRACE","request") \
 X(GetRegs,0x0202,"GET_REGS","request") \
 X(SetRegs,0x0203,"SET_REGS","request") \
 X(ReadMem,0x0301,"READMEM","request") \
 X(WriteMem,0x0302,"WRITEMEM","request") \
 X(FindMem,0x0303,"FINDMEM","request") \
 X(BpSet,0x0401,"BP_SET","request") \
 X(BpClear,0x0402,"BP_CLEAR","request") \
 X(BpClearAll,0x0403,"BP_CLEAR_ALL","request") \
 X(BpEnable,0x0404,"BP_ENABLE","request") \
 X(BpList,0x0405,"BP_LIST","request") \
 X(KeyEvent,0x0501,"KEYEVENT","request") \
 X(PasteText,0x0502,"PASTE_TEXT","request") \
 X(StateGet,0x0601,"STATE_GET","request") \
 X(StateSet,0x0602,"STATE_SET","request") \
 X(VideoText,0x0701,"VIDEO_TEXT","request") \
 X(Mount,0x0801,"MOUNT","request") \
 X(Unmount,0x0802,"UNMOUNT","request")
namespace a2debug {
#define A2_DEBUG_CONSTANT(sym,id,name,kind) inline constexpr uint32_t kType##sym = id;
A2_DEBUG_TYPES(A2_DEBUG_CONSTANT)
#undef A2_DEBUG_CONSTANT
inline std::string manifest() {
    std::string out = "{\"schema\":\"gs2-debug-manifest-v1\",\"protocol_version\":1,\"frame_bytes\":12,\"byte_order\":\"little\",\"max_payload\":1048576,\"hello_required\":true,\"reply_flag\":0,\"types\":[";
    bool first = true;
#define A2_DEBUG_ROW(sym,id,name,kind) { char row[160]; std::snprintf(row,sizeof row,"%s{\"id\":%u,\"name\":\"%s\",\"kind\":\"%s\"}",first?"":",",id,name,kind); out+=row; first=false; }
    A2_DEBUG_TYPES(A2_DEBUG_ROW)
#undef A2_DEBUG_ROW
    out += "],\"events\":[{\"id\":1,\"name\":\"stopped\"},{\"id\":2,\"name\":\"run-state\"},{\"id\":3,\"name\":\"agent-record\",\"schema\":\"a2subscription-v1\"}],\"subscription\":{\"header\":\"u32 action (0=status,1=on,2=off),u32 queue-cap (1..4096 on,0 otherwise),then UTF8 comma-separated kinds on only\",\"queue_byte_cap\":4194304,\"loss\":\"query status/off; retained means enqueued, not consumed; sequence gaps identify loss\",\"kinds\":[";
    first=true;
    for(const auto &kind:a2events::available_kinds()) {if(!first)out+=",";out+=a2ctrljson::quote(kind);first=false;}
    out += "]},\"agent_tools\":{\"discovery\":\"AGENT_TOOLS\",\"invoke\":\"AGENT_COMMAND\",\"schema\":\"a2agent-tools-v1\",\"reply\":\"a2ctrl-reply-v2\",\"requires_paused\":true,\"command_utf8_bytes_max\":2047,\"shared_dispatch\":true,\"quit\":\"QUIT\"},\"documentation\":\"Docs/DebugProtocol.md\"}";
    return out;
}
}
