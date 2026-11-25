// Map boss loot follows the map tier (map_boss_tc.cpp, #1423).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the TC drop wrapper; `report` gets one entry. Returns 1 when installed.
auto InstallMapBossTc(const D2RL::PluginContext* context, char* report, size_t size) noexcept -> int;
auto MapBossTcCounters(char* out, size_t size) noexcept -> void;
