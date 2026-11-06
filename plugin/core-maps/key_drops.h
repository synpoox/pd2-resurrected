// PD2's key drop rule on every monster death (key_drops.cpp).
#pragma once

#include "d2r.h"

#include <D2RLPlugin/api.h>

#include <cstddef>

// Registers the monster-death listener; `report` gets one entry. Returns 1 when it is on.
auto InstallKeyDrops(const D2RL::PluginContext* context, char* report, size_t reportSize) noexcept -> int;
// Live counters for the status command: deaths seen and each roll's hits.
auto KeyDropsCounters(char* out, size_t size) noexcept -> void;
