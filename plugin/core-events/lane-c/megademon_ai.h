// Lieutenant of Sin's cooldown casts (megademon_ai.cpp, #1871): PD2's Megademon think on monai row 89.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Swaps the Megademon think (monai row 89) for megademonboss; `report` gets one entry. Returns 1 when it is on.
auto InstallMegademonAi(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto MegademonAiCounters(char* out, size_t size) noexcept -> void;
