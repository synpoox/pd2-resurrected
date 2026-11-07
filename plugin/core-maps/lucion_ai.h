// PD2's Overseer AI for Lucion, LucionControl and LucionSpawner (lucion_ai.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps monai row 120's think for the port's; `report` gets one entry. Returns 1 when it is on.
auto InstallLucionAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto LucionAiCounters(char* out, size_t size) noexcept -> void;
