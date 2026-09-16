#pragma once
// a2gspu_rail_setters.hpp -- the launch-side entry to the rails that can also be
// armed live (a2gspu_rail_setters.cpp registers them; `rail set` applies them).

// Apply one registered rail from the environment at headless start, exactly as its
// old inline getenv block did: silent when unset, its arm line when set, fatal
// (exit 2) when the value cannot be honoured. Returns a2rails::SetRc.
int a2gspu_rail_arm_env(const char *rail);
