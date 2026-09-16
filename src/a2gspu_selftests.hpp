#pragma once
// A2GSPU_CPUTEST / A2GSPU_MMUTEST: headless CPU and MMU+video micro-tests.
struct GS2AppState;
int run_cpu_microtest(GS2AppState *state, const char *which);
int run_mmu_microtest(GS2AppState *state, const char *which);
