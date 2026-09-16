#pragma once
// The CTRL rail (A2GSPU_CTRL: cmd.N -> ack.N) and the windowed co-pilot rail (A2GSPU_COPILOT).
struct GS2AppState; struct computer_t;
void a2gspu_ctrl_loop(GS2AppState *state);
void a2gspu_copilot_poll(computer_t *computer);
