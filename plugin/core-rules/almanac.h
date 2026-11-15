// The Horadric Almanac's identify-all (almanac.cpp): an item interaction listener, no native hook (#945).
#pragma once

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the listener; `report` gets one entry. Returns 1 when it is live.
auto InstallAlmanac(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
auto AlmanacCounters(char* out, size_t size) noexcept -> void;
