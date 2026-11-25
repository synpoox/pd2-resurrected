// The Dark Wanderer's think and boss (wanderer.cpp, #1006, #1064): his port of D2R's think without the a3q7 gate, and the
// boss that spawns 85 frames after he leaves.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers his think (monai row 91, through ai_hooks, class 938); `report` gets one entry. Returns 1 when it is on.
auto InstallWanderer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto WandererCounters(char* out, size_t size) noexcept -> void;
