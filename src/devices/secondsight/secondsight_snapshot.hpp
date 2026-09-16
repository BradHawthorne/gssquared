
#pragma once
// FORK: snapshot block "ssight" (M5) for the SecondSight card -- see
// SecondSight::A2GSPU_snapshot. *gpu_lost reports a save taken in GPU mode,
// whose textures could not be saved; the card restores to its reset state then.
#include <cstdio>

bool secondsight_A2GSPU_present();
void secondsight_A2GSPU_snapshot(FILE *f);
bool secondsight_A2GSPU_restore(FILE *f, bool *gpu_lost);
