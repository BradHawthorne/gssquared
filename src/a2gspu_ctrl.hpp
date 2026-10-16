#pragma once
#include <string>
// The CTRL rail (A2GSPU_CTRL: cmd.N -> ack.N) and the windowed co-pilot rail (A2GSPU_COPILOT).
struct GS2AppState; struct computer_t;
void a2gspu_ctrl_loop(GS2AppState *state);
void a2gspu_copilot_poll(computer_t *computer);
std::string a2gspu_agent_capabilities(computer_t *computer);
std::string a2gspu_agent_request(GS2AppState *state,bool discovery,const std::string &command,int &frame);
