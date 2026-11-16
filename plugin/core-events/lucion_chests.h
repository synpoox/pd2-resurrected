// The Temple of the Triune's chests (lucion_chests.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Wraps the chest operate slot; `report` gets one entry. Returns 1 when it is on.
auto InstallLucionChests(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto LucionChestsCounters(char* out, size_t size) noexcept -> void;
