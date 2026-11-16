// The event Nihlathak AI (nihlathak_ai.cpp), split out of dclone_ai.cpp (#943). PD2's 0x1035bc60 nihlathakMap branch
// (map boss 0x39a, #538): stages of mob waves, then RathmaDeath. It registers on monai row 99 through ai_hooks, in
// dclone_ai.cpp's InstallDcloneAi, right after the trapped souls (the part order is part of the log).
#pragma once

#include "pd2_engine.h"

#include <D2RLPlugin/api.h>

#include <cstdint>

// Registers nihlathakMap's think on monai row 99; true when it is on.
auto RegisterNihlathakAi(const D2RL::PluginContext* context) noexcept -> bool;
// Thinks, spawned, novas and deaths of the event Nihlathak, for the dclone ai counters line.
auto NihlathakAiCounts(uint32_t out[4]) noexcept -> void;
