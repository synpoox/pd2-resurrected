// The Dark Wanderer's boss (wanderer.cpp, #1006): PD2's buffed superunique or map boss that spawns 85 frames after the
// Wanderer leaves (his think's leave branch, through ai_hooks).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers his think (monai row 91, through ai_hooks); `report` gets one entry. Returns 1 when it is on.
auto InstallWanderer(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto WandererCounters(char* out, size_t size) noexcept -> void;
