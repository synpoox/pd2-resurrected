// The map event's Gheed (#1842, gheed_store.cpp): PD2's unique stock in his store, and he stands still.
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Hooks D2R's trade fill and store item maker and registers Gheed's think, all found by signature; `report` gets one
// entry. Returns 1 when all are on.
auto InstallGheedStore(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command.
auto GheedStoreCounters(char* out, size_t size) noexcept -> void;
