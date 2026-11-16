// The final pair's shared life: a hit on one Rathma clone halves and is taken from the other too (rathma_life.cpp).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Wraps D2R's damage-type step and poison apply; `report` gets one entry. Returns how many of the 2 are on.
auto InstallRathmaLife(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto RathmaLifeCounters(char* out, size_t size) noexcept -> void;
