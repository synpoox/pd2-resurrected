// Opening a map: item spell 16 (map_open.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Installs the item spell check and use hooks; `report` gets one entry. Returns how many rules were installed.
auto InstallMapOpen(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto MapOpenCount() noexcept -> int;
// Live counters for the status command.
auto MapOpenCounters(char* out, size_t size) noexcept -> void;
