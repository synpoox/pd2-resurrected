// Anya's Hell reward: 3 random T1 maps beside her class item (anya_maps.cpp, #1323).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks Anya's gift grant, found by signature; `report` gets one entry. Returns 1 when it is on.
auto InstallAnyaMaps(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto AnyaMapsCounters(char* out, size_t size) noexcept -> void;
