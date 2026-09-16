#pragma once
// The headless spike (A2GSPU_SPIKE_FRAMES + -n): env arming, the boot, the gates, GSDIAG.
struct GS2AppState;
void run_headless_spike(GS2AppState *state);
